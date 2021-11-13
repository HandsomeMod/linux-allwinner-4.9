/*
 * ac102.h --  ac102 ALSA Soc Audio driver
 *
 * Version: 1.0
 *
 * Author: panjunwen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _AC102_H
#define _AC102_H


/*** AC102 Codec Register Define***/

/*Chip Reset*/
#define CHIP_AUDIO_RST		0x00

/*Power Control*/
#define PWR_CTRL1			0x01
#define PWR_CTRL2			0x02

/*System Function Control*/
#define SYS_FUNC_CTRL 		0x03

/*System Clock Control*/
#define ADC_CLK_SET			0x04
#define DAC_CLK_SET			0x05
#define SYS_CLK_ENA			0x06

/*I2S Common Control*/
#define I2S_CTRL			0x07
#define I2S_BCLK_CTRL		0x08
#define I2S_LRCK_CTRL1		0x09
#define I2S_LRCK_CTRL2		0x0A
#define I2S_FMT_CTRL1		0x0B
#define I2S_FMT_CTRL2		0x0C
#define I2S_FMT_CTRL3		0x0D
#define I2S_SLOT_CTRL		0x0E

/*I2S TX Control*/
#define I2S_TX_CTRL			0x0F
#define I2S_TX_CHMP_CTRL	0x11
#define I2S_TX_MIX_SRC		0x13

/*I2S RX Control*/
#define I2S_RX_CHMP_CTRL	0x16
#define I2S_RX_MIX_SRC		0x18

/*ADC Digital Control*/
#define ADC_DIG_CTRL		0x19
#define ADC_DVC				0x1A

/*DAC Digital Control*/
#define DAC_DIG_CTRL		0x1B
#define DAC_DVC				0x1C
#define DAC_MIX_SRC			0x1D

/*Digital Pad Drive Control*/
#define DIG_PADDRV_CTRL		0x1F

/*ADC Analog Control*/
#define ADC_ANA_CTRL1		0x20
#define ADC_ANA_CTRL2		0x21
#define ADC_ANA_CTRL3		0x22
#define ADC_ANA_CTRL4		0x23
#define ADC_ANA_CTRL5		0x24

/*DAC Analog Control*/
#define DAC_ANA_CTRL1		0x25
#define DAC_ANA_CTRL2		0x26
#define DAC_ANA_CTRL3		0x27
#define DAC_ANA_CTRL4		0x28

/*ADC AGC Control*/
#define AGC_STA				0x30
#define AGC_CTRL			0x31
#define AGC_DEBT			0x32
#define AGC_TGLVL			0x33
#define AGC_MAXG			0x34
#define AGC_AVGC1			0x35
#define AGC_AVGC2			0x36
#define AGC_AVGC3			0x37
#define AGC_AVGC4			0x38
#define AGC_DECAYT1			0x39
#define AGC_DECAYT2			0x3A
#define AGC_ATTACKT1		0x3B
#define AGC_ATTACKT2		0x3C
#define AGC_NTH				0x3D
#define AGC_NAVGC1			0x3E
#define AGC_NAVGC2			0x3F
#define AGC_NAVGC3			0x40
#define AGC_NAVGC4			0x41
#define HPF_COEF1			0x42
#define HPF_COEF2			0x43
#define HPF_COEF3			0x44
#define HPF_COEF4			0x45
#define AGC_OPT				0x46

/*DAC EQ Control*/
#define EQ_CTRL				0x4F

/*EQ Band1 Coef Control*/
#define EQ1_B0_H			0x50
#define EQ1_B0_M			0x51
#define EQ1_B0_L			0x52
#define EQ1_B1_H			0x53
#define EQ1_B1_M			0x54
#define EQ1_B1_L			0x55
#define EQ1_B2_H			0x56
#define EQ1_B2_M			0x57
#define EQ1_B2_L			0x58
#define EQ1_A1_H			0x59
#define EQ1_A1_M			0x5A
#define EQ1_A1_L			0x5B
#define EQ1_A2_H			0x5C
#define EQ1_A2_M			0x5D
#define EQ1_A2_L			0x5E

/*EQ Band2 Coef Control*/
#define EQ2_B0_H			0x60
#define EQ2_B0_M			0x61
#define EQ2_B0_L			0x62
#define EQ2_B1_H			0x63
#define EQ2_B1_M			0x64
#define EQ2_B1_L			0x65
#define EQ2_B2_H			0x66
#define EQ2_B2_M			0x67
#define EQ2_B2_L			0x68
#define EQ2_A1_H			0x69
#define EQ2_A1_M			0x6A
#define EQ2_A1_L			0x6B
#define EQ2_A2_H			0x6C
#define EQ2_A2_M			0x6D
#define EQ2_A2_L			0x6E

/*EQ Band3 Coef Control*/
#define EQ3_B0_H			0x70
#define EQ3_B0_M			0x71
#define EQ3_B0_L			0x72
#define EQ3_B1_H			0x73
#define EQ3_B1_M			0x74
#define EQ3_B1_L			0x75
#define EQ3_B2_H			0x76
#define EQ3_B2_M			0x77
#define EQ3_B2_L			0x78
#define EQ3_A1_H			0x79
#define EQ3_A1_M			0x7A
#define EQ3_A1_L			0x7B
#define EQ3_A2_H			0x7C
#define EQ3_A2_M			0x7D
#define EQ3_A2_L			0x7E



/*** AC102 Codec Register Bit Define***/

/*PWR_CTRL1*/
#define MBIAS_VCTRL			0
#define DLDO_VCTRL			2
#define ALDO_VCTRL			5

/*PWR_CTRL2*/
#define IREF_EN				0
#define VREF_EN				1
#define MBIAS_EN			2
#define DLDO_EN				3
#define ALDO_EN				4
#define IREF_CTRL			5


/*SYS_FUNC_CTRL*/
#define DAC_ANA_OUT_EN		0
#define MCLK_AUTO_DET_EN	1
#define AGC_GEN				2
#define NO_SW_MODE_STA		3
#define ADC_REC_FUNC_EN		4
#define DAC_PLAY_FUNC_EN	5
#define VREF_SPUP_STA		6


/*ADC_CLK_SET*/
#define NADC				0

/*DAC_CLK_SET*/
#define NDAC				0

/*SYS_CLK_ENA*/
#define ADC_DIG_CLK_EN		0
#define AGC_HPF_CLK_EN		1
#define DAC_DIG_CLK_EN		2
#define EQ_CLK_EN			3
#define I2S_CLK_EN			4
#define SYSCLK_EN			5


/*I2S_CTRL*/
#define BCLK_IOEN			7
#define LRCK_IOEN			6
#define SDO_EN				4
#define TXEN				2
#define RXEN				1
#define GEN					0

/*I2S_BCLK_CTRL*/
#define EDGE_TRANSFER		5
#define BCLK_POLARITY		4
#define BCLKDIV				0

/*I2S_LRCK_CTRL1*/
#define LRCK_POLARITY		4
#define LRCK_PERIODH		0

/*I2S_LRCK_CTRL2*/
#define LRCK_PERIODL		0

/*I2S_FMT_CTRL1*/
#define ENCD_SEL			6
#define MODE_SEL			4
#define OFFSET				2
#define TX_SLOT_HIZ			1
#define TX_STATE			0

/*I2S_FMT_CTRL2*/
#define SLOT_WIDTH_SEL		4
#define SAMPLE_RESOLUTION	0

/*I2S_FMT_CTRL3*/
#define TX_MLS				7
#define SEXT				5
#define OUT_MUTE			3
#define LRCK_WIDTH			2
#define TX_PDM				0

/*I2S_SLOT_CTRL*/
#define TX_CHSEL			0
#define RX_CHSEL			2


/*I2S_TX_CTRL*/
#define TX_CHEN				0

/*I2S_TX_CHMP_CTRL*/
#define TX_CH1_MAP			0
#define TX_CH2_MAP			1
#define TX_CH3_MAP			2
#define TX_CH4_MAP			3

/*I2S_TX_MIX_SRC*/
#define TX_MIXL_RECD_SRC	0
#define TX_MIXL_PLAY_SRC	1
#define TX_MIXR_RECD_SRC	2
#define TX_MIXR_RXM_SRC		3
#define TX_MIXL_RECD_GAIN	4
#define TX_MIXL_PLAY_GAIN	5
#define TX_MIXR_RECD_GAIN	6
#define TX_MIXR_RXM_GAIN	7


/*I2S_RX_CHMP_CTRL*/
#define RX_CH1_MAP			0
#define RX_CH2_MAP			2
#define RX_CH3_MAP			4
#define RX_CH4_MAP			6

/* I2S_RX_MIX_SRC*/
#define RX_MIX_RXL_SRC		0
#define RX_MIX_RXR_SRC		1
#define RX_MIX_RXL_GAIN		2
#define RX_MIX_RXR_GAIN		3


/* ADC_DIG_CTRL*/
#define ADC_DIG_EN			0
#define ADOUT_DLY_EN		1
#define ADOUT_DTS			2
#define ADC_PTN_SEL			4
#define DIG_MIC_OSR			6
#define DIG_MIC_EN			7


/*DAC_DIG_CTRL*/
#define DAC_DIG_EN			0
#define DAC_PTN_SEL			1
#define DITHER_SGM			3
#define DVC_ZCD_EN			6

/*DAC_MIX_SRC*/
#define DAC_MIX_RXM_SRC		0
#define DAC_MIX_RECD_SRC	1
#define DAC_MIX_RXM_GAIN	2
#define DAC_MIX_RECD_GAIN	3


/*DIG_PADDRV_CTRL*/
#define BCLK_DRV			0
#define LRCK_DRV			2
#define SDOUT_DRV			4


/*ADC_ANA_CTRL1*/
#define ADC_PGA_GEN			0
#define PGA_CTRL_RCM		1
#define PGA_GAIN_CTRL		3

/*ADC_ANA_CTRL2*/
#define DITHER_LEVEL_CTRL	0
#define DSM_VREFP_OUTCTRL	3
#define DSM_VREFP_LPMODE_EN 5
#define ADC_SEL_OUT_EDGE	6
#define DSM_DIS				7

/*ADC_ANA_CTRL3*/
#define PGA_OI_M_CTRL		0
#define PGA_OI_NM_CTRL		3
#define ADC_DEM_DIS			6
#define ADC_DITHER_EN		7

/*ADC_ANA_CTRL4*/
#define PGA_MAMP_IB_SEL		0
#define PGA_NMAMP_IB_SEL	3
#define PGA_IN_VCM_CTRL		6

/*ADC_ANA_CTRL5*/
#define DSM_OTA_CTRL		0
#define DSM_COMP_IB_SEL		2
#define DSM_OTA_IB_SEL		5


/*DAC_ANA_CTRL1*/
#define LOMUTE				2
#define RSWITCH				3
#define RAMP_EN				4
#define VRDA_EN				5
#define DAC_EN				7

/*DAC_ANA_CTRL2*/
#define LINE_OUT_AMP_GAIN	0
#define LINE_OUT_DIF_EN		4
#define LINE_OUT_EN			5

/*DAC_ANA_CTRL3*/
#define BCEN				1
#define LINEOPBC			2
#define DACOPBC				5

/*DAC_ANA_CTRL4*/
#define LOUT_VOL_ZCD_EN		0
#define LOUT_AUTO_ATT_EN	1
#define AUTO_ATT_STEP		2
#define RAMP_TIME			4


/*AGC_CTRL*/
#define AGC_HYS_SET			0
#define NOISE_DET_EN		2
#define HPF_EN				3
#define AGC_EN				4
#define AGC_NOISE_FLAG		5
#define AGC_SAT_FLAG		6

/*AGC_DEBT*/
#define SIGNAL_DEB_TIME		0
#define NOISE_DEB_TIME		4

/*AGC_TGLVL*/
#define AGC_TGLVL_SET		0

/*AGC_NTH*/
#define AGC_NOISE_THRES		0

/* AGC_OPT*/
#define AGC_OUT_NOISE_STA	0
#define AVER_FILT_COEF_SEL	1
#define AGC_GAIN_HYS_SET	2
#define ENERGY_DEFAULT_VAL	4


/*EQ_CTRL*/
#define EQ_EN				0



/*** Some Config Value ***/

/*I2S BCLK POLARITY Control*/
#define BCLK_NORMAL_DRIVE_N_SAMPLE_P	0
#define BCLK_INVERT_DRIVE_P_SAMPLE_N	1

/*I2S LRCK POLARITY Control*/
#define	LRCK_LEFT_LOW_RIGHT_HIGH		0
#define LRCK_LEFT_HIGH_RIGHT_LOW		1

/*I2S Format Selection*/
#define PCM_FORMAT						0
#define LEFT_JUSTIFIED_FORMAT			1
#define RIGHT_JUSTIFIED_FORMAT			2

/*ADC Digital Debug Control*/
#define ADC_PTN_NORMAL					0
#define ADC_PTN_0x5A5A5A				1
#define ADC_PTN_0x123456				2
#define ADC_PTN_RX_MIX_DATA				3

/*DAC Digital Debug Control*/
#define DAC_PTN_NORMAL					0
#define DAC_PTN_MINUS_6dB_SIN			1
#define DAC_PTN_MINUS_60dB_SIN			2
#define DAC_PTN_ZERO					3

/*ADC PGA GAIN Control*/
#define ADC_PGA_GAIN_0dB				0
#define ADC_PGA_GAIN_1dB				1
#define ADC_PGA_GAIN_2dB				2
#define ADC_PGA_GAIN_3dB				3
#define ADC_PGA_GAIN_4dB				4
#define ADC_PGA_GAIN_5dB				5
#define ADC_PGA_GAIN_6dB				6
#define ADC_PGA_GAIN_7dB				7
#define ADC_PGA_GAIN_8dB				8
#define ADC_PGA_GAIN_9dB				9
#define ADC_PGA_GAIN_10dB				10
#define ADC_PGA_GAIN_11dB				11
#define ADC_PGA_GAIN_12dB				12
#define ADC_PGA_GAIN_13dB				13
#define ADC_PGA_GAIN_14dB				14
#define ADC_PGA_GAIN_15dB				15
#define ADC_PGA_GAIN_16dB				16
#define ADC_PGA_GAIN_17dB				17
#define ADC_PGA_GAIN_18dB				18
#define ADC_PGA_GAIN_19dB				19
#define ADC_PGA_GAIN_20dB				20
#define ADC_PGA_GAIN_21dB				21
#define ADC_PGA_GAIN_22dB				22
#define ADC_PGA_GAIN_23dB				23
#define ADC_PGA_GAIN_24dB				24
#define ADC_PGA_GAIN_25dB				25
#define ADC_PGA_GAIN_26dB				26
#define ADC_PGA_GAIN_27dB				27
#define ADC_PGA_GAIN_28dB				28
#define ADC_PGA_GAIN_29dB				29
#define ADC_PGA_GAIN_30dB				30
#define ADC_PGA_GAIN_31dB				31

/*DAC LINEOUT GAIN Control*/
#define DAC_LINEOUT_GAIN_0dB			0
#define DAC_LINEOUT_GAIN_MINUS_3dB		1
#define DAC_LINEOUT_GAIN_MINUS_6dB		2
#define DAC_LINEOUT_GAIN_MINUS_9dB		3
#define DAC_LINEOUT_GAIN_MINUS_12dB		4
#define DAC_LINEOUT_GAIN_MINUS_15dB		5
#define DAC_LINEOUT_GAIN_MINUS_18dB		6
#define DAC_LINEOUT_GAIN_MINUS_21dB		7
#define DAC_LINEOUT_GAIN_MINUS_24dB		8
#define DAC_LINEOUT_GAIN_MINUS_27dB		9
#define DAC_LINEOUT_GAIN_MINUS_30dB		10
#define DAC_LINEOUT_GAIN_MINUS_33dB		11
#define DAC_LINEOUT_GAIN_MINUS_36dB		12
#define DAC_LINEOUT_GAIN_MINUS_39dB		13
#define DAC_LINEOUT_GAIN_MINUS_42dB		14
#define DAC_LINEOUT_GAIN_MINUS_45dB		15

/*ADC/DAC CLK DIV*/
#define ADC_DAC_CLK_DIV_1				0
#define ADC_DAC_CLK_DIV_2				1
#define ADC_DAC_CLK_DIV_3				2
#define ADC_DAC_CLK_DIV_4				3
#define ADC_DAC_CLK_DIV_6				4
#define ADC_DAC_CLK_DIV_8				5
#define ADC_DAC_CLK_DIV_12				6
#define ADC_DAC_CLK_DIV_16				7
#define ADC_DAC_CLK_DIV_24				8


#endif

