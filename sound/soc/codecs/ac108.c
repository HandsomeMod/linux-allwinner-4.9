/*
 * ac108.c  --	ac108 ALSA Soc Audio driver
 *
 * Version: 3.0
 *
 * Author: panjunwen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/of.h>
#include <sound/tlv.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>

#include "ac108.h"

#undef AC108_DEBUG_EN
#define AC108_PLAYBACK_DEBUG

#ifdef AC108_DEBUG_EN
#define AC108_DEBUG(...)		pr_err(__VA_ARGS__)
#else
#define AC108_DEBUG(...)
#endif

//test config
#undef AC108_DAPM_TEST_EN
#define AC108_CODEC_RW_TEST_EN

//0:ADC normal,  1:0x5A5A5A,  2:0x123456,  3:0x000000,
//4~7:I2S_RX_DATA,  other:reserved
#define AC108_ADC_PATTERN_SEL	ADC_PTN_0x5A5A5A

//AC108 config
#define AC108_NUM_MAX			4
#define AC108_CHANNELS_MAX		8
//16bit or 32bit slot width, other value will be reserved
#define AC108_SLOT_WIDTH		32
//TX Encoding mode enable
#define AC108_ENCODING_EN		0
//TX Encoding channel numbers, must be dual, range[1, 16]
#define AC108_ENCODING_CH_NUMS		8

//0dB~30dB and -6dB, except 1~2dB
#define AC108_PGA_GAIN			ADC_PGA_GAIN_28dB

//range[1, 1024], default PCM mode, I2S/LJ/RJ mode shall divide by 2
//#define AC108_LRCK_PERIOD		(AC108_SLOT_WIDTH * (AC108_ENCODING_EN ? 2 : AC108_CHANNELS_MAX))
#define AC108_LRCK_PERIOD		((AC108_SLOT_WIDTH * (AC108_ENCODING_EN ? 2 : AC108_CHANNELS_MAX))/2)
//AC108 SDO2/TX2 Enable (SDO1 has be enabled default)
#define AC108_SDO2_EN			0
#define AC108_DMIC_EN			0	//0:ADC	 1:DMIC

/*
 * maybe waiting the i2c status idle(about 5s).
 * IDLE_RESET_EN and POWERON_RESET_EN should be defined at the same time.
 */
//reset AC108 when in idle time
#undef AC108_IDLE_RESET_EN
//AC108 poweron soft reset enable
#undef AC108_POWERON_RESET_EN

//AC108 match method select: [undef]: i2c_detect, [define]: devices tree
#define AC108_MATCH_DTS_EN
#define AC108_REGMAP_EN

#define AC108_REGULATOR_NAME		"regulator_name"
#define AC108_RATES			(SNDRV_PCM_RATE_8000_96000 | SNDRV_PCM_RATE_KNOT)
#define AC108_FORMATS			(SNDRV_PCM_FMTBIT_S16_LE | \
					SNDRV_PCM_FMTBIT_S20_3LE | \
					SNDRV_PCM_FMTBIT_S24_LE | \
					SNDRV_PCM_FMTBIT_S32_LE)

struct ac108_public_config ac108_pub_cfg;
struct i2c_client *i2c_clt[AC108_NUM_MAX];

static const struct regmap_config ac108_regmap_config = {
	.reg_bits = 8,	//Number of bits in a register address
	.val_bits = 8,	//Number of bits in a register value
	.max_register = AC108_REG_MAX,
	.cache_type = REGCACHE_NONE,
};

struct real_val_to_reg_val {
	unsigned int real_val;
	unsigned int reg_val;
};

struct pll_div {
	u32 freq_in;
	u32 freq_out;
	u32 m1;
	u32 m2;
	u32 n;
	u32 k1;
	u32 k2;
};

static const struct real_val_to_reg_val ac108_sample_rate[] = {
	{8000,	0},
	{11025, 1},
	{12000, 2},
	{16000, 3},
	{22050, 4},
	{24000, 5},
	{32000, 6},
	{44100, 7},
	{48000, 8},
	{96000, 9},
};

static const struct real_val_to_reg_val ac108_sample_resolution[] = {
	{8,  1},
	{12, 2},
	{16, 3},
	{20, 4},
	{24, 5},
	{28, 6},
	{32, 7},
};

static const struct real_val_to_reg_val ac108_bclk_div[] = {
	{0,   0},
	{1,   1},
	{2,   2},
	{4,   3},
	{6,   4},
	{8,   5},
	{12,  6},
	{16,  7},
	{24,  8},
	{32,  9},
	{48,  10},
	{64,  11},
	{96,  12},
	{128, 13},
	{176, 14},
	{192, 15},
};

//FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;
//M1[0,31],  M2[0,1],  N[0,1023],  K1[0,31],  K2[0,1]
static const struct pll_div ac108_pll_div[] = {
	{400000,   24576000, 0,  0, 983,  7,  1},	//<out: 24.575M>
	{512000,   24576000, 0,  0, 960,  9,  1},	//24576000/48
	{768000,   24576000, 0,  0, 640,  9,  1},	//24576000/32
	{800000,   24576000, 0,  0, 768,  24, 0},
	{1024000,  24576000, 0,  0, 480,  9,  1},	//24576000/24
	{1600000,  24576000, 0,  0, 384,  24, 0},
	{2048000,  24576000, 0,  0, 240,  9,  1},	//24576000/12
	{3072000,  24576000, 0,  0, 160,  9,  1},	//24576000/8
	{4096000,  24576000, 0,  0, 120,  9,  1},	//24576000/6
	{6000000,  24576000, 4,  0, 512,  24, 0},
	{12000000, 24576000, 9,  0, 512,  24, 0},
	{13000000, 24576000, 12, 0, 639,  12, 1},	//<out: 24.577M>
	{15360000, 24576000, 9,  0, 320,  9,  1},
	{16000000, 24576000, 9,  0, 384,  24, 0},
	{19200000, 24576000, 11, 0, 384,  24, 0},
	{19680000, 24576000, 15, 1, 999,  24, 0},	//<out: 24.575M>
	{24000000, 24576000, 9,  0, 256,  24, 0},

	{400000,   22579200, 0,  0, 1016, 8,  1},	//<out: 22.5778M>
	{512000,   22579200, 0,  0, 882,  9,  1},
	{768000,   22579200, 0,  0, 588,  9,  1},
	{800000,   22579200, 0,  0, 508,  8,  1},	//<out: 22.5778M>
	{1024000,  22579200, 0,  0, 441,  9,  1},
	{1600000,  22579200, 0,  0, 254,  8,  1},	//<out: 22.5778M>
	{2048000,  22579200, 1,  0, 441,  9,  1},
	{3072000,  22579200, 2,  0, 441,  9,  1},
	{4096000,  22579200, 3,  0, 441,  9,  1},
	{6000000,  22579200, 5,  0, 429,  18, 0},	//<out: 22.5789M>
	{12000000, 22579200, 11, 0, 429,  18, 0},	//<out: 22.5789M>
	{13000000, 22579200, 12, 0, 429,  18, 0},	//<out: 22.5789M>
	{15360000, 22579200, 14, 0, 441,  9,  1},
	{16000000, 22579200, 24, 0, 882,  24, 0},
	{19200000, 22579200, 4,  0, 147,  24, 0},
	{19680000, 22579200, 13, 1, 771,  23, 0},	//<out: 22.5793M>
	{24000000, 22579200, 24, 0, 588,  24, 0},

	{12288000, 24576000, 9,  0, 400,  9,  1},	//24576000/2
	{11289600, 22579200, 9,  0, 400,  9,  1},	//22579200/2

	{24576000/1,   24576000, 9,  0, 200, 9, 1},	//24576000
	{24576000/4,   24576000, 4,  0, 400, 9, 1},	//6144000
	{24576000/16,  24576000, 0,  0, 320, 9, 1},	//1536000
	{24576000/64,  24576000, 0,  0, 640, 4, 1},	//384000
	{24576000/96,  24576000, 0,  0, 960, 4, 1},	//256000
	{24576000/128, 24576000, 0,  0, 512, 1, 1},	//192000
	{24576000/176, 24576000, 0,  0, 880, 4, 0},	//140000
	{24576000/192, 24576000, 0,  0, 960, 4, 0},	//128000

	{22579200/1,   22579200, 9,  0, 200, 9, 1},	//22579200
	{22579200/4,   22579200, 4,  0, 400, 9, 1},	//5644800
	{22579200/16,  22579200, 0,  0, 320, 9, 1},	//1411200
	{22579200/64,  22579200, 0,  0, 640, 4, 1},	//352800
	{22579200/96,  22579200, 0,  0, 960, 4, 1},	//235200
	{22579200/128, 22579200, 0,  0, 512, 1, 1},	//176400
	{22579200/176, 22579200, 0,  0, 880, 4, 0},	//128290
	{22579200/192, 22579200, 0,  0, 960, 4, 0},	//117600

	{22579200/6,   22579200, 2,  0, 360, 9, 1},	//3763200
	{22579200/8,   22579200, 0,  0, 160, 9, 1},	//2822400
	{22579200/12,  22579200, 0,  0, 240, 9, 1},	//1881600
	{22579200/24,  22579200, 0,  0, 480, 9, 1},	//940800
	{22579200/32,  22579200, 0,  0, 640, 9, 1},	//705600
	{22579200/48,  22579200, 0,  0, 960, 9, 1},	//470400
};

static const DECLARE_TLV_DB_SCALE(adc1_pga_gain_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(adc2_pga_gain_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(adc3_pga_gain_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(adc4_pga_gain_tlv, 0, 100, 0);

static const DECLARE_TLV_DB_SCALE(ch1_digital_vol_tlv, -11925, 75, 0);
static const DECLARE_TLV_DB_SCALE(ch2_digital_vol_tlv, -11925, 75, 0);
static const DECLARE_TLV_DB_SCALE(ch3_digital_vol_tlv, -11925, 75, 0);
static const DECLARE_TLV_DB_SCALE(ch4_digital_vol_tlv, -11925, 75, 0);

static const DECLARE_TLV_DB_SCALE(channel1_ch1_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel1_ch2_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel1_ch3_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel1_ch4_dig_mix_vol_tlv, -600, 600, 0);

static const DECLARE_TLV_DB_SCALE(channel2_ch1_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel2_ch2_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel2_ch3_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel2_ch4_dig_mix_vol_tlv, -600, 600, 0);

static const DECLARE_TLV_DB_SCALE(channel3_ch1_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel3_ch2_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel3_ch3_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel3_ch4_dig_mix_vol_tlv, -600, 600, 0);

static const DECLARE_TLV_DB_SCALE(channel4_ch1_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel4_ch2_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel4_ch3_dig_mix_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(channel4_ch4_dig_mix_vol_tlv, -600, 600, 0);

//static const DECLARE_TLV_DB_SCALE(adc_pga_gain_tlv,0,100,0);
//static const DECLARE_TLV_DB_SCALE(digital_vol_tlv,-11925,75,0);
//static const DECLARE_TLV_DB_SCALE(digital_mix_vol_tlv,-600,600,0);

/******************** General(volume) controls **************************/
//ac108 common controls
static const struct snd_kcontrol_new ac108_controls[] = {
	SOC_SINGLE_TLV("ADC1 PGA gain", ANA_PGA1_CTRL, ADC1_ANALOG_PGA,
			0x1f, 0, adc1_pga_gain_tlv),
	SOC_SINGLE_TLV("ADC2 PGA gain", ANA_PGA2_CTRL, ADC2_ANALOG_PGA,
			0x1f, 0, adc2_pga_gain_tlv),
	SOC_SINGLE_TLV("ADC3 PGA gain", ANA_PGA3_CTRL, ADC3_ANALOG_PGA,
			0x1f, 0, adc3_pga_gain_tlv),
	SOC_SINGLE_TLV("ADC4 PGA gain", ANA_PGA4_CTRL, ADC4_ANALOG_PGA,
			0x1f, 0, adc4_pga_gain_tlv),

	SOC_SINGLE_TLV("CH1 digital volume", ADC1_DVOL_CTRL, 0, 0xff, 0,
			ch1_digital_vol_tlv),
	SOC_SINGLE_TLV("CH2 digital volume", ADC2_DVOL_CTRL, 0, 0xff, 0,
			ch2_digital_vol_tlv),
	SOC_SINGLE_TLV("CH3 digital volume", ADC3_DVOL_CTRL, 0, 0xff, 0,
			ch3_digital_vol_tlv),
	SOC_SINGLE_TLV("CH4 digital volume", ADC4_DVOL_CTRL, 0, 0xff, 0,
			ch4_digital_vol_tlv),

	SOC_SINGLE_TLV("CH1 ch1 mixer gain", ADC1_DMIX_SRC, ADC1_ADC1_DMXL_GC,
			1, 0, channel1_ch1_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH1 ch2 mixer gain", ADC1_DMIX_SRC, ADC1_ADC2_DMXL_GC,
			1, 0, channel1_ch2_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH1 ch3 mixer gain", ADC1_DMIX_SRC, ADC1_ADC3_DMXL_GC,
			1, 0, channel1_ch3_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH1 ch4 mixer gain", ADC1_DMIX_SRC, ADC1_ADC4_DMXL_GC,
			1, 0, channel1_ch4_dig_mix_vol_tlv),

	SOC_SINGLE_TLV("CH2 ch1 mixer gain", ADC2_DMIX_SRC, ADC2_ADC1_DMXL_GC,
			1, 0, channel2_ch1_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH2 ch2 mixer gain", ADC2_DMIX_SRC, ADC2_ADC2_DMXL_GC,
			1, 0, channel2_ch2_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH2 ch3 mixer gain", ADC2_DMIX_SRC, ADC2_ADC3_DMXL_GC,
			1, 0, channel2_ch3_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH2 ch4 mixer gain", ADC2_DMIX_SRC, ADC2_ADC4_DMXL_GC,
			1, 0, channel2_ch4_dig_mix_vol_tlv),

	SOC_SINGLE_TLV("CH3 ch1 mixer gain", ADC3_DMIX_SRC, ADC3_ADC1_DMXL_GC,
			1, 0, channel3_ch1_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH3 ch2 mixer gain", ADC3_DMIX_SRC, ADC3_ADC2_DMXL_GC,
			1, 0, channel3_ch2_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH3 ch3 mixer gain", ADC3_DMIX_SRC, ADC3_ADC3_DMXL_GC,
			1, 0, channel3_ch3_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH3 ch4 mixer gain", ADC3_DMIX_SRC, ADC3_ADC4_DMXL_GC,
			1, 0, channel3_ch4_dig_mix_vol_tlv),

	SOC_SINGLE_TLV("CH4 ch1 mixer gain", ADC4_DMIX_SRC, ADC4_ADC1_DMXL_GC,
			1, 0, channel4_ch1_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH4 ch2 mixer gain", ADC4_DMIX_SRC, ADC4_ADC2_DMXL_GC,
			1, 0, channel4_ch2_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH4 ch3 mixer gain", ADC4_DMIX_SRC, ADC4_ADC3_DMXL_GC,
			1, 0, channel4_ch3_dig_mix_vol_tlv),
	SOC_SINGLE_TLV("CH4 ch4 mixer gain", ADC4_DMIX_SRC, ADC4_ADC4_DMXL_GC,
			1, 0, channel4_ch4_dig_mix_vol_tlv),

	//SOC_SINGLE_TLV("CH1 mixer gain", ADC1_DMIX_SRC, ADC1_ADC1_DMXL_GC,
	//		0x0f, 0, digital_mix_vol_tlv),
	//SOC_SINGLE_TLV("CH2 mixer gain", ADC2_DMIX_SRC, ADC2_ADC1_DMXL_GC,
	//		0x0f, 0, digital_mix_vol_tlv),
	//SOC_SINGLE_TLV("CH3 mixer gain", ADC3_DMIX_SRC, ADC3_ADC1_DMXL_GC,
	//		0x0f, 0, digital_mix_vol_tlv),
	//SOC_SINGLE_TLV("CH4 mixer gain", ADC4_DMIX_SRC, ADC4_ADC1_DMXL_GC,
	//		0x0f, 0, digital_mix_vol_tlv),
};

/*************************** DAPM controls ********************************/
//ADC12 DMIC1 Source Select Mux
static const char * const adc12_dmic1_src_mux_text[] = {
	"ADC12 switch", "DMIC1 switch"
};
static const struct soc_enum adc12_dmic1_src_mux_enum =
	SOC_ENUM_SINGLE(DMIC_EN, DMIC1_EN, 2, adc12_dmic1_src_mux_text);
static const struct snd_kcontrol_new adc12_dmic1_src_mux =
	SOC_DAPM_ENUM("ADC12 DMIC1 MUX", adc12_dmic1_src_mux_enum);

//ADC34 DMIC2 Source Select Mux
static const char * const adc34_dmic2_src_mux_text[] = {
	"ADC34 switch", "DMIC2 switch"
};
static const struct soc_enum adc34_dmic2_src_mux_enum =
	SOC_ENUM_SINGLE(DMIC_EN, DMIC2_EN, 2, adc34_dmic2_src_mux_text);
static const struct snd_kcontrol_new adc34_dmic2_src_mux =
	SOC_DAPM_ENUM("ADC34 DMIC2 MUX", adc34_dmic2_src_mux_enum);

//ADC1 Digital Source Select Mux
static const char * const adc1_digital_src_mux_text[] = {
	"ADC1 switch", "ADC2 switch", "ADC3 switch", "ADC4 switch"
};
static const struct soc_enum adc1_digital_src_mux_enum =
	SOC_ENUM_SINGLE(ADC_DSR, DIG_ADC1_SRS, 4, adc1_digital_src_mux_text);
static const struct snd_kcontrol_new adc1_digital_src_mux =
	SOC_DAPM_ENUM("ADC1 DIG MUX", adc1_digital_src_mux_enum);

//ADC2 Digital Source Select Mux
static const char * const adc2_digital_src_mux_text[] = {
	"ADC1 switch", "ADC2 switch", "ADC3 switch", "ADC4 switch"
};
static const struct soc_enum adc2_digital_src_mux_enum =
	SOC_ENUM_SINGLE(ADC_DSR, DIG_ADC2_SRS, 4, adc2_digital_src_mux_text);
static const struct snd_kcontrol_new adc2_digital_src_mux =
	SOC_DAPM_ENUM("ADC2 DIG MUX", adc2_digital_src_mux_enum);

//ADC3 Digital Source Select Mux
static const char * const adc3_digital_src_mux_text[] = {
	"ADC1 switch", "ADC2 switch", "ADC3 switch", "ADC4 switch"
};
static const struct soc_enum adc3_digital_src_mux_enum =
	SOC_ENUM_SINGLE(ADC_DSR, DIG_ADC3_SRS, 4, adc3_digital_src_mux_text);
static const struct snd_kcontrol_new adc3_digital_src_mux =
	SOC_DAPM_ENUM("ADC3 DIG MUX", adc3_digital_src_mux_enum);

//ADC4 Digital Source Select Mux
static const char * const adc4_digital_src_mux_text[] = {
	"ADC1 switch", "ADC2 switch", "ADC3 switch", "ADC4 switch"
};
static const struct soc_enum adc4_digital_src_mux_enum =
	SOC_ENUM_SINGLE(ADC_DSR, DIG_ADC4_SRS, 4, adc4_digital_src_mux_text);
static const struct snd_kcontrol_new adc4_digital_src_mux =
	SOC_DAPM_ENUM("ADC4 DIG MUX", adc4_digital_src_mux_enum);

//ADC1 Digital Source Control Mixer
static const struct snd_kcontrol_new adc1_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC1_DMIX_SRC, ADC1_ADC1_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC1_DMIX_SRC, ADC1_ADC2_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC3 DAT switch", ADC1_DMIX_SRC, ADC1_ADC3_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC4 DAT switch", ADC1_DMIX_SRC, ADC1_ADC4_DMXL_SRC, 1, 0),
};

//ADC2 Digital Source Control Mixer
static const struct snd_kcontrol_new adc2_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC2_DMIX_SRC, ADC2_ADC1_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC2_DMIX_SRC, ADC2_ADC2_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC3 DAT switch", ADC2_DMIX_SRC, ADC2_ADC3_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC4 DAT switch", ADC2_DMIX_SRC, ADC2_ADC4_DMXL_SRC, 1, 0),
};

//ADC3 Digital Source Control Mixer
static const struct snd_kcontrol_new adc3_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC3_DMIX_SRC, ADC3_ADC1_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC3_DMIX_SRC, ADC3_ADC2_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC3 DAT switch", ADC3_DMIX_SRC, ADC3_ADC3_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC4 DAT switch", ADC3_DMIX_SRC, ADC3_ADC4_DMXL_SRC, 1, 0),
};

//ADC4 Digital Source Control Mixer
static const struct snd_kcontrol_new adc4_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC4_DMIX_SRC, ADC4_ADC1_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC4_DMIX_SRC, ADC4_ADC2_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC3 DAT switch", ADC4_DMIX_SRC, ADC4_ADC3_DMXL_SRC, 1, 0),
	SOC_DAPM_SINGLE("ADC4 DAT switch", ADC4_DMIX_SRC, ADC4_ADC4_DMXL_SRC, 1, 0),
};

//I2S TX1 Ch1 Mapping Mux
static const char *i2s_tx1_ch1_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch",
	"ADC3 Sample switch", "ADC4 Sample switch"
};
static const struct soc_enum i2s_tx1_ch1_map_mux_enum =
	SOC_ENUM_SINGLE(I2S_TX1_CHMP_CTRL1, TX1_CH1_MAP, 4, i2s_tx1_ch1_map_mux_text);
static const struct snd_kcontrol_new i2s_tx1_ch1_map_mux =
	SOC_DAPM_ENUM("I2S TX1 CH1 MUX", i2s_tx1_ch1_map_mux_enum);

//I2S TX1 Ch2 Mapping Mux
static const char *i2s_tx1_ch2_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch",
	"ADC3 Sample switch", "ADC4 Sample switch"
};
static const struct soc_enum i2s_tx1_ch2_map_mux_enum =
	SOC_ENUM_SINGLE(I2S_TX1_CHMP_CTRL1, TX1_CH2_MAP, 4, i2s_tx1_ch2_map_mux_text);
static const struct snd_kcontrol_new i2s_tx1_ch2_map_mux =
	SOC_DAPM_ENUM("I2S TX1 CH2 MUX", i2s_tx1_ch2_map_mux_enum);

//I2S TX1 Ch3 Mapping Mux
static const char *i2s_tx1_ch3_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch",
	"ADC3 Sample switch", "ADC4 Sample switch"
};
static const struct soc_enum i2s_tx1_ch3_map_mux_enum =
	SOC_ENUM_SINGLE(I2S_TX1_CHMP_CTRL1, TX1_CH3_MAP, 4, i2s_tx1_ch3_map_mux_text);
static const struct snd_kcontrol_new i2s_tx1_ch3_map_mux =
	SOC_DAPM_ENUM("I2S TX1 CH3 MUX", i2s_tx1_ch3_map_mux_enum);

//I2S TX1 Ch4 Mapping Mux
static const char *i2s_tx1_ch4_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch",
	"ADC3 Sample switch", "ADC4 Sample switch"
};
static const struct soc_enum i2s_tx1_ch4_map_mux_enum =
	SOC_ENUM_SINGLE(I2S_TX1_CHMP_CTRL1, TX1_CH4_MAP, 4, i2s_tx1_ch4_map_mux_text);
static const struct snd_kcontrol_new i2s_tx1_ch4_map_mux =
	SOC_DAPM_ENUM("I2S TX1 CH4 MUX", i2s_tx1_ch4_map_mux_enum);

/************************* DAPM widgets **********************************/
//ac108 dapm widgets
static const struct snd_soc_dapm_widget ac108_dapm_widgets[] = {
	//input widgets
	SND_SOC_DAPM_INPUT("MIC1P"),
	SND_SOC_DAPM_INPUT("MIC1N"),

	SND_SOC_DAPM_INPUT("MIC2P"),
	SND_SOC_DAPM_INPUT("MIC2N"),

	SND_SOC_DAPM_INPUT("MIC3P"),
	SND_SOC_DAPM_INPUT("MIC3N"),

	SND_SOC_DAPM_INPUT("MIC4P"),
	SND_SOC_DAPM_INPUT("MIC4N"),

	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),


	//MIC PGA
	SND_SOC_DAPM_PGA("MIC1 PGA", ANA_ADC1_CTRL1, ADC1_PGA_ENABLE, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 PGA", ANA_ADC2_CTRL1, ADC2_PGA_ENABLE, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC3 PGA", ANA_ADC3_CTRL1, ADC3_PGA_ENABLE, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC4 PGA", ANA_ADC4_CTRL1, ADC4_PGA_ENABLE, 0, NULL, 0),

	//DMIC PGA
	SND_SOC_DAPM_PGA("DMIC1L PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC1R PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("DMIC2L PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2R PGA", SND_SOC_NOPM, 0, 0, NULL, 0),


	//ADC1 DIG MUX
	SND_SOC_DAPM_MUX("ADC1 DIG MUX", ADC_DIG_EN, ENAD1, 0, &adc1_digital_src_mux),

	//ADC2 DIG MUX
	SND_SOC_DAPM_MUX("ADC2 DIG MUX", ADC_DIG_EN, ENAD2, 0, &adc2_digital_src_mux),

	//ADC3 DIG MUX
	SND_SOC_DAPM_MUX("ADC3 DIG MUX", ADC_DIG_EN, ENAD3, 0, &adc3_digital_src_mux),

	//ADC4 DIG MUX
	SND_SOC_DAPM_MUX("ADC4 DIG MUX", ADC_DIG_EN, ENAD4, 0, &adc4_digital_src_mux),


	//ADC12 DMIC1 MUX
	SND_SOC_DAPM_MUX("ADC12 DMIC1 MUX", SND_SOC_NOPM, 0, 0, &adc12_dmic1_src_mux),

	//ADC34 DMIC2 MUX
	SND_SOC_DAPM_MUX("ADC34 DMIC2 MUX", SND_SOC_NOPM, 0, 0, &adc34_dmic2_src_mux),


	//ADC1 VIR PGA
	SND_SOC_DAPM_PGA("ADC1 VIR PGA", ANA_ADC1_CTRL1, ADC1_DSM_ENABLE, 0, NULL, 0),

	//ADC2 VIR PGA
	SND_SOC_DAPM_PGA("ADC2 VIR PGA", ANA_ADC2_CTRL1, ADC2_DSM_ENABLE, 0, NULL, 0),

	//ADC3 VIR PGA
	SND_SOC_DAPM_PGA("ADC3 VIR PGA", ANA_ADC3_CTRL1, ADC3_DSM_ENABLE, 0, NULL, 0),

	//ADC4 VIR PGA
	SND_SOC_DAPM_PGA("ADC4 VIR PGA", ANA_ADC4_CTRL1, ADC4_DSM_ENABLE, 0, NULL, 0),


	//ADC1 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC1 DIG MIXER", SND_SOC_NOPM, 0, 0,
		    adc1_digital_src_mixer, ARRAY_SIZE(adc1_digital_src_mixer)),

	//ADC2 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC2 DIG MIXER", SND_SOC_NOPM, 0, 0,
		    adc2_digital_src_mixer, ARRAY_SIZE(adc2_digital_src_mixer)),

	//ADC3 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC3 DIG MIXER", SND_SOC_NOPM, 0, 0,
		    adc3_digital_src_mixer, ARRAY_SIZE(adc3_digital_src_mixer)),

	//ADC4 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC4 DIG MIXER", SND_SOC_NOPM, 0, 0,
		    adc4_digital_src_mixer, ARRAY_SIZE(adc4_digital_src_mixer)),


	//I2S TX1 CH1 MUX
	SND_SOC_DAPM_MUX("I2S TX1 CH1 MUX", SND_SOC_NOPM, 0, 0,
			&i2s_tx1_ch1_map_mux),

	//I2S TX1 CH2 MUX
	SND_SOC_DAPM_MUX("I2S TX1 CH2 MUX", SND_SOC_NOPM, 0, 0,
			&i2s_tx1_ch2_map_mux),

	//I2S TX1 CH3 MUX
	SND_SOC_DAPM_MUX("I2S TX1 CH3 MUX", SND_SOC_NOPM, 0, 0,
			&i2s_tx1_ch3_map_mux),

	//I2S TX1 CH4 MUX
	SND_SOC_DAPM_MUX("I2S TX1 CH4 MUX", SND_SOC_NOPM, 0, 0,
			&i2s_tx1_ch4_map_mux),


	//AIF OUT -> (stream widget, stname must be same with
	//codec dai_driver stream_name, which will be used to build dai widget)
	SND_SOC_DAPM_AIF_OUT("AIF ADC OUT", "Capture", 0, SND_SOC_NOPM, 0, 0),
};


/***************************** DAPM routes ********************************/
//ac108 dapm routes
static const struct snd_soc_dapm_route ac108_dapm_routes[] = {
	//MIC
	{"MIC1 PGA", NULL, "MIC1P"},
	{"MIC1 PGA", NULL, "MIC1N"},

	{"MIC2 PGA", NULL, "MIC2P"},
	{"MIC2 PGA", NULL, "MIC2N"},

	{"MIC3 PGA", NULL, "MIC3P"},
	{"MIC3 PGA", NULL, "MIC3N"},

	{"MIC4 PGA", NULL, "MIC4P"},
	{"MIC4 PGA", NULL, "MIC4N"},

	//DMIC
	{"DMIC1L PGA", NULL, "DMIC1"},
	{"DMIC1R PGA", NULL, "DMIC1"},

	{"DMIC2L PGA", NULL, "DMIC2"},
	{"DMIC2R PGA", NULL, "DMIC2"},


	//ADC1 DIG MUX
	{"ADC1 DIG MUX", "ADC1 switch", "MIC1 PGA"},
	{"ADC1 DIG MUX", "ADC2 switch", "MIC2 PGA"},
	{"ADC1 DIG MUX", "ADC3 switch", "MIC3 PGA"},
	{"ADC1 DIG MUX", "ADC4 switch", "MIC4 PGA"},

	//ADC2 DIG MUX
	{"ADC2 DIG MUX", "ADC1 switch", "MIC1 PGA"},
	{"ADC2 DIG MUX", "ADC2 switch", "MIC2 PGA"},
	{"ADC2 DIG MUX", "ADC3 switch", "MIC3 PGA"},
	{"ADC2 DIG MUX", "ADC4 switch", "MIC4 PGA"},

	//ADC3 DIG MUX
	{"ADC3 DIG MUX", "ADC1 switch", "MIC1 PGA"},
	{"ADC3 DIG MUX", "ADC2 switch", "MIC2 PGA"},
	{"ADC3 DIG MUX", "ADC3 switch", "MIC3 PGA"},
	{"ADC3 DIG MUX", "ADC4 switch", "MIC4 PGA"},

	//ADC4 DIG MUX
	{"ADC4 DIG MUX", "ADC1 switch", "MIC1 PGA"},
	{"ADC4 DIG MUX", "ADC2 switch", "MIC2 PGA"},
	{"ADC4 DIG MUX", "ADC3 switch", "MIC3 PGA"},
	{"ADC4 DIG MUX", "ADC4 switch", "MIC4 PGA"},


	//ADC12 DMIC1 MUX
	{"ADC12 DMIC1 MUX", "ADC12 switch", "ADC1 DIG MUX"},
	{"ADC12 DMIC1 MUX", "ADC12 switch", "ADC2 DIG MUX"},
	{"ADC12 DMIC1 MUX", "DMIC1 switch", "DMIC1L PGA"},
	{"ADC12 DMIC1 MUX", "DMIC1 switch", "DMIC1R PGA"},

	//ADC34 DMIC2 MUX
	{"ADC34 DMIC2 MUX", "ADC34 switch", "ADC3 DIG MUX"},
	{"ADC34 DMIC2 MUX", "ADC34 switch", "ADC4 DIG MUX"},
	{"ADC34 DMIC2 MUX", "DMIC2 switch", "DMIC2L PGA"},
	{"ADC34 DMIC2 MUX", "DMIC2 switch", "DMIC2R PGA"},


	//ADC1 VIR PGA
	{"ADC1 VIR PGA", NULL, "ADC12 DMIC1 MUX"},

	//ADC2 VIR PGA
	{"ADC2 VIR PGA", NULL, "ADC12 DMIC1 MUX"},

	//ADC3 VIR PGA
	{"ADC3 VIR PGA", NULL, "ADC34 DMIC2 MUX"},

	//ADC4 VIR PGA
	{"ADC4 VIR PGA", NULL, "ADC34 DMIC2 MUX"},


	//ADC1 DIG MIXER
	{"ADC1 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC1 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},
	{"ADC1 DIG MIXER", "ADC3 DAT switch", "ADC3 VIR PGA"},
	{"ADC1 DIG MIXER", "ADC4 DAT switch", "ADC4 VIR PGA"},

	//ADC2 DIG MIXER
	{"ADC2 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC2 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},
	{"ADC2 DIG MIXER", "ADC3 DAT switch", "ADC3 VIR PGA"},
	{"ADC2 DIG MIXER", "ADC4 DAT switch", "ADC4 VIR PGA"},

	//ADC3 DIG MIXER
	{"ADC3 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC3 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},
	{"ADC3 DIG MIXER", "ADC3 DAT switch", "ADC3 VIR PGA"},
	{"ADC3 DIG MIXER", "ADC4 DAT switch", "ADC4 VIR PGA"},

	//ADC4 DIG MIXER
	{"ADC4 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC4 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},
	{"ADC4 DIG MIXER", "ADC3 DAT switch", "ADC3 VIR PGA"},
	{"ADC4 DIG MIXER", "ADC4 DAT switch", "ADC4 VIR PGA"},


	//I2S TX1 CH1 MUX
	{"I2S TX1 CH1 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX1 CH1 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},
	{"I2S TX1 CH1 MUX", "ADC3 Sample switch", "ADC3 DIG MIXER"},
	{"I2S TX1 CH1 MUX", "ADC4 Sample switch", "ADC4 DIG MIXER"},

	//I2S TX1 CH2 MUX
	{"I2S TX1 CH2 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX1 CH2 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},
	{"I2S TX1 CH2 MUX", "ADC3 Sample switch", "ADC3 DIG MIXER"},
	{"I2S TX1 CH2 MUX", "ADC4 Sample switch", "ADC4 DIG MIXER"},

	//I2S TX1 CH3 MUX
	{"I2S TX1 CH3 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX1 CH3 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},
	{"I2S TX1 CH3 MUX", "ADC3 Sample switch", "ADC3 DIG MIXER"},
	{"I2S TX1 CH3 MUX", "ADC4 Sample switch", "ADC4 DIG MIXER"},

	//I2S TX1 CH4 MUX
	{"I2S TX1 CH4 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX1 CH4 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},
	{"I2S TX1 CH4 MUX", "ADC3 Sample switch", "ADC3 DIG MIXER"},
	{"I2S TX1 CH4 MUX", "ADC4 Sample switch", "ADC4 DIG MIXER"},


	//AIF OUT
	{"AIF ADC OUT", NULL, "I2S TX1 CH1 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX1 CH2 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX1 CH3 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX1 CH4 MUX"},
};


static int ac108_read(u8 reg, u8 *rt_value, struct i2c_client *client)
{
#ifdef AC108_REGMAP_EN
	struct ac108_priv *ac108 = dev_get_drvdata(&client->dev);

	return regmap_read(ac108->regmap, (unsigned int)reg, (unsigned int *)rt_value);
#else
	int ret;
	u8 read_cmd[3] = {0};
	u8 cmd_len = 0;

	read_cmd[0] = reg;
	cmd_len = 1;

	if (client->adapter == NULL)
		pr_err("ac108_read client->adapter==NULL\n");

	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len) {
		pr_err("ac108_read error1\n");
		return -1;
	}

	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1) {
		pr_err("ac108_read error2, ret = %d.\n", ret);
		return -1;
	}

	return 0;
#endif
}

static int ac108_write(u8 reg, unsigned char value, struct i2c_client *client)
{
#ifdef AC108_REGMAP_EN
	struct ac108_priv *ac108 = dev_get_drvdata(&client->dev);
	return regmap_write(ac108->regmap, (unsigned int)reg, (unsigned int)value);
#else
	int ret = 0;
	u8 write_cmd[2] = {0};

	write_cmd[0] = reg;
	write_cmd[1] = value;

	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2) {
		pr_err("ac108_write error->[REG-0x%02x,val-0x%02x]\n", reg, value);
		return -1;
	}

	return 0;
#endif
}

static int ac108_update_bits(u8 reg, u8 mask, u8 value,
			struct i2c_client *client)
{
	u8 val_old = 0;
	u8 val_new = 0;

	ac108_read(reg, &val_old, client);
	val_new = (val_old & ~mask) | (value & mask);
	if (val_new != val_old)
		ac108_write(reg, val_new, client);

	return 0;
}

#if 0
static int ac108_multi_chips_read(u8 reg, unsigned char *rt_value)
{
	u8 i;

	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++)
		ac108_read(reg, rt_value++, i2c_clt[i]);

	return 0;
}
#endif

#ifdef AC108_IDLE_RESET_EN
static int ac108_multi_chips_write(u8 reg, unsigned char value)
{
	u8 i;

	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++)
		ac108_write(reg, value, i2c_clt[i]);

	return 0;
}
#endif

static int ac108_multi_chips_update_bits(u8 reg, u8 mask, u8 value)
{
	u8 i;

	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++)
		ac108_update_bits(reg, mask, value, i2c_clt[i]);

	return 0;
}

static void ac108_set_pga_gain(struct i2c_client *i2c)
{
	struct ac108_priv *ac108 = dev_get_drvdata(&i2c->dev);
	u32 i;

	if (ac108->ref_cfg.ref_pga.used) {
		dev_info(&i2c->dev, "ref_cfg.ref_channel:0x%x, set ref_pga_gain:%d\n",
			 ac108->ref_cfg.ref_channel, ac108->ref_cfg.ref_pga.val);
		/* set the gain for referenced channels, always used for aec */
		for (i = 0; i < 4; i++) {
			if ((ac108->ref_cfg.ref_channel >> i) & 0x1) {
				ac108_write(ANA_PGA1_CTRL + i,
					ac108->ref_cfg.ref_pga.val << 0x1, i2c);
			} else
				ac108_write(ANA_PGA1_CTRL + i, ac108->pga_gain << 0x1, i2c);
		}
	} else {
		for (i = 0; i < 4; i++)
			ac108_write(ANA_PGA1_CTRL + i, ac108->pga_gain, i2c);
	}
}

static void ac108_hw_init(struct i2c_client *i2c)
{
	struct ac108_priv *ac108 = dev_get_drvdata(&i2c->dev);

	/*** Chip reset ***/
	/*0x00=0x12: reset all registers to their default state*/
	//ac108_write(CHIP_AUDIO_RST, 0x12, i2c);

#if !AC108_DMIC_EN
	/*** Analog voltage enable ***/
	/*0x06=0x01: Enable Analog LDO*/
	ac108_write(PWR_CTRL6, 0x01, i2c);
	/*
	 * 0x07=0x9b: VREF faststart Enable,
	 * Enable VREF @ 3.4V (5V) or 3.1V (3.3V)
	 * (needed for Analog LDO and MICBIAS)
	 */
	ac108_write(PWR_CTRL7, 0x9b, i2c);
	/*
	 * 0x09=0x81: VREFP faststart Enable, Enable VREFP_FASTSTART_ENABLE
	 * (needed by all audio input channels)
	 */
	ac108_write(PWR_CTRL9, 0x81, i2c);
	/*
	 * DSM low power mode enable, Control bias current for
	 * DSM integrator opamps
	 */
	ac108_write(ANA_ADC3_CTRL7, 0x0b, i2c);
#endif
	/*** SYSCLK Config ***/
	/*SYSCLK Enable*/
	ac108_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_EN, 0x1 << SYSCLK_EN, i2c);
	/*
	 * 0x21=0x93: Module clock enable<I2S, ADC digital,
	 * MIC offset Calibration, ADC analog>
	 */
	ac108_write(MOD_CLK_EN, 0x93, i2c);
	/*
	 * 0x22=0x93: Module reset de-asserted
	 * <I2S, ADC digital, MIC offset Calibration, ADC analog>
	 */
	ac108_write(MOD_RST_CTRL, 0x93, i2c);
	/*** I2S Common Config ***/
	/*SDO1 enable, SDO2 Enable*/
	ac108_update_bits(I2S_CTRL, ((0x1 << SDO1_EN) | (0x1 << SDO2_EN)),
			((0x1 << SDO1_EN) | (!!AC108_SDO2_EN << SDO2_EN)), i2c);
	/*SDO drive data and SDI sample data at the different BCLK edge*/
	ac108_update_bits(I2S_BCLK_CTRL, (0x1 << EDGE_TRANSFER),
			(0x0 << EDGE_TRANSFER), i2c);
	ac108_update_bits(I2S_LRCK_CTRL1, 0x3 << LRCK_PERIODH,
			(((ac108->lrck_period - 1) >> 8) << LRCK_PERIODH), i2c);

	/*
	 * config LRCK period:
	 * 16bit * 8ch = 128,
	 * 32bit * 8ch = 256,
	 * 32bit *16ch = 512
	 */
	ac108_write(I2S_LRCK_CTRL2, (ac108->lrck_period - 1) & 0xFF, i2c);
	/*
	 * Encoding mode enable, Turn to hi-z state (TDM)
	 * when not transferring slot
	 */
	ac108_update_bits(I2S_FMT_CTRL1,
		0x1 << ENCD_SEL | 0x1 << TX_SLOT_HIZ | 0x1 << TX_STATE,
		!!AC108_ENCODING_EN << ENCD_SEL | 0x0 << TX_SLOT_HIZ | 0x1 << TX_STATE, i2c);
	/* 8/12/16/20/24/28/32bit Slot Width */
	ac108_update_bits(I2S_FMT_CTRL2, 0x7 << SLOT_WIDTH_SEL,
			(ac108->slot_width / 4 - 1) << SLOT_WIDTH_SEL, i2c);
	/*
	 * 0x36=0x70: TX MSB first, TX2 Mute, Transfer 0 after
	 * each sample in each slot(sample resolution < slot width),
	 * LRCK = 1 BCLK width (short frame), Linear PCM Data Mode
	 */
	ac108_write(I2S_FMT_CTRL3, AC108_SDO2_EN ? 0x60 : 0x70, i2c);

	/*0x3C=0xe4: TX1 CHn Map to CHn adc sample, n=[1,4]*/
	ac108_write(I2S_TX1_CHMP_CTRL1, 0xe4, i2c);
	/*0x3D=0xe4: TX1 CHn Map to CH(n-4) adc sample, n=[5,8]*/
	ac108_write(I2S_TX1_CHMP_CTRL2, 0xe4, i2c);
	/*0x3E=0xe4: TX1 CHn Map to CH(n-8) adc sample, n=[9,12]*/
	ac108_write(I2S_TX1_CHMP_CTRL3, 0xe4, i2c);
	/*0x3F=0xe4: TX1 CHn Map to CH(n-12) adc sample, n=[13,16]*/
	ac108_write(I2S_TX1_CHMP_CTRL4, 0xe4, i2c);

#if AC108_SDO2_EN
	/*
	 * 0x44=0x4e: TX2 CH1/2 Map to CH3/4 adc sample,
	 * TX2 CH3/4 Map to CH1/2 adc sample
	 */
	ac108_write(I2S_TX2_CHMP_CTRL1, 0x4e, i2c);
	/*0x45=0xe4: TX2 CHn Map to CH(n-4) adc sample, n=[5,8]*/
	ac108_write(I2S_TX2_CHMP_CTRL2, 0xe4, i2c);
	/*0x46=0xe4: TX2 CHn Map to CH(n-8) adc sample, n=[9,12]*/
	ac108_write(I2S_TX2_CHMP_CTRL3, 0xe4, i2c);
	/*0x47=0xe4: TX2 CHn Map to CH(n-12) adc sample, n=[13,16]*/
	ac108_write(I2S_TX2_CHMP_CTRL4, 0xe4, i2c);
#endif

	/*** ADC DIG part Config***/
	/*0x60=0x03: ADC Sample Rate 16KHz*/
	//ac108_write(ADC_SPRC, 0x03, i2c);
	/*0x61=0x1f: Digital part globe enable, ADCs digital part enable*/
	ac108_write(ADC_DIG_EN, 0x1f, i2c);
	/*0xBB=0x0f: Gating ADCs CLK de-asserted (ADCs CLK Enable)*/
	ac108_write(ANA_ADC4_CTRL7, 0x0f, i2c);

	if (ac108->debug_mode) {
		/*0x66=0x00: Digital ADCs channel HPF disable*/
		ac108_write(HPF_EN, 0x00, i2c);
	}
	/*
	 * 0X7F=0x00: ADC pattern select: 0:ADC normal, 1:0x5A5A5A,
	 * 2:0x123456, 3:0x00, 4~7:I2S RX data
	 */
	ac108_write(ADC_DIG_DEBUG, ac108->debug_mode & 0x7, i2c);

#if !AC108_DMIC_EN
	/*** ADCs analog PGA gain Config***/
	ac108_set_pga_gain(i2c);

	/*** enable AAF/ADC/PGA  and UnMute Config ***/
	/*
	 * 0xA0=0x07: ADC1 AAF & ADC enable, ADC1 PGA enable,
	 * ADC1 MICBIAS enable and UnMute
	 */
	ac108_write(ANA_ADC1_CTRL1, 0x07, i2c);
	/*
	 * 0xA7=0x07: ADC2 AAF & ADC enable, ADC2 PGA enable,
	 * ADC2 MICBIAS enable and UnMute
	 */
	ac108_write(ANA_ADC2_CTRL1, 0x07, i2c);
	/*
	 * 0xAE=0x07: ADC3 AAF & ADC enable, ADC3 PGA enable,
	 * ADC3 MICBIAS enable and UnMute
	 */
	ac108_write(ANA_ADC3_CTRL1, 0x07, i2c);
	/*
	 * 0xB5=0x07: ADC4 AAF & ADC enable, ADC4 PGA enable,
	 * ADC4 MICBIAS enable and UnMute
	 */
	ac108_write(ANA_ADC4_CTRL1, 0x07, i2c);

	/*
	 * delay 50ms to let VREF/VRP faststart powerup stable,
	 * then disable faststart
	 */
	msleep(50);
	/* VREF faststart disable */
	ac108_update_bits(PWR_CTRL7, 0x1 << VREF_FASTSTART_ENABLE,
			0x0 << VREF_FASTSTART_ENABLE, i2c);
	/*VREFP faststart disable*/
	ac108_update_bits(PWR_CTRL9, 0x1 << VREFP_FASTSTART_ENABLE,
			0x0 << VREFP_FASTSTART_ENABLE, i2c);
#else
	/*** DMIC module Enable ***/
	/*DMIC1/2 Enable, while ADC DIG source select DMIC1/2*/
	ac108_write(DMIC_EN, 0x03, i2c);
	/*GPIO1 as DMIC1_DAT, GPIO2 as DMIC_CLK*/
	ac108_write(GPIO_CFG1, 0xee, i2c);
	/*GPIO3 as DMIC2_DAT*/
	ac108_write(GPIO_CFG2, 0x7e, i2c);
#endif
}

static int ac108_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			unsigned int freq, int dir)
{
	switch (clk_id) {
	case SYSCLK_SRC_MCLK:
		AC108_DEBUG("AC108 SYSCLK source select MCLK\n\n");
		//System Clock Source Select MCLK
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_SRC,
					0x0 << SYSCLK_SRC);
	break;
	case SYSCLK_SRC_PLL:
		AC108_DEBUG("AC108 SYSCLK source select PLL\n\n");
		//System Clock Source Select PLL
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_SRC,
					0x1 << SYSCLK_SRC);
	break;
	default:
		pr_err("AC108 SYSCLK source config error:%d\n\n", clk_id);
	return -EINVAL;
	}

	//SYSCLK Enable
	ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x1<<SYSCLK_EN,
					0x1<<SYSCLK_EN);
	return 0;
}

static int ac108_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	u32 i = 0;
	u32 m1 = 0;
	u32 m2 = 0;
	u32 n = 0;
	u32 k1 = 0;
	u32 k2 = 0;

	if (!freq_out)
		return 0;

	if (freq_in < 128000 || freq_in > 24576000) {
		pr_err("AC108 PLLCLK source input freq only support [128K,24M],while now %u\n\n", freq_in);
		return -EINVAL;
	} else if ((freq_in == 24576000 || freq_in == 22579200) &&
		pll_id == SYSCLK_SRC_MCLK) {
		//System Clock Source Select MCLK, SYSCLK Enable
		AC108_DEBUG("AC108 don't need to use PLL\n\n");
		ac108_multi_chips_update_bits(SYSCLK_CTRL,
				0x1 << SYSCLK_SRC | 0x1 << SYSCLK_EN,
				0x0 << SYSCLK_SRC | 0x1 << SYSCLK_EN);
		return 0;	//Don't need to use PLL
	}

	//PLL Clock Source Select
	switch (pll_id) {
	case PLLCLK_SRC_MCLK:
		AC108_DEBUG("AC108 PLLCLK input source select MCLK\n");
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					0x0 << PLLCLK_SRC);
	break;
	case PLLCLK_SRC_BCLK:
		AC108_DEBUG("AC108 PLLCLK input source select BCLK\n");
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					0x1 << PLLCLK_SRC);
	break;
	case PLLCLK_SRC_GPIO2:
		AC108_DEBUG("AC108 PLLCLK input source select GPIO2\n");
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					0x2 << PLLCLK_SRC);
	break;
	case PLLCLK_SRC_GPIO3:
		AC108_DEBUG("AC108 PLLCLK input source select GPIO3\n");
		ac108_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					0x3 << PLLCLK_SRC);
	break;
	default:
		pr_err("AC108 PLLCLK source config error:%d\n\n", pll_id);
	return -EINVAL;
	}

	//FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;
	for (i = 0; i < ARRAY_SIZE(ac108_pll_div); i++) {
		if (ac108_pll_div[i].freq_in == freq_in &&
			ac108_pll_div[i].freq_out == freq_out) {
			m1 = ac108_pll_div[i].m1;
			m2 = ac108_pll_div[i].m2;
			n = ac108_pll_div[i].n;
			k1 = ac108_pll_div[i].k1;
			k2 = ac108_pll_div[i].k2;
			AC108_DEBUG("AC108 PLL freq_in match:%u, freq_out:%u\n\n",
				freq_in, freq_out);
			break;
		}
	}

	if (i == ARRAY_SIZE(ac108_pll_div)) {
		pr_err("AC108 don't match PLLCLK freq_in and freq_out table\n\n");
		return -EINVAL;
	}

	//Config PLL DIV param M1/M2/N/K1/K2
	ac108_multi_chips_update_bits(PLL_CTRL2,
			0x1f << PLL_PREDIV1 | 0x1 << PLL_PREDIV2,
			m1 << PLL_PREDIV1 | m2 << PLL_PREDIV2);
	ac108_multi_chips_update_bits(PLL_CTRL3, 0x3 << PLL_LOOPDIV_MSB,
				(n >> 8) << PLL_LOOPDIV_MSB);
	ac108_multi_chips_update_bits(PLL_CTRL4, 0xff << PLL_LOOPDIV_LSB,
				(u8)n << PLL_LOOPDIV_LSB);
	ac108_multi_chips_update_bits(PLL_CTRL5,
				(0x1f << PLL_POSTDIV1) | (0x1 << PLL_POSTDIV2),
				k1 << PLL_POSTDIV1 | (k2 << PLL_POSTDIV2));

	//Config PLL module current
	//ac108_multi_chips_update_bits(PLL_CTRL1, 0x7<<PLL_IBIAS, 0x4<<PLL_IBIAS);
	//ac108_multi_chips_update_bits(PLL_CTRL6, 0x1f<<PLL_CP, 0xf<<PLL_CP);

	//PLL module enable
	//PLL CLK lock enable
	ac108_multi_chips_update_bits(PLL_LOCK_CTRL, 0x1 << PLL_LOCK_EN,
				0x1 << PLL_LOCK_EN);
	//PLL Common voltage Enable, PLL Enable
	//ac108_multi_chips_update_bits(PLL_CTRL1,
	//			0x1 << PLL_EN | 0x1 << PLL_COM_EN,
	//			0x1<<PLL_EN | 0x1<<PLL_COM_EN);

	//PLLCLK Enable, SYSCLK Enable
	//0x1<<SYSCLK_SRC
	ac108_multi_chips_update_bits(SYSCLK_CTRL,
				0x1 << PLLCLK_EN | 0x1 << SYSCLK_EN,
				0x1 << PLLCLK_EN | 0x1 << SYSCLK_EN);

	return 0;
}

static int ac108_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	struct ac108_priv *ac108 = dev_get_drvdata(dai->dev);
	u32 i = 0;
	u32 bclk_div = 0;
	u32 bclk_div_reg_val = 0;

	if (!div_id) {
		/*
		 * use div_id to judge Master/Slave mode,
		 * 0: Slave mode, 1: Master mode
		 */
		AC108_DEBUG("AC108 work as Slave mode, don't need to config BCLK_DIV\n\n");
		return 0;
	}

	//default PCM mode
	bclk_div = div / ac108->lrck_period;
	//bclk_div = div/(2*AC108_LRCK_PERIOD);	//I2S/LJ/RJ mode

	for (i = 0; i < ARRAY_SIZE(ac108_bclk_div); i++) {
		if (ac108_bclk_div[i].real_val == bclk_div) {
			bclk_div_reg_val = ac108_bclk_div[i].reg_val;
			AC108_DEBUG("AC108 set BCLK_DIV_[%u]\n\n", bclk_div);
			break;
		}
	}

	if (i == ARRAY_SIZE(ac108_bclk_div)) {
		pr_err("AC108 don't support BCLK_DIV_[%u]\n\n", bclk_div);
		return -EINVAL;
	}

	//AC108 set BCLK DIV
	ac108_multi_chips_update_bits(I2S_BCLK_CTRL, 0xf << BCLKDIV,
				bclk_div_reg_val << BCLKDIV);
	return 0;
}

static int ac108_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	u8 i = 0;
	u8 tx_offset = 0;
	u8 i2s_mode = 0;
	u8 lrck_polarity = 0;
	u8 brck_polarity = 0;
	struct ac108_priv *ac108 = dev_get_drvdata(dai->dev);

	//AC108 config Master/Slave mode
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	//AC108 Master
		AC108_DEBUG("AC108 set to work as Master\n");
		//BCLK & LRCK output
		ac108_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN,
				0x3 << LRCK_IOEN, ac108->i2c);
	break;
	case SND_SOC_DAIFMT_CBS_CFS:	//AC108 Slave
		AC108_DEBUG("AC108 set to work as Slave\n");
		//BCLK & LRCK input
		ac108_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN,
				0x0<<LRCK_IOEN, ac108->i2c);
	break;
	default:
		pr_err("AC108 Master/Slave mode config error:%u\n\n",
			(fmt & SND_SOC_DAIFMT_MASTER_MASK) >> 12);
	return -EINVAL;
	}

	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++) {
		/*
		 * multi_chips: only one chip set as Master,
		 * and the others also need to set as Slave
		 */
		if (i2c_clt[i] == ac108->i2c)
			continue;
		ac108_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN,
				0x0 << LRCK_IOEN, i2c_clt[i]);
	}

	//AC108 config I2S/LJ/RJ/PCM format
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		AC108_DEBUG("AC108 config I2S format\n");
		i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 1;
	break;
	case SND_SOC_DAIFMT_RIGHT_J:
		AC108_DEBUG("AC108 config RIGHT-JUSTIFIED format\n");
		i2s_mode = RIGHT_JUSTIFIED_FORMAT;
		tx_offset = 0;
	break;
	case SND_SOC_DAIFMT_LEFT_J:
		AC108_DEBUG("AC108 config LEFT-JUSTIFIED format\n");
		i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 0;
	break;
	case SND_SOC_DAIFMT_DSP_A:
		AC108_DEBUG("AC108 config PCM-A format\n");
		i2s_mode = PCM_FORMAT;
		tx_offset = 1;
	break;
	case SND_SOC_DAIFMT_DSP_B:
		AC108_DEBUG("AC108 config PCM-B format\n");
		i2s_mode = PCM_FORMAT;
		tx_offset = 0;
	break;
	default:
		pr_err("AC108 I2S format config error:%u\n\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
	return -EINVAL;
	}
	ac108_multi_chips_update_bits(I2S_FMT_CTRL1,
		0x3 << MODE_SEL | 0x1 << TX2_OFFSET | 0x1 << TX1_OFFSET,
		i2s_mode<<MODE_SEL | tx_offset<<TX2_OFFSET | tx_offset << TX1_OFFSET);

	//AC108 config BCLK&LRCK polarity
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		AC108_DEBUG("AC108 config BCLK&LRCK polarity: BCLK_normal,LRCK_normal\n");
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
	break;
	case SND_SOC_DAIFMT_NB_IF:
		AC108_DEBUG("AC108 config BCLK&LRCK polarity: BCLK_normal,LRCK_invert\n");
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
	break;
	case SND_SOC_DAIFMT_IB_NF:
		AC108_DEBUG("AC108 config BCLK&LRCK polarity: BCLK_invert,LRCK_normal\n");
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
	break;
	case SND_SOC_DAIFMT_IB_IF:
		AC108_DEBUG("AC108 config BCLK&LRCK polarity: BCLK_invert,LRCK_invert\n");
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
		break;
	default:
		pr_err("AC108 config BCLK/LRCLK polarity error:%u\n\n",
			(fmt & SND_SOC_DAIFMT_INV_MASK) >> 8);
	return -EINVAL;
	}
	ac108_multi_chips_update_bits(I2S_BCLK_CTRL,  0x1<<BCLK_POLARITY,
				brck_polarity << BCLK_POLARITY);
	ac108_multi_chips_update_bits(I2S_LRCK_CTRL1, 0x1<<LRCK_POLARITY,
				lrck_polarity << LRCK_POLARITY);

	return 0;
}

static int ac108_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	u8 reg_val = 0;
	u16 i = 0;
	u16 channels = 0;
	u16 channels_en = 0;
	u16 sample_resolution = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		AC108_DEBUG("AC108 not need playback.\n");
		return 0;
	}

	//FIXME:AC108 hw init
	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++)
		ac108_hw_init(i2c_clt[i]);

	//AC108 set sample rate
	for (i = 0; i < ARRAY_SIZE(ac108_sample_rate); i++) {
		if (ac108_sample_rate[i].real_val ==
			params_rate(params) / (AC108_ENCODING_EN ? AC108_ENCODING_CH_NUMS / 2 : 1)) {
			ac108_multi_chips_update_bits(ADC_SPRC,
				0xf << ADC_FS_I2S1,
				ac108_sample_rate[i].reg_val << ADC_FS_I2S1);
			break;
		}
	}

	//AC108 set channels
#if !AC108_SDO2_EN
	channels = params_channels(params) *
			(AC108_ENCODING_EN ? AC108_ENCODING_CH_NUMS/2 : 1);
	for (i = 0; i < (channels + 3) / 4; i++) {
		channels_en = (channels >= 4 * (i + 1)) ? 0x000f << (4 * i) :
				((1 << (channels % 4)) - 1) << (4 * i);
		ac108_write(I2S_TX1_CTRL1, channels - 1, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL2, (u8)channels_en, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL3, channels_en >> 8, i2c_clt[i]);
	}

	for (; i < ac108_pub_cfg.ac108_nums; i++) {
		ac108_write(I2S_TX1_CTRL1, 0, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL2, 0, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL3, 0, i2c_clt[i]);
	}
#else
	channels = params_channels(params);
	for (i = 0; i < (channels + 3) / 4; i++) {
		//(2 >= 4*(i+1)) ? 0x000f<<(4*i) : ((1<<(2%4))-1)<<(4*i);
		channels_en = 0x03;
		ac108_write(I2S_TX1_CTRL1, 2 - 1, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL2, (u8)channels_en, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL3, channels_en >> 8, i2c_clt[i]);
	}
	for (; i < ac108_pub_cfg.ac108_nums; i++) {
		ac108_write(I2S_TX1_CTRL1, 0, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL2, 0, i2c_clt[i]);
		ac108_write(I2S_TX1_CTRL3, 0, i2c_clt[i]);
	}

	for (i = 0; i < (channels + 3) / 4; i++) {
		//(2 >= 4*(i+1)) ? 0x000f<<(4*i) : ((1<<(2%4))-1)<<(4*i);
		channels_en = 0x03;
		ac108_write(I2S_TX2_CTRL1, 2 - 1, i2c_clt[i]);
		ac108_write(I2S_TX2_CTRL2, (u8)channels_en, i2c_clt[i]);
		ac108_write(I2S_TX2_CTRL3, channels_en >> 8, i2c_clt[i]);
	}
	for (; i < ac108_pub_cfg.ac108_nums; i++) {
		ac108_write(I2S_TX2_CTRL1, 0, i2c_clt[i]);
		ac108_write(I2S_TX2_CTRL2, 0, i2c_clt[i]);
		ac108_write(I2S_TX2_CTRL3, 0, i2c_clt[i]);
	}
#endif

	//AC108 set sample resorution
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		sample_resolution = 8;
	break;
	case SNDRV_PCM_FORMAT_S16_LE:
		sample_resolution = 16;
	break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		sample_resolution = 20;
	break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sample_resolution = 24;
	break;
	case SNDRV_PCM_FORMAT_S32_LE:
		sample_resolution = 32;
	break;
	default:
		dev_err(dai->dev, "AC108 don't supported the sample resolution: %u\n",
			params_format(params));
	return -EINVAL;
	}

#if 0
	//AC108_ENCODING_EN
	//TX Encoding mode, SR add 4bits to mark channel number
	if ((sample_resolution <= 24) && (sample_resolution != 16))
		sample_resolution += 4;
#endif
	for (i = 0; i < ARRAY_SIZE(ac108_sample_resolution); i++) {
		if (ac108_sample_resolution[i].real_val == sample_resolution) {
			ac108_multi_chips_update_bits(I2S_FMT_CTRL2,
				0x7 << SAMPLE_RESOLUTION,
				ac108_sample_resolution[i].reg_val << SAMPLE_RESOLUTION);
			break;
		}
	}

	ac108_read(SYSCLK_CTRL, &reg_val, i2c_clt[0]);
	if (reg_val & 0x80) {
		//PLLCLK Enable
		//PLL Common voltage Enable, PLL Enable
		ac108_multi_chips_update_bits(PLL_CTRL1,
				0x1 << PLL_EN | 0x1<<PLL_COM_EN,
				0x1 << PLL_EN | 0x1<<PLL_COM_EN);
		if (!(reg_val & 0x08)) {
			//SYSCLK select MCLK
			//GPIO4 output Clock 24MHz from DPLL
			ac108_multi_chips_update_bits(GPIO_CFG2,
				0xf << GPIO4_SELECT, 0x9 << GPIO4_SELECT);
		}
	}

	//AC108 TX enable, Globle enable
	ac108_multi_chips_update_bits(I2S_CTRL, 0x1 << TXEN | 0x1 << GEN,
				0x1 << TXEN | 0x1 << GEN);

	return 0;
}

static int ac108_hw_free(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		AC108_DEBUG("AC108 not need playback.\n");
		return 0;
	}

	//AC108 I2S Globle disable
	ac108_multi_chips_update_bits(I2S_CTRL, 0x1 << GEN, 0x0 << GEN);

#ifdef AC108_IDLE_RESET_EN
	AC108_DEBUG("AC108 reset all register to their default value\n");
	ac108_multi_chips_write(CHIP_AUDIO_RST, 0x12);
#else
	//repair PLL version no sync problem && Encoding no DAT
	ac108_multi_chips_update_bits(PLL_CTRL1,
				0x1 << PLL_EN | 0x1 << PLL_COM_EN,
				0x0 << PLL_EN | 0x0 << PLL_COM_EN);
	ac108_multi_chips_update_bits(MOD_CLK_EN,
				0x1 << I2S | 0x1 << ADC_DIGITAL,
				0x0 << I2S | 0x0 << ADC_DIGITAL);
	ac108_multi_chips_update_bits(MOD_RST_CTRL,
				0x1 << I2S | 0x1 << ADC_DIGITAL,
				0x0 << I2S | 0x0 << ADC_DIGITAL);
#endif

	return 0;
}


/*** define  ac108  dai_ops  struct ***/
static const struct snd_soc_dai_ops ac108_dai_ops = {
	/*DAI clocking configuration*/
	.set_sysclk = ac108_set_sysclk,
	.set_pll = ac108_set_pll,
	.set_clkdiv = ac108_set_clkdiv,

	/*ALSA PCM audio operations*/
	.hw_params = ac108_hw_params,
	.hw_free = ac108_hw_free,

	/*DAI format configuration*/
	.set_fmt = ac108_set_fmt,
};

/*** define  ac108  dai_driver struct ***/
static struct snd_soc_dai_driver ac108_dai0 = {
	.name = "ac108-pcm0",
#ifdef AC108_PLAYBACK_DEBUG
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
#endif
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static struct snd_soc_dai_driver ac108_dai1 = {
	.name = "ac108-pcm1",
#ifdef AC108_PLAYBACK_DEBUG
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
#endif

	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static struct snd_soc_dai_driver ac108_dai2 = {
	.name = "ac108-pcm2",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static struct snd_soc_dai_driver ac108_dai3 = {
	.name = "ac108-pcm3",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = (AC108_NUM_MAX * 4),
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static struct snd_soc_dai_driver *ac108_dai[] = {
	&ac108_dai0,
	&ac108_dai1,
	&ac108_dai2,
	&ac108_dai3,
};

static int ac108_probe(struct snd_soc_codec *codec)
{
	struct ac108_priv *ac108 = dev_get_drvdata(codec->dev);

	ac108->codec = codec;

#ifdef AC108_DAPM_TEST_EN
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int ret = 0;

	/* Add virtual switch */
	ret = snd_soc_add_codec_controls(codec, ac108_controls,
					ARRAY_SIZE(ac108_controls));
	if (ret < 0) {
		dev_err(codec->dev, "Failed to register ac108 control, will continue without it.\n");
	}
	snd_soc_dapm_new_controls(dapm, ac108_dapm_widgets, ARRAY_SIZE(ac108_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, ac108_dapm_routes, ARRAY_SIZE(ac108_dapm_routes));
#endif

	return 0;
}

static int ac108_remove(struct snd_soc_codec *codec)
{
	return 0;
}

#ifdef CONFIG_PM
static int ac108_suspend(struct snd_soc_codec *codec)
{
#ifdef AC108_MATCH_DTS_EN
	struct ac108_priv *ac108 = dev_get_drvdata(codec->dev);

	/* FIXME: only support one power control */
	if (ac108->reset_gpio.used)
		gpio_set_value(ac108->reset_gpio.gpio, 0);
	if (ac108->power_gpio.used)
		gpio_set_value(ac108->power_gpio.gpio, 0);
	if (ac108->vol_supply.used)
		regulator_disable(ac108->vol_supply.regulator);
#endif

	return 0;
}

static int ac108_resume(struct snd_soc_codec *codec)
{
#ifndef AC108_IDLE_RESET_EN
	u8 i;
#endif

#ifdef AC108_MATCH_DTS_EN
	struct ac108_priv *ac108 = dev_get_drvdata(codec->dev);
	struct i2c_client *i2c = ac108->i2c;
	int ret;

	/* FIXME: only support one power control */
	if (ac108->vol_supply.used) {
		ret = regulator_enable(ac108->vol_supply.regulator);
		if (ret < 0) {
			dev_err(&i2c->dev, "fail to enable regulator %s!\n",
				ac108->vol_supply.name);
		}
	}

	if (ac108->power_gpio.used) {
		gpio_set_value(ac108->power_gpio.gpio, 1);
		msleep(20);
	}

	if (ac108->reset_gpio.used) {
		gpio_set_value(ac108->reset_gpio.gpio, 1);
		msleep(20);
	}
#endif

#ifndef AC108_IDLE_RESET_EN
	for (i = 0; i < ac108_pub_cfg.ac108_nums; i++)
		ac108_hw_init(i2c_clt[i]);
#endif

	return 0;
}

#else

#define ac108_suspend NULL
#define ac108_resume  NULL

#endif

#ifdef AC108_CODEC_RW_TEST_EN
static unsigned int ac108_codec_read(struct snd_soc_codec *codec,
				unsigned int reg)
{
	unsigned int reg_val;
	struct ac108_priv *ac108 = dev_get_drvdata(codec->dev);

	regmap_read(ac108->regmap, reg, &reg_val);
	return reg_val;
}

static int ac108_codec_write(struct snd_soc_codec *codec, unsigned int reg,
			unsigned int value)
{
	struct ac108_priv *ac108 = dev_get_drvdata(codec->dev);

	regmap_write(ac108->regmap, reg & 0xFF, value & 0xFF);
	return 0;
}
#endif

/*** define  ac108  codec_driver struct ***/
static const struct snd_soc_codec_driver ac108_soc_codec_driver = {
	.probe = ac108_probe,
	.remove = ac108_remove,
	.suspend = ac108_suspend,
	.resume = ac108_resume,
#ifdef AC108_CODEC_RW_TEST_EN
	.read = ac108_codec_read,
	.write = ac108_codec_write,
#endif
};


static ssize_t ac108_reg_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct ac108_priv *ac108 = dev_get_drvdata(dev);
	int ret = 0;
	unsigned int input_reg_offset = 0;
	unsigned int input_val = 0;
	unsigned int reg_val_read = 0;
	unsigned int rw_flag = 0;

	ret = sscanf(buf, "%d,0x%x,0x%x", &rw_flag, &input_reg_offset, &input_val);

	if (ret == 3) {
		if (!(rw_flag == 1 || rw_flag == 0)) {
			dev_err(dev, "rw_flag should be 0(read) or 1(write).\n");
			return count;
		}
		input_reg_offset &= 0xFF;
		input_val &= 0xFF;
		if ((input_reg_offset > AC108_REG_MAX) ||
			((input_reg_offset + input_val) > AC108_REG_MAX)) {
				dev_err(dev, "reg_dump addr[0x%02x] + count[0x%02x] > AC108_REG_MAX.\n",
					ac108->reg_dump_offset,
					ac108->reg_dump_count);
				return count;
			}
		if (input_reg_offset > AC108_REG_MAX) {
			pr_err("the reg offset is invalid! [0x0 - 0x%x]\n",
				AC108_REG_MAX);
			return count;
		}
		if (rw_flag) {
			regmap_write(ac108->regmap, input_reg_offset, input_val);
			regmap_read(ac108->regmap, input_reg_offset, &reg_val_read);
			pr_err("\n Reg[0x%x] : 0x%x\n", input_reg_offset, reg_val_read);
			return count;
		} else {
			ac108->reg_dump_offset = input_reg_offset;
			ac108->reg_dump_count = input_val;
		}
	} else {
		pr_err("ret:%d, The num of params invalid!\n", ret);
		pr_err("\nExample(reg range: 0x0 - 0x%x):\n", AC108_REG_MAX);
		pr_err("\nRead reg start[0x04]; reg count:0x10:\n");
		pr_err("      echo 0,0x04,0x10 > ac108_reg\n");
		pr_err("Write reg[0x04]=0x10\n");
		pr_err("      echo 1,0x04,0x10 > ac108_reg\n");
	}

	return count;
}

static ssize_t ac108_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	unsigned int i = 0;
	unsigned int reg_val_read = 0;
	struct ac108_priv *ac108 = dev_get_drvdata(dev);

	count += snprintf(buf, PAGE_SIZE, "\n/*** AC108 driver version: V3.0 ***/\n\n");

	if (!ac108->reg_dump_count) {
		count += snprintf(buf + count, PAGE_SIZE - count,
			"echo flag(Hex),reg(Hex),val(Hex) > ac108\n");
		count += snprintf(buf + count, PAGE_SIZE - count,
			"eg: read start addres=0x06, count=0x10;\n");
		count += snprintf(buf + count, PAGE_SIZE - count,
			"[command] echo 0,0x6,0x10 > ac108\n");
		count += snprintf(buf + count, PAGE_SIZE - count,
			"eg: write start addres=0x90, value=0x3c;\n");
		count += snprintf(buf + count, PAGE_SIZE - count,
			"[command] echo 1,0x90,0x3c > ac108\n");
	} else {
		do {
			reg_val_read = 0;
			if (count >= PAGE_SIZE) {
				dev_err(dev, "char count:%ld >= PAGE_SIZE.\n", (long)count);
				return count;
			}
			regmap_read(ac108->regmap, ac108->reg_dump_offset + i,
					&reg_val_read);
			count += snprintf(buf + count, PAGE_SIZE - count,
					"[0x%02x]: 0x%02x;  ",
					ac108->reg_dump_offset + i,
					reg_val_read);
			i++;
			if ((i == ac108->reg_dump_count) || (i % 4 == 0)) {
				if (count >= PAGE_SIZE) {
					dev_err(dev, "char count:%ld >= PAGE_SIZE.\n", (long)count);
					return count;
				}
				count += snprintf(buf + count, PAGE_SIZE - count, "\n");
			}
		} while (i < ac108->reg_dump_count);
	}
	return count;
}

static DEVICE_ATTR(ac108_reg, 0644, ac108_reg_show, ac108_reg_store);

static struct attribute *ac108_debug_attrs[] = {
	&dev_attr_ac108_reg.attr,
	NULL,
};

static struct attribute_group ac108_debug_attr_group = {
	.name	= "ac108_debug",
	.attrs	= ac108_debug_attrs,
};

static int ac108_i2c_probe(struct i2c_client *i2c,
			const struct i2c_device_id *i2c_id)
{
	struct ac108_priv *ac108 = NULL;
	int ret = 0;
#ifdef AC108_MATCH_DTS_EN
	enum of_gpio_flags gpio_flags;
	unsigned int temp_val = 0;
	struct device_node *np = i2c->dev.of_node;
#endif

	ac108 = devm_kzalloc(&i2c->dev, sizeof(struct ac108_priv), GFP_KERNEL);
	if (IS_ERR_OR_NULL(ac108)) {
		dev_err(&i2c->dev, "Unable to allocate ac108 private data\n");
		return -ENOMEM;
	}

	ac108->i2c = i2c;
	dev_set_drvdata(&i2c->dev, ac108);

	ac108->regmap = devm_regmap_init_i2c(ac108->i2c, &ac108_regmap_config);
	if (IS_ERR_OR_NULL(ac108->regmap)) {
		dev_err(&i2c->dev, "Failed to set cache I/O.\n");
		goto err_devm_regmap_i2c;
	}

#ifdef AC108_MATCH_DTS_EN
	ret = of_property_read_u32(np, "regulator_used", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get regulator_used failed.\n");
		ac108->vol_supply.used = 0x0;
	} else
		ac108->vol_supply.used = temp_val;

	/* FIXME: only support one power */
	if (ac108->vol_supply.used) {
		ret = of_property_read_u32(np, "power_voltage",
						&ac108->vol_supply.voltage);
		if (ret < 0) {
			dev_err(&i2c->dev, "get ac108 regulator vol failed!\n");
			goto err_property_get_power_vol;
		}
		/* should fix the vol for the chip */
		if (ac108->vol_supply.voltage > 5000000)
			ac108->vol_supply.voltage = 5000000;
		if (ac108->vol_supply.voltage < 1800000)
			ac108->vol_supply.voltage = 1800000;
		dev_warn(&i2c->dev, "ac108 regulator vol is:%d\n",
			ac108->vol_supply.voltage);

		ret = of_property_read_string(np, AC108_REGULATOR_NAME,
				&ac108->vol_supply.name);
		if (ret < 0) {
			dev_err(&i2c->dev, "get ac108 regulator name failed\n");
			goto err_regulator_get_name;
		} else {
			ac108->vol_supply.regulator = regulator_get(NULL, ac108->vol_supply.name);
			if (IS_ERR_OR_NULL(ac108->vol_supply.regulator)) {
				dev_err(&i2c->dev, "get ac108 regulator failed!\n");
				goto err_regulator_get;
			}
			ret = regulator_set_voltage(ac108->vol_supply.regulator,
						ac108->vol_supply.voltage,
						ac108->vol_supply.voltage);
			if (ret < 0) {
				dev_err(&i2c->dev, "set ac108 voltage failed!\n");
				goto err_regulator_set_voltage;
			}
			ret = regulator_enable(ac108->vol_supply.regulator);
			if (ret < 0) {
				dev_err(&i2c->dev, "fail to enable regulator %s.\n",
					ac108->vol_supply.name);
				goto err_regulator_enable;
			}
		}
	}

	ret = of_property_read_u32(np, "power_gpio_used", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get power_gpio_used failed.\n");
		ac108->power_gpio.used = 0x0;
	} else
		ac108->power_gpio.used = temp_val;

	if (ac108->power_gpio.used) {
		ac108->power_gpio.gpio = of_get_named_gpio_flags(np,
					     "power-gpio", 0, &gpio_flags);
		if (!gpio_is_valid(ac108->power_gpio.gpio)) {
			dev_err(&i2c->dev, "power_gpio is invalid.\n");
			goto err_power_gpio_invalid;
		}
		ret = gpio_request(ac108->power_gpio.gpio, "ac108 power gpio");
		if (ret < 0) {
			dev_err(&i2c->dev, "power_gpio request failed.\n");
			goto err_power_gpio_request;
		}
		gpio_direction_output(ac108->power_gpio.gpio, 1);
		gpio_set_value(ac108->power_gpio.gpio, 1);
		msleep(20);
	}

	ret = of_property_read_u32(np, "reset_gpio_used", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get reset_gpio_used failed.\n");
		ac108->reset_gpio.used = 0x0;
	} else
		ac108->reset_gpio.used = temp_val;

	if (ac108->reset_gpio.used) {
		ac108->reset_gpio.gpio = of_get_named_gpio_flags(np,
					     "reset-gpio", 0, &gpio_flags);
		if (!gpio_is_valid(ac108->reset_gpio.gpio)) {
			dev_err(&i2c->dev, "reset_gpio is invalid.\n");
			goto err_reset_gpio_invalid;
		}
		ret = gpio_request(ac108->reset_gpio.gpio, "ac108 reset gpio");
		if (ret < 0) {
			dev_err(&i2c->dev, "reset_gpio request failed.\n");
			goto err_reset_gpio_request;
		}
		gpio_direction_output(ac108->reset_gpio.gpio, 1);
		gpio_set_value(ac108->reset_gpio.gpio, 1);
		msleep(20);
	}

	ret = of_property_read_u32(np, "twi_bus", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get twi_bus failed.\n");
		goto err_get_twi_bus;
	} else
		ac108->twi_bus = temp_val;

	ret = of_property_read_u32(np, "pga_gain", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get pga_gain failed.\n");
		ac108->pga_gain = AC108_PGA_GAIN;
	} else
		ac108->pga_gain = temp_val;

	ret = of_property_read_u32(np, "ref_pga_used", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get ref_pga_used failed.\n");
		ac108->ref_cfg.ref_pga.used = 0x0;
	} else
		ac108->ref_cfg.ref_pga.used = temp_val;

	if (ac108->ref_cfg.ref_pga.used) {
		ret = of_property_read_u32(np, "ref_pga_gain", &temp_val);
		if (ret < 0) {
			dev_err(&i2c->dev, "get ref_pga_gain failed.\n");
			ac108->ref_cfg.ref_pga.val = 0x08;
		} else
			ac108->ref_cfg.ref_pga.val = temp_val;

		ret = of_property_read_u32(np, "ref_channel", &temp_val);
		if (ret < 0) {
			dev_err(&i2c->dev, "get reference channel failed.\n");
			ac108->ref_cfg.ref_channel = 0x0;
		} else
			ac108->ref_cfg.ref_channel = temp_val;
	}

	ret = of_property_read_u32(np, "lrck_period", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get lrck_period failed.\n");
		ac108->lrck_period = AC108_LRCK_PERIOD;
	} else
		ac108->lrck_period = temp_val;

	ret = of_property_read_u32(np, "slot_width", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get slot_width failed.\n");
		ac108->slot_width = AC108_SLOT_WIDTH;
	} else
		ac108->slot_width = temp_val;

	ret = of_property_read_u32(np, "debug_mode", &temp_val);
	if (ret < 0) {
		dev_err(&i2c->dev, "get debug_mode failed.\n");
		ac108->debug_mode = AC108_ADC_PATTERN_SEL;
	} else
		ac108->debug_mode = temp_val;
#else
	ac108->lrck_period = AC108_LRCK_PERIOD;
	ac108->slot_width = AC108_SLOT_WIDTH;
	ac108->debug_mode = AC108_ADC_PATTERN_SEL;
#endif

	ac108_pub_cfg.ac108_nums++;

	if (i2c_id->driver_data < AC108_NUM_MAX) {
		i2c_clt[i2c_id->driver_data] = i2c;
		ret = snd_soc_register_codec(&i2c->dev, &ac108_soc_codec_driver,
				ac108_dai[i2c_id->driver_data], 1);
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to register ac108 codec: %d\n", ret);
			goto err_register_codec;
		}
#ifdef AC108_POWERON_RESET_EN
		ac108_write(CHIP_AUDIO_RST, 0x12, i2c);
#endif
#ifndef AC108_IDLE_RESET_EN
		ac108_hw_init(i2c);
#endif
	} else {
		pr_err("The wrong i2c_id number :%d\n",
			(int)(i2c_id->driver_data));
		goto err_i2c_driver_data;
	}
	ret = sysfs_create_group(&i2c->dev.kobj, &ac108_debug_attr_group);
	if (ret < 0) {
		pr_err("failed to create attr group\n");
		goto err_sysfs_create_group;
	}

	dev_warn(&i2c->dev, "i2c probe succeed.\n");

	return 0;

err_sysfs_create_group:
	snd_soc_unregister_codec(&i2c->dev);
err_register_codec:
err_i2c_driver_data:
#ifdef AC108_MATCH_DTS_EN
err_get_twi_bus:
	if (ac108->reset_gpio.used)
		gpio_free(ac108->reset_gpio.gpio);
err_reset_gpio_request:
err_reset_gpio_invalid:
	if (ac108->power_gpio.used)
		gpio_free(ac108->power_gpio.gpio);
err_power_gpio_request:
err_power_gpio_invalid:
	if (ac108->vol_supply.used)
		regulator_disable(ac108->vol_supply.regulator);
err_regulator_enable:
err_regulator_set_voltage:
	if (ac108->vol_supply.used)
		regulator_put(ac108->vol_supply.regulator);
err_regulator_get:
err_regulator_get_name:
err_property_get_power_vol:
#endif
err_devm_regmap_i2c:
	devm_kfree(&i2c->dev, ac108);
	return ret;
}

static int ac108_i2c_remove(struct i2c_client *i2c)
{
	struct ac108_priv *ac108 = dev_get_drvdata(&i2c->dev);
	unsigned int i = 0;

	sysfs_remove_group(&i2c->dev.kobj, &ac108_debug_attr_group);
	snd_soc_unregister_codec(&i2c->dev);

	for (i = 0; i < AC108_NUM_MAX; i++) {
		if (i2c_clt[i] == i2c)
			i2c_clt[i] = NULL;
	}
	ac108_pub_cfg.ac108_nums--;
#ifdef AC108_MATCH_DTS_EN
	if (ac108->reset_gpio.used)
		gpio_free(ac108->reset_gpio.gpio);
	if (ac108->power_gpio.used)
		gpio_free(ac108->power_gpio.gpio);
	if (ac108->vol_supply.used)
		regulator_disable(ac108->vol_supply.regulator);
	if (ac108->vol_supply.used)
		regulator_put(ac108->vol_supply.regulator);
#endif
	devm_kfree(&i2c->dev, ac108);
	return 0;
}

#ifndef AC108_MATCH_DTS_EN
//I2C devices register method_3: i2c_detect
static int ac108_i2c_detect(struct i2c_client *client,
			struct i2c_board_info *info)
{
	u8 ac108_chip_id;
	struct i2c_adapter *adapter = client->adapter;

	ac108_read(CHIP_AUDIO_RST, &ac108_chip_id, client);
	pr_alert("\nAC108_Chip_ID on I2C-%d:0x%02X\n",
			adapter->nr, ac108_chip_id);

	if (ac108_chip_id == 0x4A) {
		if (client->addr == 0x3b) {
			strlcpy(info->type, "MicArray_0", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x35) {
			strlcpy(info->type, "MicArray_1", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x3c) {
			strlcpy(info->type, "MicArray_2", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x36) {
			strlcpy(info->type, "MicArray_3", I2C_NAME_SIZE);
			return 0;
		}
	}

	return -ENODEV;
}

//I2C devices address used in register method_3
static const unsigned short ac108_i2c_addr[] = {
	0x3b,
	0x35,
	0x3c,
	0x36,
	I2C_CLIENT_END,
};
#endif

//I2C devices register method_1: i2c_board_info (i2c_register_board_info)
//I2C devices register method_2: device tree source (in .dts file)
static const struct i2c_board_info ac108_i2c_board_info[] = {
	{I2C_BOARD_INFO("MicArray_0", 0x3b),},//ac108_0
	{I2C_BOARD_INFO("MicArray_1", 0x35),},//ac108_1
	{I2C_BOARD_INFO("MicArray_2", 0x3c),},//ac108_2
	{I2C_BOARD_INFO("MicArray_3", 0x36),},//ac108_3
};

//I2C driver and devices match method_1: i2c_device_id
static const struct i2c_device_id ac108_i2c_id[] = {
	{ "MicArray_0", 0 },//ac108_0
	{ "MicArray_1", 1 },//ac108_1
	{ "MicArray_2", 2 },//ac108_2
	{ "MicArray_3", 3 },//ac108_3
	{},
};
MODULE_DEVICE_TABLE(i2c, ac108_i2c_id);

#ifdef AC108_MATCH_DTS_EN
//I2C driver and devices match method_2: of_device_id (devices tree)
static const struct of_device_id ac108_dt_ids[] = {
	//ac108_0
	{ .compatible = "Allwinner,MicArray_0", },
	//ac108_1
	{ .compatible = "Allwinner,MicArray_1", },
	//ac108_2
	{ .compatible = "Allwinner,MicArray_2", },
	//ac108_3
	{ .compatible = "Allwinner,MicArray_3", },
	{},
};
MODULE_DEVICE_TABLE(of, ac108_dt_ids);
#endif

static struct i2c_driver ac108_i2c_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "ac108",
		.owner = THIS_MODULE,
#ifdef AC108_MATCH_DTS_EN
		.of_match_table = of_match_ptr(ac108_dt_ids),
#endif
	},
	.probe = ac108_i2c_probe,
	.remove = ac108_i2c_remove,
	.id_table = ac108_i2c_id,
#ifndef AC108_MATCH_DTS_EN
	.address_list = ac108_i2c_addr,
	.detect = ac108_i2c_detect,
#endif
};
module_i2c_driver(ac108_i2c_driver);

MODULE_DESCRIPTION("ASoC ac108 codec driver");
MODULE_AUTHOR("panjunwen");
MODULE_LICENSE("GPL");
