// SPDX-License-Identifier: GPL-2.0-or-later
/* sma1303.c -- sma1303 ALSA SoC Audio driver
 *
 * r013, 2020.05.06	- initial version  sma1303
 *
 * Copyright 2019 Silicon Mitus Corporation / Iron Device Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include "sma1303.h"

#define CHECK_PERIOD_TIME 1 /* sec per HZ */

#define PLL_MATCH(_input_clk_name, _output_clk_name, _input_clk,\
		_post_n, _n, _vco,  _p_cp)\
{\
	.input_clk_name		= _input_clk_name,\
	.output_clk_name	= _output_clk_name,\
	.input_clk		= _input_clk,\
	.post_n			= _post_n,\
	.n			= _n,\
	.vco			= _vco,\
	.p_cp		= _p_cp,\
}

enum sma1303_type {
	SMA1303,
};

/* PLL clock setting Table */
struct sma1303_pll_match {
	char *input_clk_name;
	char *output_clk_name;
	unsigned int input_clk;
	unsigned int post_n;
	unsigned int n;
	unsigned int vco;
	unsigned int p_cp;
};

struct sma1303_priv {
	enum sma1303_type devtype;
	struct attribute_group *attr_grp;
	struct kobject *kobj;
	struct regmap *regmap;
	struct sma1303_pll_match *pll_matches;
	int num_of_pll_matches;
	unsigned int sys_clk_id;
	unsigned int init_vol;
	unsigned int cur_vol;
	unsigned int tsdw_cnt;
	unsigned int bst_vol_lvl_status;
	unsigned int flt_vdd_gain_status;
	bool amp_power_status;
	bool force_amp_power_down;
	bool stereo_two_chip;
	bool impossible_bst_ctrl;
	bool fault_reg_flag;
	struct mutex lock;
	struct delayed_work check_fault_work;
	long check_fault_period;
	long check_fault_status;
	unsigned int format;
	struct device *dev;
	unsigned int rev_num;
	unsigned int last_over_temp;
	unsigned int last_ocp_val;
	unsigned int last_bclk;
};

static struct sma1303_pll_match sma1303_pll_matches[] = {
/* in_clk_name, out_clk_name, input_clk post_n, n, vco, p_cp */
PLL_MATCH("1.536MHz",  "24.576MHz", 1536000,  0x07, 0xE0, 0x8B, 0x03),
PLL_MATCH("3.072MHz",  "24.576MHz", 3072000,  0x07, 0x70, 0x8B, 0x03),
PLL_MATCH("6.144MHz",  "24.576MHz", 6144000,  0x07, 0x70, 0x8B, 0x07),
PLL_MATCH("12.288MHz", "24.576MHz", 12288000, 0x07, 0x70, 0x8B, 0x0B),
PLL_MATCH("19.2MHz",   "24.343MHz", 19200000, 0x07, 0x47, 0x8B, 0x0A),
PLL_MATCH("24.576MHz", "24.576MHz", 24576000, 0x07, 0x70, 0x8B, 0x0F),
};

static int sma1303_startup(struct snd_soc_component *);
static int sma1303_shutdown(struct snd_soc_component *);

/* Initial register value - {register, value} 2019.10.17 */
static const struct reg_default sma1303_reg_def[] = {
	{ 0x00, 0x80 }, /* 0x00 SystemCTRL  */
	{ 0x01, 0x00 }, /* 0x01 InputCTRL1  */
	{ 0x02, 0x00 }, /* 0x02 InputCTRL2  */
	{ 0x03, 0x11 }, /* 0x03 InputCTRL3  */
	{ 0x04, 0x17 }, /* 0x04 InputCTRL4  */
	{ 0x09, 0x00 }, /* 0x09 OutputCTRL  */
	{ 0x0A, 0x58 }, /* 0x0A SPK_VOL  */
	{ 0x0B, 0xD8 }, /* 0x0B BST_TEST  */
	{ 0x0C, 0x8C }, /* 0x0C BST_TEST1  */
	{ 0x0D, 0x07 }, /* 0x0D SPK_TEST  */
	{ 0x0E, 0xAF }, /* 0x0E MUTE_VOL_CTRL  */
	{ 0x10, 0x00 }, /* 0x10 SystemCTRL1  */
	{ 0x11, 0x00 }, /* 0x11 SystemCTRL2  */
	{ 0x12, 0x00 }, /* 0x12 SystemCTRL3  */
	{ 0x14, 0x60 }, /* 0x14 Modulator  */
	{ 0x15, 0x01 }, /* 0x15 BassSpk1  */
	{ 0x16, 0x0F }, /* 0x16 BassSpk2  */
	{ 0x17, 0x0F }, /* 0x17 BassSpk3  */
	{ 0x18, 0x0F }, /* 0x18 BassSpk4  */
	{ 0x19, 0x00 }, /* 0x19 BassSpk5  */
	{ 0x1A, 0x00 }, /* 0x1A BassSpk6  */
	{ 0x1B, 0x00 }, /* 0x1B BassSpk7  */
	{ 0x23, 0x19 }, /* 0x23 CompLim1  */
	{ 0x24, 0x00 }, /* 0x24 CompLim2  */
	{ 0x25, 0x00 }, /* 0x25 CompLim3  */
	{ 0x26, 0x04 }, /* 0x26 CompLim4  */
	{ 0x33, 0x00 }, /* 0x33 SDM_CTRL  */
	{ 0x34, 0x0C }, /* 0x34 OTP_DATA1  */
	{ 0x36, 0x92 }, /* 0x36 Protection  */
	{ 0x37, 0x3F }, /* 0x37 SlopeCTRL  */
	{ 0x38, 0x64 }, /* 0x38 OTP_TRM0 */
	{ 0x3B, 0x00 }, /* 0x3B Test1  */
	{ 0x3C, 0x00 }, /* 0x3C Test2  */
	{ 0x3D, 0x00 }, /* 0x3D Test3  */
	{ 0x3E, 0x03 }, /* 0x3E ATEST1  */
	{ 0x3F, 0x00 }, /* 0x3F ATEST2  */
	{ 0x8B, 0x07 }, /* 0x8B PLL_POST_N  */
	{ 0x8C, 0x70 }, /* 0x8C PLL_N  */
	{ 0x8D, 0x8B }, /* 0x8D PLL_A_SETTING  */
	{ 0x8E, 0x6F }, /* 0x8E PLL_CTRL  */
	{ 0x8F, 0x03 }, /* 0x8F PLL_P,CP  */
	{ 0x90, 0x31 }, /* 0x90 Postscaler  */
	{ 0x91, 0x42 }, /* 0x91 Class-G Control  */
	{ 0x92, 0x80 }, /* 0x92 FDPEC Control  */
	{ 0x94, 0x35 }, /* 0x94 Boost Control1  */
	{ 0x95, 0x44 }, /* 0x95 Boost Control2  */
	{ 0x96, 0x42 }, /* 0x96 Boost Control3  */
	{ 0x97, 0x9A }, /* 0x97 Boost Control4  */
	{ 0xA0, 0x00 }, /* 0xA0 PAD_CTRL0  */
	{ 0xA1, 0x3B }, /* 0xA1 PAD_CTRL1  */
	{ 0xA2, 0x48 }, /* 0xA2 TOP_MAN1  */
	{ 0xA3, 0x28 }, /* 0xA3 TOP_MAN2  */
	{ 0xA4, 0x40 }, /* 0xA4 TOP_MAN3  */
	{ 0xA5, 0x01 }, /* 0xA5 TDM1  */
	{ 0xA6, 0x41 }, /* 0xA6 TDM2  */
	{ 0xA7, 0x00 }, /* 0xA7 CLK_MON  */
	{ 0xFA, 0xE0 }, /* 0xFA Status1  */
	{ 0xFB, 0x06 }, /* 0xFB Status2  */
	{ 0xFF, 0x10 }, /* 0xFF Device Index  */
};

static bool sma1303_readable_register(struct device *dev, unsigned int reg)
{
	if (reg > SMA1303_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1303_00_SYSTEM_CTRL ... SMA1303_04_INPUT1_CTRL4:
	case SMA1303_09_OUTPUT_CTRL ... SMA1303_0E_MUTE_VOL_CTRL:
	case SMA1303_10_SYSTEM_CTRL1 ... SMA1303_12_SYSTEM_CTRL3:
	case SMA1303_14_MODULATOR ... SMA1303_1B_BASS_SPK7:
	case SMA1303_23_COMP_LIM1 ... SMA1303_26_COMP_LIM4:
	case SMA1303_33_SDM_CTRL ... SMA1303_34_OTP_DATA1:
	case SMA1303_36_PROTECTION  ... SMA1303_38_OTP_TRM0:
	case SMA1303_3B_TEST1  ... SMA1303_3F_ATEST2:
	case SMA1303_8B_PLL_POST_N ... SMA1303_92_FDPEC_CTRL:
	case SMA1303_94_BOOST_CTRL1 ... SMA1303_97_BOOST_CTRL4:
	case SMA1303_A0_PAD_CTRL0 ... SMA1303_A7_CLK_MON:
	case SMA1303_FA_STATUS1 ... SMA1303_FB_STATUS2:
	case SMA1303_FF_DEVICE_INDEX:
		return true;
	default:
		return false;
	}
}

static bool sma1303_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg > SMA1303_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1303_00_SYSTEM_CTRL ... SMA1303_04_INPUT1_CTRL4:
	case SMA1303_09_OUTPUT_CTRL ... SMA1303_0E_MUTE_VOL_CTRL:
	case SMA1303_10_SYSTEM_CTRL1 ... SMA1303_12_SYSTEM_CTRL3:
	case SMA1303_14_MODULATOR ... SMA1303_1B_BASS_SPK7:
	case SMA1303_23_COMP_LIM1 ... SMA1303_26_COMP_LIM4:
	case SMA1303_33_SDM_CTRL ... SMA1303_34_OTP_DATA1:
	case SMA1303_36_PROTECTION  ... SMA1303_38_OTP_TRM0:
	case SMA1303_3B_TEST1  ... SMA1303_3F_ATEST2:
	case SMA1303_8B_PLL_POST_N ... SMA1303_92_FDPEC_CTRL:
	case SMA1303_94_BOOST_CTRL1 ... SMA1303_97_BOOST_CTRL4:
	case SMA1303_A0_PAD_CTRL0 ... SMA1303_A7_CLK_MON:
		return true;
	default:
		return false;
	}
}

static bool sma1303_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SMA1303_FA_STATUS1 ... SMA1303_FB_STATUS2:
	case SMA1303_FF_DEVICE_INDEX:
		return true;
	default:
		return false;
	}
}

/* DB scale conversion of speaker volume */
static const DECLARE_TLV_DB_SCALE(sma1303_spk_tlv, -6000, 50, 0);

/* common bytes ext functions */
static int bytes_ext_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol, int reg)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	unsigned int i, reg_val;
	u8 *val;

	val = (u8 *)ucontrol->value.bytes.data;
	for (i = 0; i < params->max; i++) {
		regmap_read(sma1303->regmap, reg + i, &reg_val);
		if (sizeof(reg_val) > 2)
			reg_val = cpu_to_le32(reg_val);
		else
			reg_val = cpu_to_le16(reg_val);
		memcpy(val + i, &reg_val, sizeof(u8));
	}

	return 0;
}

static int bytes_ext_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol, int reg)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	void *data;
	u8 *val;
	int i, ret;

	data = kmemdup(ucontrol->value.bytes.data,
			params->max, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	val = (u8 *)data;
	for (i = 0; i < params->max; i++) {
		ret = regmap_write(sma1303->regmap, reg + i, *(val + i));
		if (ret) {
			dev_err(component->dev,
				"configuration fail, register: %x ret: %d\n",
				reg + i, ret);
			kfree(data);
			return ret;
		}
	}
	kfree(data);

	return 0;
}

static int power_up_down_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = sma1303->amp_power_status;

	return 0;
}

static int power_up_down_control_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	if (sel && !(sma1303->force_amp_power_down))
		sma1303_startup(component);
	else
		sma1303_shutdown(component);

	return 0;
}

static int power_down_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = sma1303->force_amp_power_down;

	return 0;
}

static int power_down_control_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	sma1303->force_amp_power_down = ucontrol->value.integer.value[0];

	if (sma1303->force_amp_power_down) {
		dev_info(component->dev, "%s\n", "Force AMP Power Down");
		sma1303_shutdown(component);
	}

	return 0;
}


/* InputCTRL1 [0x01] */
static const char * const sma1303_input_format_text[] = {
	"I2S", "LJ", "Reserved", "Reserved",
	"RJ_16", "RJ_18", "RJ_20", "RJ_24"};

static const struct soc_enum sma1303_input_format_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_input_format_text),
		sma1303_input_format_text);

static int sma1303_input_format_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_01_INPUT1_CTRL1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x70) >> 4);

	return 0;
}

static int sma1303_input_format_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_01_INPUT1_CTRL1, 0x70, (sel << 4));

	return 0;
}

/* InputCTRL2 [0x02] */
static const char * const sma1303_in_audio_mode_text[] = {
	"I2S mode", "PCM short", "PCM long", "Reserved"};

static const struct soc_enum sma1303_in_audio_mode_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_in_audio_mode_text),
		sma1303_in_audio_mode_text);

static int sma1303_in_audio_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_02_INPUT1_CTRL2, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_in_audio_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_02_INPUT1_CTRL2, 0xC0, (sel << 6));

	return 0;
}

/* InputCTRL3 [0x03] */
static const char * const sma1303_pcm_n_slot_text[] = {
	"Slot_1", "Slot_2", "Slot_3", "Slot_4", "Slot_5", "Slot_6",
	"Slot_7", "Slot_8", "Slot_9", "Slot_10", "Slot_11", "Slot_12",
	"Slot_13", "Slot_14", "Slot_15", "Slot_16"};

static const struct soc_enum sma1303_pcm_n_slot_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_pcm_n_slot_text),
		sma1303_pcm_n_slot_text);

static int sma1303_pcm_n_slot_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_03_INPUT1_CTRL3, &val);
	ucontrol->value.integer.value[0] = (val & 0x0F);

	return 0;
}

static int sma1303_pcm_n_slot_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_03_INPUT1_CTRL3, 0x0F, sel);

	return 0;
}

/* InputCTRL4 [0x04] */
static const char * const sma1303_pcm1_slot_text[] = {
	"Slot_1", "Slot_2", "Slot_3", "Slot_4", "Slot_5", "Slot_6",
	"Slot_7", "Slot_8", "Slot_9", "Slot_10", "Slot_11", "Slot_12",
	"Slot_13", "Slot_14", "Slot_15", "Slot_16"};

static const struct soc_enum sma1303_pcm1_slot_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_pcm1_slot_text),
		sma1303_pcm1_slot_text);

static int sma1303_pcm1_slot_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_04_INPUT1_CTRL4, &val);
	ucontrol->value.integer.value[0] = ((val & 0xF0) >> 4);

	return 0;
}

static int sma1303_pcm1_slot_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_04_INPUT1_CTRL4, 0xF0, (sel << 4));

	return 0;
}

static const char * const sma1303_pcm2_slot_text[] = {
	"Slot_1", "Slot_2", "Slot_3", "Slot_4", "Slot_5", "Slot_6",
	"Slot_7", "Slot_8", "Slot_9", "Slot_10", "Slot_11", "Slot_12",
	"Slot_13", "Slot_14", "Slot_15", "Slot_16"};

static const struct soc_enum sma1303_pcm2_slot_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_pcm2_slot_text),
		sma1303_pcm2_slot_text);

static int sma1303_pcm2_slot_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_04_INPUT1_CTRL4, &val);
	ucontrol->value.integer.value[0] = (val & 0x0F);

	return 0;
}

static int sma1303_pcm2_slot_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_04_INPUT1_CTRL4, 0x0F, sel);

	return 0;
}

/* output CTRL [0x09] */
static const char * const sma1303_port_config_text[] = {
	"IN_Port", "Reserved", "OUT_Port", "Reserved"};

static const struct soc_enum sma1303_port_config_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_port_config_text),
			sma1303_port_config_text);

static int sma1303_port_config_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_09_OUTPUT_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0x60) >> 5);

	return 0;
}

static int sma1303_port_config_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_09_OUTPUT_CTRL, 0x60, (sel << 5));

	return 0;
}


static const char * const sma1303_port_out_sel_text[] = {
	"Disable", "Format_C", "Mixer_out", "After_DSP",
	"Postscaler", "Reserved", "Reserved", "Reserved"};

static const struct soc_enum sma1303_port_out_sel_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_port_out_sel_text),
	sma1303_port_out_sel_text);

static int sma1303_port_out_sel_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_09_OUTPUT_CTRL, &val);
	ucontrol->value.integer.value[0] = (val & 0x07);

	return 0;
}

static int sma1303_port_out_sel_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_09_OUTPUT_CTRL, 0x07, sel);

	return 0;
}
/* BST OFF slope */
static const char * const sma1303_bst_off_slope_text[] = {
	"6.7ns", "4.8ns", "2.6ns", "1.2ns"};

static const struct soc_enum sma1303_bst_off_slope_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_bst_off_slope_text),
	sma1303_bst_off_slope_text);

static int sma1303_bst_off_slope_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_0B_BST_TEST, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_bst_off_slope_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_0B_BST_TEST, 0xC0, (sel << 6));

	return 0;
}

/* High side OCP level Control */
static const char * const sma1303_set_ocp_h_text[] = {
	"4.5", "3.2", "2.1", "0.9"};

static const struct soc_enum sma1303_set_ocp_h_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_set_ocp_h_text),
	sma1303_set_ocp_h_text);

static int sma1303_set_ocp_h_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_0C_BST_TEST1, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_set_ocp_h_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_0C_BST_TEST1, 0xC0, (sel << 6));

	return 0;
}

/* SPK Turn Off Slope Set */
static const char * const sma1303_spk_off_slope_text[] = {
	"00", "01", "10", "11"};

static const struct soc_enum sma1303_spk_off_slope_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_spk_off_slope_text),
	sma1303_spk_off_slope_text);

static int sma1303_spk_off_slope_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_0D_SPK_TEST, &val);
	ucontrol->value.integer.value[0] = (val & 0x03);

	return 0;
}

static int sma1303_spk_off_slope_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_0D_SPK_TEST, 0x03, sel);

	return 0;
}

/* Volume slope */
static const char * const sma1303_vol_slope_text[] = {
	"Off", "Slow(1sec)", "Medium(0.5sec)",
	"Fast(0.1sec)"};

static const struct soc_enum sma1303_vol_slope_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_vol_slope_text),
	sma1303_vol_slope_text);

static int sma1303_vol_slope_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_vol_slope_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_0E_MUTE_VOL_CTRL, 0xC0, (sel << 6));

	return 0;
}

/* Mute slope */
static const char * const sma1303_mute_slope_text[] = {
	"Off", "Slow(200ms)", "Medium(50ms)",
	"Fast(10ms)"};

static const struct soc_enum sma1303_mute_slope_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_mute_slope_text),
	sma1303_mute_slope_text);

static int sma1303_mute_slope_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0x30) >> 4);

	return 0;
}

static int sma1303_mute_slope_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_0E_MUTE_VOL_CTRL, 0x30, (sel << 4));

	return 0;
}

/* Speaker mode */
static const char * const sma1303_spkmode_text[] = {
	"Off", "Mono", "Reserved", "Reserved",
	"Stereo", "Reserved", "Reserved", "Reserved"};

static const struct soc_enum sma1303_spkmode_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_spkmode_text),
	sma1303_spkmode_text);

static int sma1303_spkmode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_10_SYSTEM_CTRL1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x1C) >> 2);

	return 0;
}

static int sma1303_spkmode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_10_SYSTEM_CTRL1, 0x1C, (sel << 2));

	if (sel == (SPK_MONO >> 2))
		sma1303->stereo_two_chip = false;
	else if (sel == (SPK_STEREO >> 2))
		sma1303->stereo_two_chip = true;

	return 0;
}

/* SystemCTRL3 Set */
static const char * const sma1303_input_gain_text[] = {
	"Gain_0dB", "Gain_M6dB", "Gain_M12dB", "Gain_MInf"};

static const struct soc_enum sma1303_input_gain_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_input_gain_text),
		sma1303_input_gain_text);

static int sma1303_input_gain_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_12_SYSTEM_CTRL3, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_input_gain_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_12_SYSTEM_CTRL3, 0xC0, (sel << 6));

	return 0;
}

static const char * const sma1303_input_r_gain_text[] = {
	"Gain_0dB", "Gain_M6dB", "Gain_M12dB", "Gain_MInf"};

static const struct soc_enum sma1303_input_r_gain_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_input_r_gain_text),
		sma1303_input_r_gain_text);

static int sma1303_input_r_gain_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_12_SYSTEM_CTRL3, &val);
	ucontrol->value.integer.value[0] = ((val & 0x30) >> 4);

	return 0;
}

static int sma1303_input_r_gain_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_12_SYSTEM_CTRL3, 0x30, (sel << 4));

	return 0;
}

/* Modulator Set */
static const char * const sma1303_spk_hysfb_text[] = {
	"f_625kHz", "f_414kHz", "f_297kHz", "f_226kHz"};

static const struct soc_enum sma1303_spk_hysfb_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_spk_hysfb_text), sma1303_spk_hysfb_text);

static int sma1303_spk_hysfb_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_14_MODULATOR, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_spk_hysfb_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_14_MODULATOR, 0xC0, (sel << 6));

	return 0;
}

static int spk_bdelay_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_14_MODULATOR);
}

static int spk_bdelay_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_14_MODULATOR);
}

/* bass boost speaker coeff */
static int bass_spk_coeff_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_15_BASS_SPK1);
}

static int bass_spk_coeff_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_15_BASS_SPK1);
}

/* DRC speaker coeff */
static int comp_lim_spk_coeff_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_23_COMP_LIM1);
}

static int comp_lim_spk_coeff_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_23_COMP_LIM1);
}

/* PWM LR Delay Set */
static const char * const sma1303_lr_delay_text[] = {
	"Delay_00", "Delay_01", "Delay_10", "Delay_11"};

static const struct soc_enum sma1303_lr_delay_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_lr_delay_text), sma1303_lr_delay_text);

static int sma1303_lr_delay_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_36_PROTECTION, &val);
	ucontrol->value.integer.value[0] = ((val & 0x60) >> 5);

	return 0;
}

static int sma1303_lr_delay_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_36_PROTECTION, 0x60, (sel << 5));

	return 0;
}

/* OTP MODE Set */
static const char * const sma1303_otp_mode_text[] = {
	"Disable", "I_L1_S_L2", "R_L1_S_L2", "S_L1_S_L2"};

static const struct soc_enum sma1303_otp_mode_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_otp_mode_text), sma1303_otp_mode_text);

static int sma1303_otp_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_36_PROTECTION, &val);
	ucontrol->value.integer.value[0] = (val & 0x03);

	return 0;
}

static int sma1303_otp_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap, SMA1303_36_PROTECTION, 0x03, sel);

	return 0;
}

/* Slope CTRL */
static int slope_ctrl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_37_SLOPE_CTRL);
}

static int slope_ctrl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_37_SLOPE_CTRL);
}

/* Test 1~3, ATEST 1~2 */
static int test_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_3B_TEST1);
}

static int test_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_3B_TEST1);
}

/* PLL Setting */
static int pll_setting_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_8B_PLL_POST_N);
}

static int pll_setting_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_8B_PLL_POST_N);
}

/* PLL Power Control */
static int pll_pd_ctrl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_8E_PLL_CTRL);
}

static int pll_pd_ctrl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_8E_PLL_CTRL);
}

static const char * const sma1303_trm_lvl_text[] = {
	"On", "Off"};

static const struct soc_enum sma1303_trm_lvl_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_lvl_text),
			sma1303_trm_lvl_text);

static int sma1303_trm_lvl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_8E_PLL_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0x10) >> 4);

	return 0;
}

static int sma1303_trm_lvl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];
	int device_info, otp_data;

	regmap_read(sma1303->regmap, SMA1303_34_OTP_DATA1, &otp_data);
	regmap_read(sma1303->regmap, SMA1303_FF_DEVICE_INDEX, &device_info);

	if (((device_info & 0x03) != REV_NUM_TV0) && (sel == 1))
		regmap_update_bits(sma1303->regmap,
			SMA1303_8E_PLL_CTRL, 0x10,
			(((otp_data & 0x20) >> 5) << 4));
	else
		regmap_update_bits(sma1303->regmap,
			SMA1303_8E_PLL_CTRL, 0x10, 0 << 4);

	return 0;
}

/* Post Gain Set */
static int postscaler_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_get(kcontrol, ucontrol, SMA1303_90_POSTSCALER);
}

static int postscaler_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return bytes_ext_put(kcontrol, ucontrol, SMA1303_90_POSTSCALER);
}

/* ClassG control Set */
static const char * const sma1303_attack_lvl_text[] = {
	"BST_ON", "LVL_0.0625FS", "LVL_0.125FS", "LVL_0.1875FS",
	"LVL_0.25FS", "LVL_0.3125FS", "LVL_0.375FS", "LVL_0.4375FS",
	"LVL_0.5FS", "LVL_0.5625FS", "LVL_0.625FS", "LVL_0.6875FS",
	"LVL_0.75FS", "LVL_0.8125FS", "LVL_0.875FS", "BST_OFF"};

static const struct soc_enum sma1303_attack_lvl_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_attack_lvl_text),
		sma1303_attack_lvl_text);

static int sma1303_attack_lvl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_91_CLASS_G_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0xF0) >> 4);

	return 0;
}

static int sma1303_attack_lvl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_91_CLASS_G_CTRL, 0xF0, (sel << 4));

	return 0;
}

static const char * const sma1303_release_time_text[] = {
	"Time_0ms", "Time_20ms", "Time_40ms", "Time_60ms",
	"Time_80ms", "Time_100ms", "Time_120ms", "Time_140ms",
	"Time_160ms", "Time_180ms", "Time_200ms", "Time_220ms",
	"Time_240ms", "Time_260ms", "Time_280ms", "Time_300ms"};

static const struct soc_enum sma1303_release_time_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_release_time_text),
		sma1303_release_time_text);

static int sma1303_release_time_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_91_CLASS_G_CTRL, &val);
	ucontrol->value.integer.value[0] = (val & 0x0F);

	return 0;
}

static int sma1303_release_time_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_91_CLASS_G_CTRL, 0x0F, sel);

	return 0;
}
/* FDPEC control Set */
static const char * const sma1303_flt_vdd_gain_text[] = {
	"VDD_2.4V", "VDD_2.45V", "VDD_2.5V", "VDD_2.55V",
	"VDD_2.6V", "VDD_2.65V", "VDD_2.7V", "VDD_2.75V",
	"VDD_2.8V", "VDD_2.85V", "VDD_2.9V", "VDD_2.95V",
	"VDD_3.0V", "VDD_3.05V", "VDD_3.1V", "VDD_3.15V"};

static const struct soc_enum sma1303_flt_vdd_gain_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_flt_vdd_gain_text),
		sma1303_flt_vdd_gain_text);

static int sma1303_flt_vdd_gain_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_92_FDPEC_CTRL, &val);
	ucontrol->value.integer.value[0] = ((val & 0xF0) >> 4);

	return 0;
}

static int sma1303_flt_vdd_gain_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	sma1303->flt_vdd_gain_status = sel;

	regmap_update_bits(sma1303->regmap,
		SMA1303_92_FDPEC_CTRL, 0xF0, (sel << 4));

	return 0;
}

/* Boost control1 Set */
static const char * const sma1303_trm_osc_text[] = {
	"f_1.4MHz", "f_1.5MHz", "f_1.8MHz", "f_2.0MHz",
	"f_2.2MHz", "f_2.5MHz", "f_2.7MHz", "f_3.1MHz"};

static const struct soc_enum sma1303_trm_osc_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_osc_text), sma1303_trm_osc_text);

static int sma1303_trm_osc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_94_BOOST_CTRL1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x70) >> 4);

	return 0;
}

static int sma1303_trm_osc_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_94_BOOST_CTRL1, 0x70, (sel << 4));

	return 0;
}

static const char * const sma1303_trm_rmp_text[] = {
	"RMP_0.49A/us", "RMP_0.98A/us", "RMP_1.47A/us", "RMP_1.96A/us",
	"RMP_2.45A/us", "RMP_2.94A/us", "RMP_3.43A/us", "RMP_3.92A/us"};

static const struct soc_enum sma1303_trm_rmp_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_rmp_text), sma1303_trm_rmp_text);

static int sma1303_trm_rmp_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_94_BOOST_CTRL1, &val);
	ucontrol->value.integer.value[0] = (val & 0x07);

	return 0;
}

static int sma1303_trm_rmp_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_94_BOOST_CTRL1, 0x07, sel);

	return 0;
}

/* Boost control2 Set */
static const char * const sma1303_trm_ocl_text[] = {
	"I_2.0A", "I_2.3A", "I_2.6A", "I_2.8A",
	"I_3.1A", "I_3.4A", "I_3.7A", "I_3.9A"};

static const struct soc_enum sma1303_trm_ocl_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_ocl_text), sma1303_trm_ocl_text);

static int sma1303_trm_ocl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_95_BOOST_CTRL2, &val);
	ucontrol->value.integer.value[0] = ((val & 0x70) >> 4);

	return 0;
}

static int sma1303_trm_ocl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_95_BOOST_CTRL2, 0x70, (sel << 4));

	return 0;
}

/* Boost control2 Set */
static const char * const sma1303_trm_comp_i_text[] = {
	"IG_30pF", "IG_60pF", "IG_90pF", "IG_120pF"};

static const struct soc_enum sma1303_trm_comp_i_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_comp_i_text),
			sma1303_trm_comp_i_text);

static int sma1303_trm_comp_i_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_95_BOOST_CTRL2, &val);
	ucontrol->value.integer.value[0] = ((val & 0x0C) >> 2);

	return 0;
}

static int sma1303_trm_comp_i_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_95_BOOST_CTRL2, 0x0C, (sel << 2));

	return 0;
}

static const char * const sma1303_trm_comp_p_text[] = {
	"PG_0.5Mohm", "PG_0.375Mohm", "PG_0.25Mohm", "PG_0.125Mohm"};

static const struct soc_enum sma1303_trm_comp_p_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_comp_p_text),
			sma1303_trm_comp_p_text);

static int sma1303_trm_comp_p_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_95_BOOST_CTRL2, &val);
	ucontrol->value.integer.value[0] = (val & 0x03);

	return 0;
}

static int sma1303_trm_comp_p_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_95_BOOST_CTRL2, 0x03, sel);

	return 0;
}

/* Boost control3 Set */
static const char * const sma1303_trm_dt_text[] = {
	"Time_16.0ns", "Time_14.0ns", "Time_12.0ns", "Time_11.0ns",
	"Time_10.0ns", "Time_9.0ns", "Time_8.0ns", "Time_7.3ns",
	"Time_6.7ns", "Time_6.2ns", "Time_5.8ns", "Time_5.4ns",
	"Time_5.1ns", "Time_4.8ns", "Time_2.1ns", "Time_2.1ns"};

static const struct soc_enum sma1303_trm_dt_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_dt_text), sma1303_trm_dt_text);

static int sma1303_trm_dt_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_96_BOOST_CTRL3, &val);
	ucontrol->value.integer.value[0] = ((val & 0xF0) >> 4);

	return 0;
}

static int sma1303_trm_dt_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_96_BOOST_CTRL3, 0xF0, (sel << 4));

	return 0;
}

static const char * const sma1303_trm_slw_text[] = {
	"Time_6.7ns", "Time_4.8ns", "Time_2.6ns", "Time_1.2ns"};

static const struct soc_enum sma1303_trm_slw_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_slw_text), sma1303_trm_slw_text);

static int sma1303_trm_slw_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_96_BOOST_CTRL3, &val);
	ucontrol->value.integer.value[0] = (val & 0x03);

	return 0;
}

static int sma1303_trm_slw_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_96_BOOST_CTRL3, 0x03, sel);

	return 0;
}

/* Boost control4 Set */
static const char * const sma1303_trm_vbst_text[] = {
	"BST_5.5V", "BST_5.6V", "BST_5.7V", "BST_5.8V",
	"BST_5.9V", "BST_6.0V", "BST_6.1V", "BST_6.2V"};

static const struct soc_enum sma1303_trm_vbst_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_vbst_text), sma1303_trm_vbst_text);

static int sma1303_trm_vbst_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_97_BOOST_CTRL4, &val);
	ucontrol->value.integer.value[0] = ((val & 0x1C) >> 2);

	return 0;
}

static int sma1303_trm_vbst_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	if (sma1303->impossible_bst_ctrl)
		dev_info(component->dev,
		"Trimming of boost voltage does not change on 'impossible-bst-ctrl' property\n");
	else {
		sma1303->bst_vol_lvl_status = sel;

		regmap_update_bits(sma1303->regmap,
			SMA1303_97_BOOST_CTRL4, 0x1C, (sel << 2));
	}

	return 0;
}

static const char * const sma1303_trm_tmin_text[] = {
	"Time_40ns", "Time_80ns", "Time_120ns", "Time_160ns"};

static const struct soc_enum sma1303_trm_tmin_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_trm_tmin_text), sma1303_trm_tmin_text);

static int sma1303_trm_tmin_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_97_BOOST_CTRL4, &val);
	ucontrol->value.integer.value[0] = (val & 0x03);

	return 0;
}

static int sma1303_trm_tmin_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_97_BOOST_CTRL4, 0x03, sel);

	return 0;
}

/* TOP_MAN1 Set */
static const char * const sma1303_pll_div_text[] = {
	"PLL_OUT", "PLL_OUT/2", "PLL_OUT/4", "PLL_OUT/8"};

static const struct soc_enum sma1303_pll_div_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_pll_div_text), sma1303_pll_div_text);

static int sma1303_pll_div_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A2_TOP_MAN1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x30) >> 4);

	return 0;
}

static int sma1303_pll_div_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A2_TOP_MAN1, 0x30, (sel << 4));

	return 0;
}


/* TOP_MAN3 Set */
static const char * const sma1303_o_format_text[] = {
	"Reserved", "LJ", "I2S", "Reserved",
	"TDM", "Reserved", "Reserved", "Reserved"};

static const struct soc_enum sma1303_o_format_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_o_format_text), sma1303_o_format_text);

static int sma1303_o_format_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A4_TOP_MAN3, &val);
	ucontrol->value.integer.value[0] = ((val & 0xE0) >> 5);

	return 0;
}

static int sma1303_o_format_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A4_TOP_MAN3, 0xE0, (sel << 5));

	return 0;
}

static const char * const sma1303_sck_rate_text[] = {
	"fs_64", "fs_64", "fs_32", "fs_32"};

static const struct soc_enum sma1303_sck_rate_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_sck_rate_text), sma1303_sck_rate_text);

static int sma1303_sck_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A4_TOP_MAN3, &val);
	ucontrol->value.integer.value[0] = ((val & 0x18) >> 3);

	return 0;
}

static int sma1303_sck_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A4_TOP_MAN3, 0x18, (sel << 3));

	return 0;
}

/* TDM1 Set */
static const char * const sma1303_tdm_slot1_rx_text[] = {
	"0", "1", "2", "3", "4", "5", "6", "7"};

static const struct soc_enum sma1303_tdm_slot1_rx_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_tdm_slot1_rx_text),
		sma1303_tdm_slot1_rx_text);

static int sma1303_tdm_slot1_rx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A5_TDM1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x38) >> 3);

	return 0;
}

static int sma1303_tdm_slot1_rx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A5_TDM1, 0x38, (sel << 3));

	return 0;
}

static const char * const sma1303_tdm_slot2_rx_text[] = {
	"0", "1", "2", "3", "4", "5", "6", "7"};

static const struct soc_enum sma1303_tdm_slot2_rx_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_tdm_slot2_rx_text),
		sma1303_tdm_slot2_rx_text);

static int sma1303_tdm_slot2_rx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A5_TDM1, &val);
	ucontrol->value.integer.value[0] = ((val & 0x07) >> 0);

	return 0;
}

static int sma1303_tdm_slot2_rx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A5_TDM1, 0x07, (sel << 0));

	return 0;
}

/* TDM2 Set */
static const char * const sma1303_tdm_slot1_tx_text[] = {
	"0", "1", "2", "3", "4", "5", "6", "7"};

static const struct soc_enum sma1303_tdm_slot1_tx_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_tdm_slot1_tx_text),
			sma1303_tdm_slot1_tx_text);

static int sma1303_tdm_slot1_tx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A6_TDM2, &val);
	ucontrol->value.integer.value[0] = ((val & 0x38) >> 3);

	return 0;
}

static int sma1303_tdm_slot1_tx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A6_TDM2, 0x38, (sel << 3));

	return 0;
}

static const char * const sma1303_tdm_slot2_tx_text[] = {
	"0", "1", "2", "3", "4", "5", "6", "7"};

static const struct soc_enum sma1303_tdm_slot2_tx_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_tdm_slot2_tx_text),
			sma1303_tdm_slot2_tx_text);

static int sma1303_tdm_slot2_tx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A6_TDM2, &val);
	ucontrol->value.integer.value[0] = ((val & 0x07) >> 0);

	return 0;
}

static int sma1303_tdm_slot2_tx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A6_TDM2, 0x07, (sel << 0));

	return 0;
}

/* Clock Monitioring Set */
static const char * const sma1303_clk_mon_time_sel_text[] = {
	"Time_80us", "Time_40us", "Time_20us", "Time_10us"};

static const struct soc_enum sma1303_clk_mon_time_sel_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_clk_mon_time_sel_text),
			sma1303_clk_mon_time_sel_text);

static int sma1303_clk_mon_time_sel_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val;

	regmap_read(sma1303->regmap, SMA1303_A7_CLK_MON, &val);
	ucontrol->value.integer.value[0] = ((val & 0xC0) >> 6);

	return 0;
}

static int sma1303_clk_mon_time_sel_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int sel = (int)ucontrol->value.integer.value[0];

	regmap_update_bits(sma1303->regmap,
		SMA1303_A7_CLK_MON, 0xC0, (sel << 6));

	return 0;
}

static const struct snd_kcontrol_new sma1303_snd_controls[] = {
/* System CTRL [0x00] */
SOC_SINGLE("I2C Reg Reset(1:reset_0:normal)",
		SMA1303_00_SYSTEM_CTRL, 1, 1, 0),
SOC_SINGLE_EXT("Power Up(1:Up_0:Down)", SND_SOC_NOPM, 0, 1, 0,
	power_up_down_control_get, power_up_down_control_put),
SOC_SINGLE_EXT("Force AMP Power Down", SND_SOC_NOPM, 0, 1, 0,
	power_down_control_get, power_down_control_put),

/* Input CTRL1 [0x01] */
SOC_SINGLE("I2S/PCM Clock mode(1:M_2:S)",
		SMA1303_01_INPUT1_CTRL1, 7, 1, 0),
SOC_ENUM_EXT("I2S input format(I2S_LJ_RJ)", sma1303_input_format_enum,
	sma1303_input_format_get, sma1303_input_format_put),
SOC_SINGLE("First-channel pol I2S(1:H_0:L)",
		SMA1303_01_INPUT1_CTRL1, 3, 1, 0),
SOC_SINGLE("Data written SCK edge(1:R_0:F)",
		SMA1303_01_INPUT1_CTRL1, 2, 1, 0),

/* Input CTRL2 [0x02] */
SOC_ENUM_EXT("Input audio mode", sma1303_in_audio_mode_enum,
	sma1303_in_audio_mode_get, sma1303_in_audio_mode_put),
SOC_SINGLE("Data inversion(1:R_0:L)",
		SMA1303_02_INPUT1_CTRL2, 5, 1, 0),
SOC_SINGLE("Decoding select(1:A_0:u)",
		SMA1303_02_INPUT1_CTRL2, 4, 1, 0),
SOC_SINGLE("Companding PCM data(1:C_2:L)",
		SMA1303_02_INPUT1_CTRL2, 3, 1, 0),
SOC_SINGLE("PCM sample freq(1:16kHz_0:8kHz)",
		SMA1303_02_INPUT1_CTRL2, 2, 1, 0),
SOC_SINGLE("PCM stereo/mono(1:S_0:M)",
		SMA1303_02_INPUT1_CTRL2, 1, 1, 0),
SOC_SINGLE("PCM data length(1:16_0:8)",
		SMA1303_02_INPUT1_CTRL2, 0, 1, 0),

/* Input CTRL3 [0x03] */
SOC_ENUM_EXT("Number of slots per sampling period(PCM)",
		sma1303_pcm_n_slot_enum, sma1303_pcm_n_slot_get,
		sma1303_pcm_n_slot_put),

/* Input CTRL4 [0x04] */
SOC_ENUM_EXT("Position of the first sample at 8,16kHz",
		sma1303_pcm1_slot_enum, sma1303_pcm1_slot_get,
		sma1303_pcm1_slot_put),
SOC_ENUM_EXT("Position of the second sample at 16kHz",
		sma1303_pcm2_slot_enum, sma1303_pcm2_slot_get,
		sma1303_pcm2_slot_put),

/* Output CTRL [0x09] */
SOC_ENUM_EXT("Port In/Out port config", sma1303_port_config_enum,
	sma1303_port_config_get, sma1303_port_config_put),
SOC_ENUM_EXT("Port Output Source", sma1303_port_out_sel_enum,
	sma1303_port_out_sel_get, sma1303_port_out_sel_put),

/* SPK_VOL [0x0A] */
SOC_SINGLE_TLV("Speaker Volume", SMA1303_0A_SPK_VOL,
		0, 167, 1, sma1303_spk_tlv),

/* BST_TEST [0x0B] */
SOC_ENUM_EXT("BST Turn-off slope", sma1303_bst_off_slope_enum,
	sma1303_bst_off_slope_get, sma1303_bst_off_slope_put),
SOC_SINGLE("OCP Test(1:test_0:normal)",
		SMA1303_0B_BST_TEST, 5, 1, 0),
SOC_SINGLE("LEB(1:long_0:short)",
		SMA1303_0B_BST_TEST, 4, 1, 0),
SOC_SINGLE("P-Gain sel(1:normal_0:high)",
		SMA1303_0B_BST_TEST, 3, 1, 0),
SOC_SINGLE("VCOMP Mon(1:mon_0:normal)",
		SMA1303_0B_BST_TEST, 2, 1, 0),
SOC_SINGLE("PMOS Test(1:pmos_0:normal)",
		SMA1303_0B_BST_TEST, 1, 1, 0),
SOC_SINGLE("NMOS Test(1:nmos_0:normal)",
		SMA1303_0B_BST_TEST, 0, 1, 0),

/* BST_TEST1 [0x0C] */
SOC_ENUM_EXT("High Side OCP lvl", sma1303_set_ocp_h_enum,
	sma1303_set_ocp_h_get, sma1303_set_ocp_h_put),
SOC_SINGLE("OCL Test(1:test_0:normal)",
		SMA1303_0C_BST_TEST1, 5, 1, 0),
SOC_SINGLE("Loop Check(1:en_0:normal)",
		SMA1303_0C_BST_TEST1, 4, 1, 0),
SOC_SINGLE("SH Protection(1:en_0:dis)",
		SMA1303_0C_BST_TEST1, 3, 1, 0),

/* SPK_TEST [0x0D] */
SOC_SINGLE("SPK OCP filter delay(1:Normal_0:Long)",
		SMA1303_0D_SPK_TEST, 2, 1, 0),
SOC_ENUM_EXT("SPK turn off slope control", sma1303_spk_off_slope_enum,
	sma1303_spk_off_slope_get, sma1303_spk_off_slope_put),

/* MUTE_VOL_CTRL [0x0E] */
SOC_ENUM_EXT("Volume slope", sma1303_vol_slope_enum,
	sma1303_vol_slope_get, sma1303_vol_slope_put),
SOC_ENUM_EXT("Mute slope", sma1303_mute_slope_enum,
	sma1303_mute_slope_get, sma1303_mute_slope_put),
SOC_SINGLE("Speaker Mute Switch(1:muted_0:un)",
		SMA1303_0E_MUTE_VOL_CTRL, 0, 1, 0),

/* System CTRL1 [0x10] */
SOC_ENUM_EXT("Speaker Mode", sma1303_spkmode_enum,
	sma1303_spkmode_get, sma1303_spkmode_put),

/* System CTRL2 [0x11] */
SOC_SINGLE("Speaker Bass(1:en_0:dis)", SMA1303_11_SYSTEM_CTRL2, 6, 1, 0),
SOC_SINGLE("Speaker Comp/Limiter(1:en_0:dis)",
		SMA1303_11_SYSTEM_CTRL2, 5, 1, 0),
SOC_SINGLE("LR_DATA_SW(1:swap_0:normal)", SMA1303_11_SYSTEM_CTRL2, 4, 1, 0),
SOC_SINGLE("Mono Mix(1:en_0:dis)", SMA1303_11_SYSTEM_CTRL2, 0, 1, 0),

/* System CTRL3 [0x12] */
SOC_ENUM_EXT("Input gain", sma1303_input_gain_enum,
	sma1303_input_gain_get, sma1303_input_gain_put),
SOC_ENUM_EXT("Input gain for right channel", sma1303_input_r_gain_enum,
	sma1303_input_r_gain_get, sma1303_input_r_gain_put),

/* Modulator [0x14] */
SOC_ENUM_EXT("Speaker HYSFB", sma1303_spk_hysfb_enum,
	sma1303_spk_hysfb_get, sma1303_spk_hysfb_put),
SND_SOC_BYTES_EXT("Speaker BDELAY", 1, spk_bdelay_get, spk_bdelay_put),

/* BassSpk1~7 [0x15 ~ 0x1B] */
SND_SOC_BYTES_EXT("Bass Boost SPK Coeff", 7,
	bass_spk_coeff_get, bass_spk_coeff_put),

/* Comp_Lim1~4 [0x23 ~ 0x26] */
SND_SOC_BYTES_EXT("DRC SPK Coeff", 4,
	comp_lim_spk_coeff_get, comp_lim_spk_coeff_put),

/* SDM CTRL [0x33] */
SOC_SINGLE("SPK modulator sync(1:1/8_0:1/4)",
		SMA1303_33_SDM_CTRL, 2, 1, 0),

/* Protection [0x36] */
SOC_SINGLE("Edge displacement(1:dis_0:en)",
		SMA1303_36_PROTECTION, 7, 1, 0),
SOC_ENUM_EXT("PWM LR delay", sma1303_lr_delay_enum,
		sma1303_lr_delay_get, sma1303_lr_delay_put),
SOC_SINGLE("OCP spk output state(1:dis_0:en)",
		SMA1303_36_PROTECTION, 3, 1, 0),
SOC_SINGLE("OCP mode(1:SD_0:AR)",
		SMA1303_36_PROTECTION, 2, 1, 0),
SOC_ENUM_EXT("OTP mode", sma1303_otp_mode_enum,
		sma1303_otp_mode_get, sma1303_otp_mode_put),

/* Slope CTRL [0x37] */
SND_SOC_BYTES_EXT("SlopeCTRL", 1, slope_ctrl_get, slope_ctrl_put),


/* Test1~3, ATEST1~2 [0x3B ~ 0x3F] */
SND_SOC_BYTES_EXT("Test mode(Test 1~3_ATEST 1~2)",
		5, test_mode_get, test_mode_put),

/* PLL Setting [0x8B ~ 0x8F] */
SND_SOC_BYTES_EXT("PLL Setting", 5, pll_setting_get, pll_setting_put),
SND_SOC_BYTES_EXT("PLL Power control", 1, pll_pd_ctrl_get, pll_pd_ctrl_put),
SOC_ENUM_EXT("Trimming of OTP Level(1:on_0:off)", sma1303_trm_lvl_enum,
	sma1303_trm_lvl_get, sma1303_trm_lvl_put),
SOC_SINGLE("Low OCL mode(1:normal_0:low)",
		SMA1303_8E_PLL_CTRL, 3, 1, 0),

/* Postscaler [0x90] */
SND_SOC_BYTES_EXT("Postscaler Set", 1,
		postscaler_get, postscaler_put),
SOC_SINGLE("Postscaler(1:bypass_0:en)",
		SMA1303_90_POSTSCALER, 0, 1, 0),

/* ClassG CTRL [0x91] */
SOC_ENUM_EXT("Attack level control", sma1303_attack_lvl_enum,
	sma1303_attack_lvl_get, sma1303_attack_lvl_put),
SOC_ENUM_EXT("Release time control", sma1303_release_time_enum,
	sma1303_release_time_get, sma1303_release_time_put),

/* FDPEC CTRL [0x92] */
SOC_ENUM_EXT("Filtered VDD gain control", sma1303_flt_vdd_gain_enum,
	sma1303_flt_vdd_gain_get, sma1303_flt_vdd_gain_put),
SOC_SINGLE("Fast charge(1:dis_0:en)",
		SMA1303_92_FDPEC_CTRL, 2, 1, 0),

/* Boost CTRL1 [0x94] */
SOC_ENUM_EXT("Trimming of switching frequency", sma1303_trm_osc_enum,
	sma1303_trm_osc_get, sma1303_trm_osc_put),
SOC_ENUM_EXT("Trimming of ramp compensation", sma1303_trm_rmp_enum,
	sma1303_trm_rmp_get, sma1303_trm_rmp_put),

/* Boost CTRL2 [0x95] */
SOC_ENUM_EXT("Trimming of over current limit", sma1303_trm_ocl_enum,
	sma1303_trm_ocl_get, sma1303_trm_ocl_put),
SOC_ENUM_EXT("Trimming of loop comp I gain", sma1303_trm_comp_i_enum,
	sma1303_trm_comp_i_get, sma1303_trm_comp_i_put),
SOC_ENUM_EXT("Trimming of loop comp P gain", sma1303_trm_comp_p_enum,
	sma1303_trm_comp_p_get, sma1303_trm_comp_p_put),

/* Boost CTRL3 [0x96] */
SOC_ENUM_EXT("Trimming of driver deadtime", sma1303_trm_dt_enum,
	sma1303_trm_dt_get, sma1303_trm_dt_put),
SOC_ENUM_EXT("Trimming of switching slew", sma1303_trm_slw_enum,
	sma1303_trm_slw_get, sma1303_trm_slw_put),

/* Boost CTRL4 [0x97] */
SOC_ENUM_EXT("Trimming of boost voltage", sma1303_trm_vbst_enum,
	sma1303_trm_vbst_get, sma1303_trm_vbst_put),
SOC_ENUM_EXT("Trimming of minimum on-time", sma1303_trm_tmin_enum,
	sma1303_trm_tmin_get, sma1303_trm_tmin_put),

/* PAD CTRL0 [0xA0] */
SOC_SINGLE("PAD SDO(1:3mA_0:2mA)",
		SMA1303_A0_PAD_CTRL0, 2, 1, 0),

/* PAD CTRL1 [0xA1] */
SOC_SINGLE("PAD Input LRCK(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 5, 1, 0),
SOC_SINGLE("PAD Input SCK(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 4, 1, 0),
SOC_SINGLE("PAD Input SDI(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 3, 1, 0),
SOC_SINGLE("PAD Input SDO(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 2, 1, 0),
SOC_SINGLE("PAD Input SDA(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 1, 1, 0),
SOC_SINGLE("PAD Input SCL(1:on_0:off)",
		SMA1303_A1_PAD_CTRL1, 0, 1, 0),

/* TOP_MAN1 [0xA2] */
SOC_SINGLE("PLL Lock Skip Mode(1:dis_0:en)",
		SMA1303_A2_TOP_MAN1, 7, 1, 0),
SOC_SINGLE("PLL Power Down(1:PD_0:oper)",
		SMA1303_A2_TOP_MAN1, 6, 1, 0),
SOC_ENUM_EXT("PLL Test Divider at SDO", sma1303_pll_div_enum,
		sma1303_pll_div_get, sma1303_pll_div_put),
SOC_SINGLE("PLL Ref Clk(1:SCK_0:PLL_REF_Clk1)",
		SMA1303_A2_TOP_MAN1, 3, 1, 0),
SOC_SINGLE("DAC Down Conver(1:C_0:N)",
		SMA1303_A2_TOP_MAN1, 2, 1, 0),
SOC_SINGLE("SDO Pad Output Ctrl(1:L_0:H)",
		SMA1303_A2_TOP_MAN1, 1, 1, 0),
SOC_SINGLE("SDO_output2(1:only_0:normal)",
		SMA1303_A2_TOP_MAN1, 0, 1, 0),

/* TOP_MAN2 [0xA3] */
SOC_SINGLE("Monitoring at SDO(1:OSC_0:PLL)",
		SMA1303_A3_TOP_MAN2, 7, 1, 0),
SOC_SINGLE("Test clk out en(1:Clk out_0:N)",
		SMA1303_A3_TOP_MAN2, 6, 1, 0),
SOC_SINGLE("SDO output(1:H_0:N)",
		SMA1303_A3_TOP_MAN2, 3, 1, 0),
SOC_SINGLE("Clk Monitor(1:Not_0:Mon)",
		SMA1303_A3_TOP_MAN2, 1, 1, 0),
SOC_SINGLE("OSC PD(1:PD_0:N)",
		SMA1303_A3_TOP_MAN2, 0, 1, 0),

/* TOP_MAN3 [0xA4] */
SOC_ENUM_EXT("TOP Manager Output Format", sma1303_o_format_enum,
	sma1303_o_format_get, sma1303_o_format_put),
SOC_ENUM_EXT("TOP Manager SCK rate", sma1303_sck_rate_enum,
	sma1303_sck_rate_get, sma1303_sck_rate_put),
SOC_SINGLE("TOP Manager LRCK Pol(1:H_0:L)",
		SMA1303_A4_TOP_MAN3, 0, 1, 0),

/* TDM1 [0xA5] */
SOC_SINGLE("TDM clk pol(1:fall_0:rise)",
		SMA1303_A5_TDM1, 7, 1, 0),
SOC_SINGLE("TDM Tx(1:stereo_0:mono)",
		SMA1303_A5_TDM1, 6, 1, 0),
SOC_ENUM_EXT("TDM slot1 pos Rx", sma1303_tdm_slot1_rx_enum,
		sma1303_tdm_slot1_rx_get, sma1303_tdm_slot1_rx_put),
SOC_ENUM_EXT("TDM slot2 pos Rx", sma1303_tdm_slot2_rx_enum,
		sma1303_tdm_slot2_rx_get, sma1303_tdm_slot2_rx_put),

/* TDM2 [0xA6] */
SOC_SINGLE("TDM Data Length(1:32_0:16)",
		SMA1303_A6_TDM2, 7, 1, 0),
SOC_SINGLE("TDM n-slot(1:8_0:4)",
		SMA1303_A6_TDM2, 6, 1, 0),
SOC_ENUM_EXT("TDM slot1 pos Tx", sma1303_tdm_slot1_tx_enum,
		sma1303_tdm_slot1_tx_get, sma1303_tdm_slot1_tx_put),
SOC_ENUM_EXT("TDM slot2 pos Tx", sma1303_tdm_slot2_tx_enum,
		sma1303_tdm_slot2_tx_get, sma1303_tdm_slot2_tx_put),

/* CLK_MON [0xA7] */
SOC_ENUM_EXT("Clk monitor time select", sma1303_clk_mon_time_sel_enum,
		sma1303_clk_mon_time_sel_get, sma1303_clk_mon_time_sel_put),
SOC_SINGLE("Noise Gen mod(1:en_0:normal)",
		SMA1303_A7_CLK_MON, 5, 1, 0),
SOC_SINGLE("Noise Gen post(1:en_0:normal)",
		SMA1303_A7_CLK_MON, 4, 1, 0),
SOC_SINGLE("Limiter Oper Range(1:en_0:dis)",
		SMA1303_A7_CLK_MON, 3, 1, 0),
};

static int sma1303_startup(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	if (sma1303->amp_power_status) {
		dev_info(component->dev, "%s : %s\n",
			__func__, "Already AMP Power on");
		return 0;
	}

	dev_info(component->dev, "%s : TRM_%s, FLT_%s\n",
	__func__, sma1303_trm_vbst_text[sma1303->bst_vol_lvl_status],
	sma1303_flt_vdd_gain_text[sma1303->flt_vdd_gain_status]);

	regmap_update_bits(sma1303->regmap, SMA1303_8E_PLL_CTRL,
			PLL_PD2_MASK, PLL_OPERATION2);

	regmap_update_bits(sma1303->regmap, SMA1303_00_SYSTEM_CTRL,
			POWER_MASK, POWER_ON);

	if (sma1303->stereo_two_chip == true) {
		/* SPK Mode (Stereo) */
		regmap_update_bits(sma1303->regmap, SMA1303_10_SYSTEM_CTRL1,
				SPK_MODE_MASK, SPK_STEREO);
	} else {
		/* SPK Mode (Mono) */
		regmap_update_bits(sma1303->regmap, SMA1303_10_SYSTEM_CTRL1,
				SPK_MODE_MASK, SPK_MONO);
	}

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	regmap_update_bits(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL,
			SPK_MUTE_MASK, SPK_UNMUTE);

	sma1303->amp_power_status = true;

	return 0;
}

static int sma1303_shutdown(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	if (!(sma1303->amp_power_status)) {
		dev_info(component->dev, "%s : %s\n",
			__func__, "Already AMP Shutdown");
		return 0;
	}

	dev_info(component->dev, "%s\n", __func__);

	regmap_update_bits(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL,
			SPK_MUTE_MASK, SPK_MUTE);

	cancel_delayed_work_sync(&sma1303->check_fault_work);

	/* To improve the Boost OCP issue,
	 * time should be available for the Boost release time(40ms)
	 * and Mute slope time(15ms)
	 */
	msleep(55);

	regmap_update_bits(sma1303->regmap, SMA1303_10_SYSTEM_CTRL1,
			SPK_MODE_MASK, SPK_OFF);

	regmap_update_bits(sma1303->regmap, SMA1303_00_SYSTEM_CTRL,
			POWER_MASK, POWER_OFF);

	regmap_update_bits(sma1303->regmap, SMA1303_8E_PLL_CTRL,
			PLL_PD2_MASK, PLL_PD2);

	sma1303->amp_power_status = false;

	return 0;
}

static int sma1303_clk_supply_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_info(component->dev, "%s : PRE_PMU\n", __func__);
	break;

	case SND_SOC_DAPM_POST_PMD:
		dev_info(component->dev, "%s : POST_PMD\n", __func__);
	break;
	}

	return 0;
}

static int sma1303_dac_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_info(component->dev, "%s : PRE_PMU\n", __func__);

		if (sma1303->force_amp_power_down == false)
			sma1303_startup(component);

		break;

	case SND_SOC_DAPM_POST_PMU:
		dev_info(component->dev, "%s : POST_PMU\n", __func__);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		dev_info(component->dev, "%s : PRE_PMD\n", __func__);

		sma1303_shutdown(component);

		break;

	case SND_SOC_DAPM_POST_PMD:
		dev_info(component->dev, "%s : POST_PMD\n", __func__);

		break;
	}

	return 0;
}

static int sma1303_dac_feedback_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_info(component->dev, "%s : DAC feedback ON\n", __func__);
		regmap_update_bits(sma1303->regmap,
			SMA1303_09_OUTPUT_CTRL,
				PORT_CONFIG_MASK|PORT_OUT_SEL_MASK,
				OUTPUT_PORT_ENABLE|SPEAKER_PATH);
		/* even if Capture stream on, Mixer should turn on
		 * SDO output(1:High-Z,0:Normal output)
		 */
		regmap_update_bits(sma1303->regmap,
			SMA1303_A3_TOP_MAN2, SDO_OUTPUT_MASK,
				NORMAL_OUT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		dev_info(component->dev, "%s : DAC feedback OFF\n", __func__);
		regmap_update_bits(sma1303->regmap,
			SMA1303_09_OUTPUT_CTRL, PORT_OUT_SEL_MASK,
				OUT_SEL_DISABLE);
		regmap_update_bits(sma1303->regmap,
			SMA1303_A3_TOP_MAN2, SDO_OUTPUT_MASK,
				HIGH_Z_OUT);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget sma1303_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("CLK_SUPPLY", SND_SOC_NOPM, 0, 0, sma1303_clk_supply_event,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_DAC_E("DAC", "Playback", SND_SOC_NOPM, 0, 0, sma1303_dac_event,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_ADC_E("DAC_FEEDBACK", "Capture", SND_SOC_NOPM, 0, 0,
				sma1303_dac_feedback_event,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_OUTPUT("SPK"),
SND_SOC_DAPM_INPUT("SDO"),
};

static const struct snd_soc_dapm_route sma1303_audio_map[] = {
/* sink, control, source */
{"DAC", NULL, "CLK_SUPPLY"},
{"SPK", NULL, "DAC"},
{"DAC_FEEDBACK", NULL, "SDO"},
};

static int sma1303_setup_pll(struct snd_soc_component *component,
		unsigned int bclk)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	int i = 0;

	dev_info(component->dev, "%s : BCLK = %d kHz\n",
		__func__, bclk/1000);

	if (sma1303->sys_clk_id == SMA1303_PLL_CLKIN_MCLK) {
		dev_info(component->dev, "%s : MCLK is not supported\n",
		__func__);
	} else if (sma1303->sys_clk_id == SMA1303_PLL_CLKIN_BCLK) {
		/* PLL operation, PLL Clock, External Clock,
		 * PLL reference SCK clock
		 */
		regmap_update_bits(sma1303->regmap, SMA1303_A2_TOP_MAN1,
				PLL_PD_MASK|PLL_REF_CLK_MASK,
				PLL_OPERATION|PLL_SCK);

		for (i = 0; i < sma1303->num_of_pll_matches; i++) {
			if (sma1303->pll_matches[i].input_clk == bclk)
				break;
		}
	}

	regmap_write(sma1303->regmap, SMA1303_8B_PLL_POST_N,
			sma1303->pll_matches[i].post_n);
	regmap_write(sma1303->regmap, SMA1303_8C_PLL_N,
			sma1303->pll_matches[i].n);
	regmap_write(sma1303->regmap, SMA1303_8D_PLL_A_SETTING,
			sma1303->pll_matches[i].vco);
	regmap_write(sma1303->regmap, SMA1303_8F_PLL_P_CP,
			sma1303->pll_matches[i].p_cp);

	return 0;
}

static int sma1303_dai_hw_params_amp(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	unsigned int input_format = 0;
	unsigned int bclk = params_rate(params) * params_physical_width(params)
			* params_channels(params);

	dev_info(component->dev, "%s : rate = %d : bit size = %d : channel = %d\n", __func__, params_rate(params), params_width(params), params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		/* PLL clock setting according to sample rate and bit */
		if (sma1303->force_amp_power_down == false &&
			(sma1303->sys_clk_id == SMA1303_PLL_CLKIN_MCLK
			|| sma1303->sys_clk_id == SMA1303_PLL_CLKIN_BCLK)) {

			if (sma1303->last_bclk != bclk) {
				if (sma1303->amp_power_status) {
					sma1303_shutdown(component);
					sma1303_setup_pll(component, bclk);
					sma1303_startup(component);
				} else
					sma1303_setup_pll(component, bclk);

				sma1303->last_bclk = bclk;
			}
		}

		switch (params_rate(params)) {
		case 8000:
		case 12000:
		case 16000:
		case 24000:
		case 32000:
		case 44100:
		case 48000:
		case 96000:
		regmap_update_bits(sma1303->regmap, SMA1303_A2_TOP_MAN1,
				DAC_DN_CONV_MASK, DAC_DN_CONV_DISABLE);
		regmap_update_bits(sma1303->regmap, SMA1303_01_INPUT1_CTRL1,
				LEFTPOL_MASK, LOW_FIRST_CH);
		break;

		case 192000:
		regmap_update_bits(sma1303->regmap, SMA1303_A2_TOP_MAN1,
				DAC_DN_CONV_MASK, DAC_DN_CONV_ENABLE);
		regmap_update_bits(sma1303->regmap, SMA1303_01_INPUT1_CTRL1,
				LEFTPOL_MASK, HIGH_FIRST_CH);
		break;

		default:
			dev_err(component->dev, "%s not support rate : %d\n",
				__func__, params_rate(params));

		return -EINVAL;
		}
	/* substream->stream is SNDRV_PCM_STREAM_CAPTURE */
	} else {

		switch (params_format(params)) {

		case SNDRV_PCM_FORMAT_S16_LE:
			dev_info(component->dev,
				"%s set format SNDRV_PCM_FORMAT_S16_LE\n",
				__func__);
			break;

		case SNDRV_PCM_FORMAT_S24_LE:
			dev_info(component->dev,
				"%s set format SNDRV_PCM_FORMAT_S24_LE\n",
				__func__);
			break;

		default:
			dev_err(component->dev,
				"%s not support data bit : %d\n", __func__,
						params_format(params));
			return -EINVAL;
		}
	}

	switch (params_width(params)) {
	case 16:
		switch (sma1303->format) {
		case SND_SOC_DAIFMT_I2S:
			input_format |= STANDARD_I2S;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			input_format |= LJ;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			input_format |= RJ_16BIT;
			break;
		}
		break;
	case 24:
		switch (sma1303->format) {
		case SND_SOC_DAIFMT_I2S:
			input_format |= STANDARD_I2S;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			input_format |= LJ;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			input_format |= RJ_24BIT;
			break;
		}
		break;

	default:
		dev_err(component->dev,
			"%s not support data bit : %d\n", __func__,
					params_format(params));
		return -EINVAL;
	}

	regmap_update_bits(sma1303->regmap, SMA1303_01_INPUT1_CTRL1,
				I2S_MODE_MASK, input_format);

	return 0;
}

static int sma1303_dai_set_sysclk_amp(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	dev_info(component->dev, "%s\n", __func__);

	switch (clk_id) {
	case SMA1303_EXTERNAL_CLOCK_19_2:
		break;
	case SMA1303_EXTERNAL_CLOCK_24_576:
		break;
	case SMA1303_PLL_CLKIN_MCLK:
		break;
	case SMA1303_PLL_CLKIN_BCLK:
		break;
	default:
		dev_err(component->dev, "Invalid clk id: %d\n", clk_id);
		return -EINVAL;
	}
	sma1303->sys_clk_id = clk_id;
	return 0;
}

static int sma1303_dai_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	if (!(sma1303->amp_power_status)) {
		dev_info(component->dev, "%s : %s\n",
			__func__, "Already AMP Shutdown");
		return 0;
	}

	if (mute) {
		dev_info(component->dev, "%s : %s\n", __func__, "MUTE");

		regmap_update_bits(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL,
					SPK_MUTE_MASK, SPK_MUTE);
	} else {
		dev_info(component->dev, "%s : %s\n", __func__, "UNMUTE");

		regmap_update_bits(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL,
					SPK_MUTE_MASK, SPK_UNMUTE);
	}

	return 0;
}

static int sma1303_dai_set_fmt_amp(struct snd_soc_dai *dai,
					unsigned int fmt)
{
	struct snd_soc_component *component  = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {

	case SND_SOC_DAIFMT_CBS_CFS:
		dev_info(component->dev,
				"%s : %s\n", __func__, "I2S slave mode");
		/* I2S/PCM clock mode - slave mode */
		regmap_update_bits(sma1303->regmap, SMA1303_01_INPUT1_CTRL1,
					MASTER_SLAVE_MASK, SLAVE_MODE);

		break;

	case SND_SOC_DAIFMT_CBM_CFM:
		dev_info(component->dev,
				"%s : %s\n", __func__, "I2S master mode");
		/* I2S/PCM clock mode - master mode */
		regmap_update_bits(sma1303->regmap, SMA1303_01_INPUT1_CTRL1,
					MASTER_SLAVE_MASK, MASTER_MODE);
		break;

	default:
		dev_err(component->dev,
				"Unsupported MASTER/SLAVE : 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		sma1303->format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;

	default:
		dev_err(component->dev,
				"Unsupported I2S FORMAT : 0x%x\n", fmt);
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sma1303_dai_ops_amp = {
	.set_sysclk = sma1303_dai_set_sysclk_amp,
	.set_fmt = sma1303_dai_set_fmt_amp,
	.hw_params = sma1303_dai_hw_params_amp,
	.digital_mute = sma1303_dai_digital_mute,
};

#define SMA1303_RATES SNDRV_PCM_RATE_8000_192000
#define SMA1303_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
		SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sma1303_dai[] = {
{
	.name = "sma1303-amplifier",
	.id = 0,
	.playback = {
	.stream_name = "Playback",
	.channels_min = 1,
	.channels_max = 2,
	.rates = SMA1303_RATES,
	.formats = SMA1303_FORMATS,
	},
	.capture = {
	.stream_name = "Capture",
	.channels_min = 1,
	.channels_max = 2,
	.rates = SMA1303_RATES,
	.formats = SMA1303_FORMATS,
	},
	.ops = &sma1303_dai_ops_amp,
}
};

static int sma1303_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:

		dev_info(component->dev, "%s\n", "SND_SOC_BIAS_ON");
		sma1303_startup(component);

		break;

	case SND_SOC_BIAS_PREPARE:

		dev_info(component->dev, "%s\n", "SND_SOC_BIAS_PREPARE");

		break;

	case SND_SOC_BIAS_STANDBY:

		dev_info(component->dev, "%s\n", "SND_SOC_BIAS_STANDBY");

		break;

	case SND_SOC_BIAS_OFF:

		dev_info(component->dev, "%s\n", "SND_SOC_BIAS_OFF");
		sma1303_shutdown(component);

		break;
	}

	/* Don't use codec->dapm.bias_level,
	 * use snd_soc_component_get_dapm() if it is needed
	 */

	return 0;
}

static void sma1303_check_fault_worker(struct work_struct *work)
{
	struct sma1303_priv *sma1303 =
		container_of(work, struct sma1303_priv, check_fault_work.work);
	int ret;
	unsigned int over_temp, ocp_val, uvlo_val;
	bool fault_val_flag = false;

	mutex_lock(&sma1303->lock);

	if (sma1303->tsdw_cnt)
		ret = regmap_read(sma1303->regmap,
			SMA1303_0A_SPK_VOL, &sma1303->cur_vol);
	else
		ret = regmap_read(sma1303->regmap,
			SMA1303_0A_SPK_VOL, &sma1303->init_vol);

	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_0A_SPK_VOL : %d\n", ret);
		mutex_unlock(&sma1303->lock);
		return;
	}

	ret = regmap_read(sma1303->regmap, SMA1303_FA_STATUS1, &over_temp);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FA_STATUS1 : %d\n", ret);
		mutex_unlock(&sma1303->lock);
		return;
	}

	ret = regmap_read(sma1303->regmap, SMA1303_FB_STATUS2, &ocp_val);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FB_STATUS2 : %d\n", ret);
		mutex_unlock(&sma1303->lock);
		return;
	}

	ret = regmap_read(sma1303->regmap, SMA1303_FF_DEVICE_INDEX, &uvlo_val);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FF_DEVICE_INDEX : %d\n", ret);
		mutex_unlock(&sma1303->lock);
		return;
	}

	if (~over_temp & OT1_OK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OT1(Over Temperature Level 1)\n", __func__);

		/* Volume control (Current Volume -3dB) */
		if ((sma1303->cur_vol + 6) <= 0xFF)
			regmap_write(sma1303->regmap,
				SMA1303_0A_SPK_VOL, sma1303->cur_vol + 6);

		sma1303->tsdw_cnt++;
		fault_val_flag = true;
	} else if (sma1303->tsdw_cnt) {
		regmap_write(sma1303->regmap,
			SMA1303_0A_SPK_VOL, sma1303->init_vol);
		sma1303->tsdw_cnt = 0;
		sma1303->cur_vol = sma1303->init_vol;
	}

	if (~over_temp & OT2_OK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OT2(Over Temperature Level 2)\n", __func__);
		fault_val_flag = true;
	}
	if (ocp_val & OCP_SPK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OCP_SPK(Over Current Protect SPK)\n", __func__);
		fault_val_flag = true;
	}
	if (ocp_val & OCP_BST_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OCP_BST(Over Current Protect Boost)\n", __func__);
		fault_val_flag = true;
	}
	if ((ocp_val & CLK_MON_STATUS) && (sma1303->amp_power_status)) {
		dev_crit(sma1303->dev,
			"%s : CLK_FAULT(No clock input)\n", __func__);
		fault_val_flag = true;
	}
	if (uvlo_val & UVLO_BST_STATUS) {
		dev_crit(sma1303->dev,
			"%s : UVLO(Under Voltage Lock Out)\n", __func__);
		fault_val_flag = true;
	}

	if ((over_temp != sma1303->last_over_temp) ||
		(ocp_val != sma1303->last_ocp_val)) {

		dev_crit(sma1303->dev, "Please check AMP status");
		dev_info(sma1303->dev, "STATUS1=0x%02X : STATUS2=0x%02X\n",
				over_temp, ocp_val);
		sma1303->last_over_temp = over_temp;
		sma1303->last_ocp_val = ocp_val;
	}

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	if (fault_val_flag && sma1303->fault_reg_flag) {
		int i, j = 0;
		char reg[256];
		char reg_str[12][50] = { };
		bool state;

		regmap_bulk_read(sma1303->regmap,
			SMA1303_00_SYSTEM_CTRL, reg, ARRAY_SIZE(reg));
		dev_crit(sma1303->dev, "\n");
		dev_crit(sma1303->dev, "sma1303 register values\n");
		dev_crit(sma1303->dev, "\t   0\t1\t2\t3\t4\t5\t6\t7\t8\t9\tA\tB\tC\tD\tE\tF\n");
		dev_crit(sma1303->dev, "\t  ---------------------------------------------------------------------------------------------------------\n");
		for (i = 0; i <= SMA1303_A7_CLK_MON; i++) {
			switch (i) {
			case SMA1303_00_SYSTEM_CTRL ... SMA1303_04_INPUT1_CTRL4:
			case SMA1303_09_OUTPUT_CTRL
				... SMA1303_0E_MUTE_VOL_CTRL:
			case SMA1303_10_SYSTEM_CTRL1
				... SMA1303_12_SYSTEM_CTRL3:
			case SMA1303_14_MODULATOR ... SMA1303_1B_BASS_SPK7:
			case SMA1303_23_COMP_LIM1 ... SMA1303_26_COMP_LIM4:
			case SMA1303_33_SDM_CTRL ... SMA1303_34_OTP_DATA1:
			case SMA1303_36_PROTECTION  ... SMA1303_38_OTP_TRM0:
			case SMA1303_3B_TEST1  ... SMA1303_3F_ATEST2:
			case SMA1303_8B_PLL_POST_N ... SMA1303_92_FDPEC_CTRL:
			case SMA1303_94_BOOST_CTRL1 ... SMA1303_97_BOOST_CTRL4:
			case SMA1303_A0_PAD_CTRL0 ... SMA1303_A7_CLK_MON:
				state = true;
				break;
			default:
				state = false;
			}
			if (state == false)
				sprintf(reg_str[j], "%sX\t", reg_str[j]);
			else
				sprintf(reg_str[j], "%s%02X\t",
					reg_str[j], reg[i]);

			if (i%16 == 15 || i == SMA1303_A7_CLK_MON) {
				dev_crit(sma1303->dev, "%X  |%s",
					j, reg_str[j]);
				j++;
			}
		}

		for (i = SMA1303_FA_STATUS1-10;
				i <= SMA1303_FF_DEVICE_INDEX; i++) {
			switch (i) {
			case SMA1303_FA_STATUS1 ... SMA1303_FB_STATUS2:
			case SMA1303_FF_DEVICE_INDEX:
				state = true;
				break;
			default:
				state = false;
			}
			if (state == false)
				sprintf(reg_str[j], "%sX\t", reg_str[j]);
			else
				sprintf(reg_str[j], "%s%02X\t",
					reg_str[j], reg[i]);

			if (i == SMA1303_FF_DEVICE_INDEX) {
				dev_crit(sma1303->dev, "F  |%s",  reg_str[j]);
				j++;
			}
		}
		sma1303->fault_reg_flag = false;
	}

	if (!(~over_temp & OT1_OK_STATUS) && !(~over_temp & OT2_OK_STATUS)
			&& !(ocp_val & OCP_SPK_STATUS)
			&& !(ocp_val & OCP_BST_STATUS)
			&& !(ocp_val & CLK_MON_STATUS)
			&& !(uvlo_val & UVLO_BST_STATUS)) {
		fault_val_flag = false;
		sma1303->fault_reg_flag = true;
	}

	mutex_unlock(&sma1303->lock);
}

#ifdef CONFIG_PM
static int sma1303_suspend(struct snd_soc_component *component)
{
	dev_info(component->dev, "%s\n", __func__);

	return 0;
}

static int sma1303_resume(struct snd_soc_component *component)
{
	dev_info(component->dev, "%s\n", __func__);

	return 0;
}
#else
#define sma1303_suspend NULL
#define sma1303_resume NULL
#endif

static int sma1303_reset(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret;
	unsigned int status, otp_stat;

	dev_info(component->dev, "%s\n", __func__);

	/* I2C Register Reset */
	regmap_update_bits(sma1303->regmap,
		SMA1303_00_SYSTEM_CTRL, RESETBYI2C_MASK, RESETBYI2C_RESET);

	ret = regmap_read(sma1303->regmap, SMA1303_FF_DEVICE_INDEX, &status);

	if (ret != 0)
		dev_err(sma1303->dev,
			"failed to read SMA1303_FA_STATUS1 : %d\n", ret);
	else
		sma1303->rev_num = status & REV_NUM_STATUS;

	if (sma1303->rev_num == REV_NUM_TV0)
		dev_info(component->dev, "SMA1303 Trimming Version 0\n");
	else if (sma1303->rev_num == REV_NUM_TV1)
		dev_info(component->dev, "SMA1303 Trimming Version 1\n");

	regmap_read(sma1303->regmap, SMA1303_FB_STATUS2, &otp_stat);

	if (((sma1303->rev_num == REV_NUM_TV0) &&
		((otp_stat & 0x0E) == OTP_STAT_OK_0)) ||
		((sma1303->rev_num != REV_NUM_TV0) &&
		((otp_stat & 0x0C) == OTP_STAT_OK_1)))
		dev_info(component->dev, "SMA1303 OTP Status Successful\n");
	else
		dev_info(component->dev, "SMA1303 OTP Status Fail\n");

	regmap_write(sma1303->regmap, SMA1303_00_SYSTEM_CTRL, 0x80);
	/* Volume control (-0.5dB) */
	regmap_write(sma1303->regmap, SMA1303_0A_SPK_VOL, sma1303->init_vol);

	/* BST off slope 2.6ns*/
	regmap_update_bits(sma1303->regmap,
		SMA1303_0B_BST_TEST, BST_OFF_SLOPE_MASK, BST_OFF_SLOPE_2_6ns);

	/* Shoot Through Protection disable */
	regmap_update_bits(sma1303->regmap,
		SMA1303_0C_BST_TEST1, EN_SH_PRT_MASK, EN_SH_PRT_DISABLE);

	/* VOL_SLOPE - Off, MUTE_SLOPE - Fast */
	regmap_write(sma1303->regmap, SMA1303_0E_MUTE_VOL_CTRL,	0x3F);

	/* Mono for one chip solution */
	regmap_write(sma1303->regmap, SMA1303_10_SYSTEM_CTRL1, 0x04);

	if (sma1303->stereo_two_chip == true) {
		/* MONO MIX Off */
		regmap_update_bits(sma1303->regmap,
		SMA1303_11_SYSTEM_CTRL2, MONOMIX_MASK, MONOMIX_OFF);
	} else {
		/* MONO MIX ON */
		regmap_update_bits(sma1303->regmap,
		SMA1303_11_SYSTEM_CTRL2, MONOMIX_MASK, MONOMIX_ON);
	}

	/* Delay control between OUTA and OUTB
	 * with main clock duty cycle
	 */
	regmap_write(sma1303->regmap, SMA1303_14_MODULATOR, 0x5c);

	/* Enable test registers */
	regmap_write(sma1303->regmap, SMA1303_3B_TEST1, 0x5A);

	/* Stereo idle noise improvement : Improved idle noise by
	 * asynchronizing the PWM waveform of Left and Right
	 */
	regmap_update_bits(sma1303->regmap, SMA1303_3C_TEST2,
		SDM_SYNC_DIS_MASK, SDM_SYNC_DISABLE);

	/* Low power mode operation, SPK output frequency - 410kHz,
	 * Thermal adjust - 150C, 110C
	 */
	regmap_update_bits(sma1303->regmap, SMA1303_3F_ATEST2,
			SPK_OUT_FREQ_MASK, SPK_OUT_FREQ_410K);
	regmap_update_bits(sma1303->regmap, SMA1303_3F_ATEST2,
			LOW_POWER_MODE_MASK, LOW_POWER_MODE_ENABLE);

	/* Post gain -0.625dB, Postscaler Enable
	 */
	regmap_write(sma1303->regmap, SMA1303_90_POSTSCALER, 0x26);

	/* Attack level - 0.25FS, Release time - 40ms(48kHz)
	 */
	regmap_write(sma1303->regmap, SMA1303_91_CLASS_G_CTRL, 0x42);

	/* Filtered VDD gain control 3.1V */
	regmap_write(sma1303->regmap, SMA1303_92_FDPEC_CTRL, 0xE0);
	sma1303->flt_vdd_gain_status = (FLT_VDD_GAIN_3P10 >> 4);

	/* Trimming of switching frequency - 2.0MHz,
	 * Trimming of ramp compensation - 2.94A/us
	 */
	regmap_write(sma1303->regmap, SMA1303_94_BOOST_CTRL1, 0x35);

	/* Apply tuning values of PWM slope control and
	 * PWM dead time control
	 * SPK_SLOPE - 2'b10, SPK_DEAD_TIME - 4'b0111
	 */
	regmap_write(sma1303->regmap, SMA1303_37_SLOPE_CTRL, 0x27);

	/* Trimming of over current limit - 2.0A,
	 * Trimming of loop compensation
	 * - PI compensation, 120pF I-gain, 0.5Mohm P-gain
	 */
	regmap_write(sma1303->regmap, SMA1303_95_BOOST_CTRL2, 0x0C);

	/* boost voltage - 6.0V, minimum on-time - 80ns
	 */
	regmap_write(sma1303->regmap, SMA1303_97_BOOST_CTRL4,	0x95);
	sma1303->bst_vol_lvl_status = (TRM_VBST_6P0 >> 2);

	/* Trimming of driver deadtime - 10.0ns, switching slew - 2.6ns */
	regmap_write(sma1303->regmap, SMA1303_96_BOOST_CTRL3, 0x42);

	/* PLL Lock disable */
	regmap_update_bits(sma1303->regmap,
			SMA1303_A2_TOP_MAN1, PLL_LOCK_SKIP_MASK,
			PLL_LOCK_DISABLE);

	dev_info(component->dev,
		"%s init_vol is 0x%x\n", __func__, sma1303->init_vol);

	return 0;
}

static int sma1303_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *dapm_widget_str = NULL;
	int prefix_len = 0, str_max = 30;

	dev_info(component->dev, "%s\n", __func__);

	if (component->name_prefix != NULL) {
		dev_info(component->dev, "%s : component name prefix - %s\n",
			__func__, component->name_prefix);

		prefix_len = strlen(component->name_prefix);
		dapm_widget_str = kzalloc(prefix_len + str_max, GFP_KERNEL);

		if (!dapm_widget_str) {
			kfree(dapm_widget_str);
			return -ENOMEM;
		}

		strcpy(dapm_widget_str, component->name_prefix);
		strcat(dapm_widget_str, " Playback");

		snd_soc_dapm_ignore_suspend(dapm, dapm_widget_str);

		memset(dapm_widget_str + prefix_len, 0, str_max);

		strcpy(dapm_widget_str, component->name_prefix);
		strcat(dapm_widget_str, " SPK");

		snd_soc_dapm_ignore_suspend(dapm, dapm_widget_str);

		kfree(dapm_widget_str);
	} else {
		snd_soc_dapm_ignore_suspend(dapm, "Playback");
		snd_soc_dapm_ignore_suspend(dapm, "SPK");
	}

	snd_soc_dapm_sync(dapm);

	sma1303_reset(component);

	return 0;
}

static void sma1303_remove(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	dev_info(component->dev, "%s\n", __func__);

	cancel_delayed_work_sync(&sma1303->check_fault_work);
	sma1303_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static const struct snd_soc_component_driver sma1303_component = {
	.probe = sma1303_probe,
	.remove = sma1303_remove,
	.suspend = sma1303_suspend,
	.resume = sma1303_resume,
	.controls = sma1303_snd_controls,
	.num_controls = ARRAY_SIZE(sma1303_snd_controls),
	.dapm_widgets = sma1303_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sma1303_dapm_widgets),
	.dapm_routes = sma1303_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sma1303_audio_map),
};

const struct regmap_config sma_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SMA1303_FF_DEVICE_INDEX,
	.readable_reg = sma1303_readable_register,
	.writeable_reg = sma1303_writeable_register,
	.volatile_reg = sma1303_volatile_register,

	.cache_type = REGCACHE_NONE,
	.reg_defaults = sma1303_reg_def,
	.num_reg_defaults = ARRAY_SIZE(sma1303_reg_def),
};

static ssize_t check_fault_period_show(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int rc;

	rc = (int)snprintf(buf, PAGE_SIZE,
			"%ld\n", sma1303->check_fault_period);

	return (ssize_t)rc;
}

static ssize_t check_fault_period_store(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int ret;

	ret = kstrtol(buf, 10, &sma1303->check_fault_period);

	if (ret)
		return -EINVAL;

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(check_fault_period);

static ssize_t check_fault_status_show(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int rc;

	rc = (int)snprintf(buf, PAGE_SIZE,
			"%ld\n", sma1303->check_fault_status);

	return (ssize_t)rc;
}

static ssize_t check_fault_status_store(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int ret;

	ret = kstrtol(buf, 10, &sma1303->check_fault_status);

	if (ret)
		return -EINVAL;

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(check_fault_status);

static struct attribute *sma1303_attr[] = {
	&dev_attr_check_fault_period.attr,
	&dev_attr_check_fault_status.attr,
	NULL,
};

static struct attribute_group sma1303_attr_group = {
	.attrs = sma1303_attr,
};

static int sma1303_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct sma1303_priv *sma1303;
	struct device_node *np = client->dev.of_node;
	int ret;
	u32 value;
	unsigned int device_info;

	dev_info(&client->dev, "%s is here. Driver version REV013\n", __func__);

	sma1303 = devm_kzalloc(&client->dev, sizeof(struct sma1303_priv),
							GFP_KERNEL);

	if (!sma1303)
		return -ENOMEM;

	sma1303->regmap = devm_regmap_init_i2c(client, &sma_i2c_regmap);

	if (IS_ERR(sma1303->regmap)) {
		ret = PTR_ERR(sma1303->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n", ret);

		if (sma1303->regmap)
			regmap_exit(sma1303->regmap);

		devm_kfree(&client->dev, sma1303);

		return ret;
	}

	if (np) {
		if (!of_property_read_u32(np, "init-vol", &value)) {
			dev_info(&client->dev,
				"init_vol is 0x%x from DT\n", value);
			sma1303->init_vol = value;
		} else {
			dev_info(&client->dev,
				"init_vol is set with 0x31(-0.5dB)\n");
			sma1303->init_vol = 0x31;
		}
		if (of_property_read_bool(np, "stereo-two-chip")) {
			dev_info(&client->dev, "Stereo for two chip solution\n");
				sma1303->stereo_two_chip = true;
		} else {
			dev_info(&client->dev, "Mono for one chip solution\n");
				sma1303->stereo_two_chip = false;
		}
		if (of_property_read_bool(np, "impossible-bst-ctrl")) {
			dev_info(&client->dev, "Boost control setting is not possible\n");
				sma1303->impossible_bst_ctrl = true;
		} else {
			dev_info(&client->dev, "Boost control setting is possible\n");
				sma1303->impossible_bst_ctrl = false;
		}
		if (!of_property_read_u32(np, "sys-clk-id", &value)) {
			switch (value) {
			case SMA1303_EXTERNAL_CLOCK_19_2:
			case SMA1303_EXTERNAL_CLOCK_24_576:
			case SMA1303_PLL_CLKIN_MCLK:
				dev_info(&client->dev, "MCLK is not supported\n");
				break;
			case SMA1303_PLL_CLKIN_BCLK:
				dev_info(&client->dev,
				"Take an BCLK(SCK) and covert it to an internal PLL for use\n");
				break;
			default:
				dev_err(&client->dev,
					"Invalid sys-clk-id: %d\n", value);
				return -EINVAL;
			}
			sma1303->sys_clk_id = value;
		} else {
			dev_info(&client->dev, "Use the internal PLL clock by default\n");
			sma1303->sys_clk_id = SMA1303_PLL_CLKIN_BCLK;
		}
	} else {
		dev_err(&client->dev,
			"device node initialization error\n");
		devm_kfree(&client->dev, sma1303);
		return -ENODEV;
	}

	ret = regmap_read(sma1303->regmap,
		SMA1303_FF_DEVICE_INDEX, &device_info);

	if ((ret != 0) || ((device_info & 0xF8) != DEVICE_ID)) {
		dev_err(&client->dev, "device initialization error (%d 0x%02X)",
				ret, device_info);
		/* devm_kfree(&client->dev, sma1303);
		 * return -ENODEV;
		 */
	}
	dev_info(&client->dev, "chip version 0x%02X\n", device_info);

	/* set initial value as normal AMP IC status */
	sma1303->last_over_temp = 0xC0;
	sma1303->last_ocp_val = 0x0A;
	sma1303->tsdw_cnt = 0;
	sma1303->cur_vol = sma1303->init_vol;
	sma1303->last_bclk = 0;

	INIT_DELAYED_WORK(&sma1303->check_fault_work,
		sma1303_check_fault_worker);

	mutex_init(&sma1303->lock);
	sma1303->check_fault_period = CHECK_PERIOD_TIME;

	sma1303->devtype = id->driver_data;
	sma1303->dev = &client->dev;
	sma1303->kobj = &client->dev.kobj;

	i2c_set_clientdata(client, sma1303);

	sma1303->force_amp_power_down = false;
	sma1303->amp_power_status = false;
	sma1303->check_fault_status = true;
	sma1303->fault_reg_flag = true;
	sma1303->pll_matches = sma1303_pll_matches;
	sma1303->num_of_pll_matches =
		ARRAY_SIZE(sma1303_pll_matches);

	ret = devm_snd_soc_register_component(&client->dev,
			&sma1303_component, sma1303_dai, 1);

	if (ret) {
		dev_err(&client->dev, "Failed to register component");
		snd_soc_unregister_component(&client->dev);

		if (sma1303)
			devm_kfree(&client->dev, sma1303);

		return ret;
	}

	/* Create sma1303 sysfs attributes */
	sma1303->attr_grp = &sma1303_attr_group;
	ret = sysfs_create_group(sma1303->kobj, sma1303->attr_grp);

	if (ret) {
		dev_err(&client->dev,
			"failed to create attribute group [%d]\n", ret);
		sma1303->attr_grp = NULL;
	}

	return ret;
}

static int sma1303_i2c_remove(struct i2c_client *client)
{
	struct sma1303_priv *sma1303 =
		(struct sma1303_priv *) i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);

	cancel_delayed_work_sync(&sma1303->check_fault_work);

	snd_soc_unregister_component(&client->dev);

	if (sma1303)
		devm_kfree(&client->dev, sma1303);

	return 0;
}

static const struct i2c_device_id sma1303_i2c_id[] = {
	{"sma1303", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sma1303_i2c_id);

static const struct of_device_id sma1303_of_match[] = {
	{ .compatible = "siliconmitus,sma1303", },
	{ }
};
MODULE_DEVICE_TABLE(of, sma1303_of_match);

static struct i2c_driver sma1303_i2c_driver = {
	.driver = {
		.name = "sma1303",
		.of_match_table = sma1303_of_match,
	},
	.probe = sma1303_i2c_probe,
	.remove = sma1303_i2c_remove,
	.id_table = sma1303_i2c_id,
};

static int __init sma1303_init(void)
{
	int ret;

	ret = i2c_add_driver(&sma1303_i2c_driver);

	if (ret)
		pr_err("Failed to register sma1303 I2C driver: %d\n", ret);

	return ret;
}

static void __exit sma1303_exit(void)
{
	i2c_del_driver(&sma1303_i2c_driver);
}

module_init(sma1303_init);
module_exit(sma1303_exit);

MODULE_DESCRIPTION("ALSA SoC SMA1303 driver");
MODULE_AUTHOR("Justin, <jeongjin.kim@siliconmitus.com>");
MODULE_AUTHOR("KS Jo, <kiseok.jo@irondevice.com>");
MODULE_LICENSE("GPL v2");
