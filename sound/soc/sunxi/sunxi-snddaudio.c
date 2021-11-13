/*
 * sound\soc\sunxi\sunxi-snddaudio.c
 * (C) Copyright 2014-2018
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Wolfgang Huang <huangjinhui@allwinnertech.com>
 * yumingfeng <yumingfeng@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#define DEBUG
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/debugfs.h>
#ifdef CONFIG_SND_SUNXI_MAD
#include "sunxi-mad.h"
#endif
#include "sunxi-snddaudio.h"

static atomic_t daudio_count = ATOMIC_INIT(-1);

static int sunxi_snddaudio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *card = rtd->card;
	unsigned int freq, clk_div;
	int ret;
#ifdef USE_ASOC_FMT_SETTING
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);
	struct platform_data *pdata = &(snddaudio_priv->pdata);
#endif

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 64000:
	case 12000:
	case 24000:
	case 48000:
	case 96000:
	case 192000:
#if defined(CONFIG_AHUB_FREQ_REQ)
	freq = 98304000;
#else
	freq = 24576000;
#endif
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
#if defined(CONFIG_AHUB_FREQ_REQ)
	freq = 90316800;
#else
	freq = 22579200;
#endif
		break;
	default:
		dev_err(card->dev, "unsupport params rate:%d\n", freq);
		return -EINVAL;
	}

	/* set platform clk source freq and set the mode as daudio or pcm */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, freq, 0);
	if (ret < 0) {
		dev_warn(card->dev, "cpu_dai set sysclk failed\n");
		return ret;
	}

	/* set codec clk source freq and set the mode as daudio or pcm */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, freq, 0);
	if (ret < 0)
		dev_warn(card->dev, "codec_dai set sysclk failed\n");

	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, freq, freq);
	if (ret < 0)
		dev_warn(card->dev, "codec_dai set set_pll failed.\n");

	/*
	 * SND_SOC_DAIFMT_I2S
	 * SND_SOC_DAIFMT_NB_NF
	 * SND_SOC_DAIFMT_CBS_CFS
	 */
#ifdef USE_ASOC_FMT_SETTING
	/* set codec dai fmt */
	ret = snd_soc_dai_set_fmt(codec_dai, pdata->audio_format
			| (pdata->signal_inversion << SND_SOC_DAIFMT_SIG_SHIFT)
			| (pdata->daudio_master << SND_SOC_DAIFMT_MASTER_SHIFT));
	if (ret < 0)
		dev_warn(card->dev, "codec dai set fmt failed\n");

	/* set cpu dai fmt */
	ret = snd_soc_dai_set_fmt(cpu_dai, pdata->audio_format
			| (pdata->signal_inversion << SND_SOC_DAIFMT_SIG_SHIFT)
			| (pdata->daudio_master << SND_SOC_DAIFMT_MASTER_SHIFT));
	if (ret < 0)
		dev_warn(card->dev, "cpu dai set fmt failed\n");
#else
	/* set codec dai fmt */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		dev_warn(card->dev, "codec dai set fmt failed\n");
#endif

	/* set system clk div */
	clk_div = freq / params_rate(params);
	ret = snd_soc_dai_set_clkdiv(cpu_dai, 0, clk_div);
	if (ret < 0) {
		dev_warn(card->dev, "cpu_dai set clkdiv failed\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(codec_dai, 0, clk_div);
	if (ret < 0)
		dev_warn(card->dev, "codec_dai set clkdiv failed\n");

	return 0;
}

#ifdef CONFIG_SND_SUNXI_MAD
static int sunxi_snddaudio_mad_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(rtd->card);

	snddaudio_priv->mad_priv.mad_bind = 0;
	snddaudio_priv->mad_priv.lpsd_chan_sel = 0;
	snddaudio_priv->mad_priv.mad_standby_chan_sel = 1;

	return 0;
}
#endif

static int sunxi_snddaudio_init(struct snd_soc_pcm_runtime *rtd)
{
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_snddaudio_mad_init(rtd);
#endif

	return 0;
}

/* enable the snddaudio suspend */
#ifdef CONFIG_SND_SUNXI_MAD
static int sunxi_snddaudio_mad_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(rtd->card);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (snddaudio_priv->mad_priv.mad_bind == 1)
			rtd->dai_link->ignore_suspend = 1;
	}

	return 0;
}
#endif

static int sunxi_snddaudio_startup(struct snd_pcm_substream *substream)
{
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_snddaudio_mad_startup(substream);
#endif
	return 0;
}

/* disable the snddaudio suspend */
#ifdef CONFIG_SND_SUNXI_MAD
static void sunxi_snddaudio_mad_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(rtd->card);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (snddaudio_priv->mad_priv.mad_bind == 1)
			rtd->dai_link->ignore_suspend = 0;
	}
}
#endif

static void sunxi_snddaudio_shutdown(struct snd_pcm_substream *substream)
{
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_snddaudio_mad_shutdown(substream);
#endif
}

#ifdef CONFIG_SND_SUNXI_MAD
/* mad_bind config*/
static int sunxi_snddaudio_set_mad_bind(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);

	snddaudio_priv->mad_priv.mad_bind = ucontrol->value.integer.value[0];

	return 0;
}

static int sunxi_snddaudio_get_mad_bind(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = snddaudio_priv->mad_priv.mad_bind;

	return 0;
}

static const char *mad_bind_function[] = {"mad_unbind", "mad_bind"};

static const struct soc_enum mad_bind_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mad_bind_function),
			mad_bind_function),
};

/*lpsd channel sel*/
static int sunxi_snddaudio_set_lpsd_chan(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);

	mad_priv->lpsd_chan_sel = ucontrol->value.integer.value[0];

	return 0;
}

static int sunxi_snddaudio_get_lpsd_chan(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);

	ucontrol->value.integer.value[0] = mad_priv->lpsd_chan_sel;

	return 0;
}

static const char *lpsd_chan_sel_function[] = {"0th_chan", "1st_chan", "2nd_chan",
	"3rd_chan", "4th_chan", "5th_chan", "6th_chan", "7th_chan", "8th_chan"};

static const struct soc_enum lpsd_chan_sel_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lpsd_chan_sel_function),
			lpsd_chan_sel_function),
};

/*mad_standby channel sel*/
static int sunxi_snddaudio_set_mad_standby_chan(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);

	mad_priv->mad_standby_chan_sel = ucontrol->value.integer.value[0];
	sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);

	return 0;
}

static int sunxi_snddaudio_get_mad_standby_chan(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct sunxi_snddaudio_priv *snddaudio_priv =
				snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);

	ucontrol->value.integer.value[0] = mad_priv->mad_standby_chan_sel;

	return 0;
}

static const char *mad_standby_chan_sel_function[] = {
	"Zero_chan", "Two_chan", "Four_chan"};

static const struct soc_enum mad_standby_chan_sel_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mad_standby_chan_sel_function),
			mad_standby_chan_sel_function),
};

/* daudio machine kcontrols */
static const struct snd_kcontrol_new sunxi_snddaudio_controls[] = {
	SOC_ENUM_EXT("daudio bind mad Function", mad_bind_enum[0],
		sunxi_snddaudio_get_mad_bind, sunxi_snddaudio_set_mad_bind),
	SOC_ENUM_EXT("lpsd channel sel Function", lpsd_chan_sel_enum[0],
		sunxi_snddaudio_get_lpsd_chan, sunxi_snddaudio_set_lpsd_chan),
	SOC_ENUM_EXT("mad_standby channel sel Function",
		mad_standby_chan_sel_enum[0],
		sunxi_snddaudio_get_mad_standby_chan,
		sunxi_snddaudio_set_mad_standby_chan),
};
#endif

static struct snd_soc_ops sunxi_snddaudio_ops = {
	.startup = sunxi_snddaudio_startup,
	.hw_params      = sunxi_snddaudio_hw_params,
	.shutdown = sunxi_snddaudio_shutdown,
};

static struct snd_soc_dai_link sunxi_snddaudio_dai_link = {
	.name           = "sysvoice",
	.stream_name    = "SUNXI-AUDIO",
	.cpu_dai_name   = "sunxi-daudio",
	.platform_name  = "sunxi-daudio",
	.codec_dai_name = "snd-soc-dummy-dai",
	.codec_name     = "snd-soc-dummy",
	.init           = sunxi_snddaudio_init,
	.ops            = &sunxi_snddaudio_ops,
};

static struct snd_soc_card snd_soc_sunxi_snddaudio = {
	.name           = "snddaudio",
	.owner          = THIS_MODULE,
#ifdef CONFIG_SND_SUNXI_MAD
	.controls = sunxi_snddaudio_controls,
	.num_controls = ARRAY_SIZE(sunxi_snddaudio_controls),
#endif
};

#ifdef CONFIG_SND_SUNXI_SOC_AHUB
static int sunxi_snddaudio_dev_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link	*dai_link;
	struct snd_soc_card *card;
	char name[30] = "snddaudio";
	unsigned int temp_val;
	int ret = 0;

	card = devm_kzalloc(&pdev->dev, sizeof(struct snd_soc_card),
			GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	memcpy(card, &snd_soc_sunxi_snddaudio, sizeof(struct snd_soc_card));

	card->dev = &pdev->dev;

	dai_link = devm_kzalloc(&pdev->dev,
			sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!dai_link) {
		ret = -ENOMEM;
		goto err_kfree_card;
	}

	memcpy(dai_link, &sunxi_snddaudio_dai_link,
			sizeof(struct snd_soc_dai_link));
	card->dai_link = dai_link;
	card->num_links = 1;

	dai_link->cpu_dai_name = NULL;
	dai_link->cpu_of_node = of_parse_phandle(np,
					"sunxi,cpudai-controller", 0);
	if (!dai_link->cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'sunxi,cpudai-controller' invalid\n");
		ret = -EINVAL;
		goto err_kfree_link;
	}
	dai_link->platform_name = "snd-soc-dummy";

	ret = of_property_read_string(np, "sunxi,snddaudio-codec",
			&dai_link->codec_name);
	/*
	 * As we setting codec & codec_dai in dtb, we just setting the
	 * codec & codec_dai in the dai_link. But if we just not setting,
	 * we then using the snd-soc-dummy as the codec & codec_dai to
	 * construct the snd-card for audio playback & capture.
	 */
	if (!ret) {
		ret = of_property_read_string(np, "sunxi,snddaudio-codec-dai",
				&dai_link->codec_dai_name);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai name invaild in dtb\n");
			ret = -EINVAL;
			goto err_kfree_link;
		}
		sprintf(name+3, "%s", dai_link->codec_name);
	} else {
		if (dai_link->cpu_of_node && of_property_read_u32(
			dai_link->cpu_of_node, "tdm_num", &temp_val) >= 0)
			sprintf(name+9, "%u", temp_val);
		else
			sprintf(name+9, "%d", atomic_inc_return(&daudio_count));
	}

	card->name = name;
	dev_info(card->dev, "codec: %s, codec_dai: %s.\n",
			dai_link->codec_name,
			dai_link->codec_dai_name);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(card->dev, "snd_soc_register_card failed\n");
		goto err_kfree_link;
	}

	return ret;
err_kfree_link:
	devm_kfree(&pdev->dev, card->dai_link);
err_kfree_card:
	devm_kfree(&pdev->dev, card);
	return ret;
}
#else
static int sunxi_snddaudio_dev_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link	*dai_link;
	struct snd_soc_card *card;
	struct sunxi_snddaudio_priv *snddaudio_priv;
	char name[30] = "snddaudio";
	unsigned int temp_val;
	int ret = 0;

	snddaudio_priv = devm_kzalloc(&pdev->dev,
			sizeof(struct sunxi_snddaudio_priv), GFP_KERNEL);
	if (!snddaudio_priv) {
		dev_err(&pdev->dev, "Can't malloc the sunxi_snddaudio_priv!\n");
		return -ENOMEM;
	}

	snddaudio_priv->card = devm_kzalloc(&pdev->dev,
					sizeof(struct snd_soc_card),
					GFP_KERNEL);
	if (!snddaudio_priv->card) {
		dev_err(&pdev->dev, "Can't malloc the snd_soc_card!\n");
		goto err_kfree_sndaudio_priv;
	}

	card = snddaudio_priv->card;
	memcpy(card, &snd_soc_sunxi_snddaudio, sizeof(struct snd_soc_card));

	card->dev = &pdev->dev;

	dai_link = devm_kzalloc(&pdev->dev,
			sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!dai_link) {
		ret = -ENOMEM;
		goto err_kfree_card;
	}
	memcpy(dai_link, &sunxi_snddaudio_dai_link,
			sizeof(struct snd_soc_dai_link));
	card->dai_link = dai_link;
	card->num_links = 1;

	ret = of_property_read_u32(np, "audio_format", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "audio_format config missing or invalid\n");
		snddaudio_priv->pdata.audio_format = 1;
	} else {
		snddaudio_priv->pdata.audio_format = temp_val;
	}

	ret = of_property_read_u32(np, "signal_inversion", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "signal_inversion config missing or invalid\n");
		snddaudio_priv->pdata.signal_inversion = 1;
	} else {
		snddaudio_priv->pdata.signal_inversion = temp_val;
	}

	ret = of_property_read_u32(np, "daudio_master", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "daudio_master config missing or invalid\n");
		/*
		 * default setting SND_SOC_DAIFMT_CBS_CFS mode
		 * codec clk & FRM slave
		 */
		snddaudio_priv->pdata.daudio_master = 4;
	} else {
		snddaudio_priv->pdata.daudio_master = temp_val;
	}

	dai_link->cpu_dai_name = NULL;
	dai_link->cpu_of_node = of_parse_phandle(np,
			"sunxi,daudio0-controller", 0);
	if (dai_link->cpu_of_node)
		goto cpu_node_find;

	dai_link->cpu_of_node = of_parse_phandle(np,
			"sunxi,daudio1-controller", 0);
	if (dai_link->cpu_of_node)
		goto cpu_node_find;

	dai_link->cpu_of_node = of_parse_phandle(np,
			"sunxi,daudio-controller", 0);
	if (dai_link->cpu_of_node)
		goto cpu_node_find;

	dev_err(card->dev, "Property 'sunxi,daudio-controller' missing\n");

	goto err_kfree_link;

cpu_node_find:
	dai_link->cpu_dai_name = NULL;
	dai_link->platform_name = NULL;
	dai_link->platform_of_node =
				dai_link->cpu_of_node;

	ret = of_property_read_string(np, "sunxi,snddaudio-codec",
			&dai_link->codec_name);
	/*
	 * As we setting codec & codec_dai in dtb, we just setting the
	 * codec & codec_dai in the dai_link. But if we just not setting,
	 * we then using the snd-soc-dummy as the codec & codec_dai to
	 * construct the snd-card for audio playback & capture.
	 */
	if (!ret) {
		ret = of_property_read_string(np, "sunxi,snddaudio-codec-dai",
				&dai_link->codec_dai_name);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai name invaild in dtb\n");
			ret = -EINVAL;
			goto err_kfree_link;
		}
		sprintf(name+3, "%s", dai_link->codec_name);
	} else {
		if (dai_link->cpu_of_node && of_property_read_u32(
			dai_link->cpu_of_node, "tdm_num", &temp_val) >= 0)
			sprintf(name+9, "%u", temp_val);
		else
			sprintf(name+9, "%d", atomic_inc_return(&daudio_count));
	}

	card->name = name;
	dev_info(card->dev, "codec: %s, codec_dai: %s.\n",
			dai_link->codec_name,
			dai_link->codec_dai_name);

#ifdef CONFIG_SND_SUNXI_MAD
	snddaudio_priv->mad_priv.mad_bind = 0;
#endif

	snd_soc_card_set_drvdata(card, snddaudio_priv);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(card->dev, "snd_soc_register_card failed\n");
		goto err_kfree_link;
	}

	return ret;

err_kfree_link:
	devm_kfree(&pdev->dev, card->dai_link);
err_kfree_card:
	devm_kfree(&pdev->dev, card);
err_kfree_sndaudio_priv:
	devm_kfree(&pdev->dev, snddaudio_priv);
	return ret;
}
#endif

static int sunxi_snddaudio_dev_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sunxi_snddaudio_priv *snddaudio_priv;

	snddaudio_priv = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	devm_kfree(&pdev->dev, card->dai_link);
	devm_kfree(&pdev->dev, card);
	devm_kfree(&pdev->dev, snddaudio_priv);

	return 0;
}

static const struct of_device_id sunxi_snddaudio_of_match[] = {
	{ .compatible = "allwinner,sunxi-daudio0-machine", },
	{ .compatible = "allwinner,sunxi-daudio1-machine", },
	{ .compatible = "allwinner,sunxi-daudio2-machine", },
	{ .compatible = "allwinner,sunxi-daudio3-machine", },
	{},
};

static struct platform_driver sunxi_snddaudio_driver = {
	.driver = {
		.name   = "snddaudio",
		.owner  = THIS_MODULE,
		.pm     = &snd_soc_pm_ops,
		.of_match_table = sunxi_snddaudio_of_match,
	},
	.probe  = sunxi_snddaudio_dev_probe,
	.remove = sunxi_snddaudio_dev_remove,
};

static int __init sunxi_snddaudio_driver_init(void)
{
	return platform_driver_register(&sunxi_snddaudio_driver);
}
late_initcall(sunxi_snddaudio_driver_init);

static void __exit sunxi_snddaudio_driver_exit(void)
{
	platform_driver_unregister(&sunxi_snddaudio_driver);
}
module_exit(sunxi_snddaudio_driver_exit);

MODULE_AUTHOR("Wolfgang Huang");
MODULE_DESCRIPTION("SUNXI snddaudio ALSA SoC Audio Card Driver");
MODULE_LICENSE("GPL");
