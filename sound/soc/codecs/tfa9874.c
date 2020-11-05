/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Based of tfa9872.c
 * Register definitions taken from tfa98xx kernel driver:
 * Copyright (C) 2014-2020 NXP Semiconductors, All Rights Reserved.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#define TFA9874_SYS_CTRL0		0x00
#define TFA9874_SYS_CTRL0_PWDN		BIT(0)
#define TFA9874_SYS_CTRL0_I2CR		BIT(1)
#define TFA9874_SYS_CTRL0_AMPE		BIT(3)

#define TFA9874_SYS_CTRL1		0x01
#define TFA9874_SYS_CTRL1_MANSCONF	BIT(2)
#define TFA9874_SYS_CTRL1_MANSAOOSC	BIT(4)

#define TFA9874_SYS_CTRL2		0x02
#define TFA9874_SYS_CTRL2_FRACTDEL	GENMASK(10, 5)
#define TFA9874_SYS_CTRL2_AUDFS		GENMASK(4, 0)

#define TFA9874_REVISIONNUMBER		0x03
#define TFA9874_CLK_GATING_CTRL		0x05
#define TFA9874_TDM_CFG0		0x20
#define TFA9874_TDM_CFG1		0x21
#define TFA9874_TDM_CFG2		0x22
#define TFA9874_TDM_CFG3		0x23
#define TFA9874_TDM_CFG6		0x26
#define TFA9874_TDM_CFG7		0x27
#define TFA9874_AUDIO_CTRL		0x51
#define TFA9874_AMP_CFG			0x52
#define TFA9874_KEY1_PWM_CFG		0x58
#define TFA9874_GAIN_ATT		0x61
#define TFA9874_LOW_NOISE_GAIN1		0x62
#define TFA9874_LOW_NOISE_GAIN2		0x63
#define TFA9874_MODE1_DETECTOR1		0x64
#define TFA9874_MODE1_DETECTOR2		0x65
#define TFA9874_TDM_SRC			0x68
#define TFA9874_CURSENSE_COMP		0x6f
#define TFA9874_DCDC_CTRL0		0x70
#define TFA9874_DCDC_CTRL1		0x71
#define TFA9874_DCDC_CTRL4		0x74
#define TFA9874_DCDC_CTRL5		0x75
#define TFA9874_CURR_SENSE_CTRL		0x80
#define TFA9874_KEY2_CS_CFG2		0x82
#define TFA9874_KEY2_CS_CFG3		0x83
#define TFA9874_KEY2_CS_CFG4		0x84
#define TFA9874_KEY2_CS_CFG5		0x85
#define TFA9874_MTPKEY1_REG		0xa0
#define TFA9874_MTPKEY2_REG		0xa1

struct tfa98xx_cfg_field {
	u16 reg;
	u16 mask;
	const char *prop_name;
};

static struct tfa98xx_cfg_field tfa9874_fields[] = {
	{ TFA9874_SYS_CTRL0,	    GENMASK( 4,  4), "dcdc-enable" },
	{ TFA9874_SYS_CTRL2,        TFA9874_SYS_CTRL2_FRACTDEL, "vi-fractional-delay" },
	{ TFA9874_TDM_CFG1,	    GENMASK( 3,  0), "tdm-slots" },
	{ TFA9874_TDM_CFG1,	    GENMASK( 8,  4), "tdm-slot-bits" },
	{ TFA9874_TDM_CFG2,	    GENMASK( 6,  2), "tdm-sample-size" },
	{ TFA9874_TDM_CFG3,	    GENMASK( 3,  3), "tdm-current-sense" },
	{ TFA9874_TDM_CFG3,	    GENMASK( 4,  4), "tdm-voltage-sense" },
	{ TFA9874_TDM_CFG3,	    GENMASK( 1,  1), "tdm-sink1-enable" },
	{ TFA9874_TDM_CFG6,	    GENMASK( 3,  0), "tdm-speaker-dcdc-slot" },
	{ TFA9874_TDM_CFG6,	    GENMASK( 7,  4), "tdm-dcdc-slot" },
	{ TFA9874_TDM_CFG6,	    GENMASK(15, 12), "tdm-current-sense-slot" },
	{ TFA9874_TDM_CFG7,	    GENMASK( 3,  0), "tdm-voltage-sense-slot" },
	{ TFA9874_AUDIO_CTRL,	    GENMASK( 5,  5), "bypass-hpf" },
	{ TFA9874_AUDIO_CTRL,	    GENMASK( 7,  7), "dpsa-enable" },
	{ TFA9874_AMP_CFG,	    GENMASK(12,  5), "amplifier-gain" },
	{ 0x53,			    GENMASK( 5,  5), "bypass-lowpower" },
	{ 0x56,			    GENMASK( 9,  9), "sel-pwm-delay-src" },
	{ TFA9874_KEY1_PWM_CFG,     GENMASK( 8,  4), "pwm-delay" },
	{ TFA9874_GAIN_ATT,	    GENMASK( 5,  2), "ctrl-attl" },
	{ TFA9874_GAIN_ATT,	    GENMASK( 9,  6), "tdm-speaker-gain" },
	{ TFA9874_LOW_NOISE_GAIN1,  GENMASK(15, 14), "low-noise-mode" },
	{ TFA9874_LOW_NOISE_GAIN2,  GENMASK(11,  6), "low-audio-hold-time" },
	{ TFA9874_MODE1_DETECTOR1,  GENMASK(15, 14), "lpm1-mode" },
	{ TFA9874_MODE1_DETECTOR2,  GENMASK(11,  6), "lpm1-hold-time" },
	{ TFA9874_TDM_SRC,	    GENMASK(2,   0), "tdm-source-mapping" },
	{ TFA9874_TDM_SRC,	    GENMASK(4,   3), "tdm-sense-a-val" },
	{ TFA9874_TDM_SRC,	    GENMASK(6,   5), "tdm-sense-b-val" },
	{ TFA9874_CURSENSE_COMP,    GENMASK(2,	 0), "cursense-comp-delay" },
	{ TFA9874_CURSENSE_COMP,    GENMASK(5,   5), "enable-cursense-comp" },
	{ TFA9874_CURSENSE_COMP,    GENMASK(9,   7), "pwms-clip-lvl" },
	{ TFA9874_DCDC_CTRL0,	    GENMASK(6,   3), "max-coil-current" },
	{ TFA9874_DCDC_CTRL0,	    GENMASK(8,   7), "slope-compensation-current" },
	{ TFA9874_DCDC_CTRL0,	    GENMASK(14, 14), "dcdcoff-mode" },
	{ 0x76,			    GENMASK(14,  9), "second-boost-voltage" }, //TODO: name with appropriate #define
	{ 0x76,			    GENMASK( 8,  3), "first-boost-voltage" },
	{ TFA9874_DCDC_CTRL4,	    GENMASK( 3,  3), "boost-track" },
	{ TFA9874_DCDC_CTRL4,	    GENMASK( 8,  4), "first-boost-trip-lvl" },
	{ TFA9874_DCDC_CTRL4,	    GENMASK(13,  9), "boost-hold-time" },
	{ TFA9874_DCDC_CTRL4,	    GENMASK(15, 15), "ignore-flag-voutcomp86" },
	{ TFA9874_DCDC_CTRL5,	    GENMASK( 7,  3), "second-boost-trip-lvl" },
	{ TFA9874_DCDC_CTRL5,	    GENMASK(12,  8), "boost-trip-lvl-track" },
	{ TFA9874_DCDC_CTRL5,	    GENMASK(15, 15), "enable-trip-hyst" },
	{ TFA9874_CURR_SENSE_CTRL,  GENMASK( 1,  0), "select-clk-cs" },
	{ TFA9874_KEY2_CS_CFG3,	    GENMASK( 5,  0), "cs-ktemp" },
	{ TFA9874_KEY2_CS_CFG3,	    GENMASK(10,  6), "cs-ktemp2" },
	{ TFA9874_KEY2_CS_CFG4,	    GENMASK( 4,  4), "cs-adc-nortz" },
	{ TFA9874_KEY2_CS_CFG4,	    GENMASK( 8,  5), "cs-adc-offset" },
	{ TFA9874_KEY2_CS_CFG5,     GENMASK( 1,  1), "cs-classd-trans-skip" },
	{ TFA9874_KEY2_CS_CFG5,	    GENMASK( 3,  3), "cs-inn-short" },
	{ TFA9874_KEY2_CS_CFG5,	    GENMASK( 4,  4), "cs-inp-short" },
	{ 0x88,			    GENMASK( 1,  0), "volsense-pwm-selection" },
	{ 0xb0,			    GENMASK( 5,  5), "bypass-otp" },
	{ 0xc4,			    GENMASK(13, 13), "test-boost-ocp" },
};

static int tfa9874_digital_mute(struct snd_soc_dai *codec_dai, int mute, int stream) {
	struct snd_soc_component *component = codec_dai->component;
	int val = mute ? 0 : TFA9874_SYS_CTRL0_AMPE;

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	snd_soc_component_update_bits(component, TFA9874_SYS_CTRL0,
						 TFA9874_SYS_CTRL0_AMPE, val);

	return 0;
}

static const struct snd_soc_dapm_widget tfa9874_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
	SND_SOC_DAPM_OUT_DRV_E("PWUP", TFA9874_SYS_CTRL0, 0, 1, NULL, 0, NULL, 0),
};

static const struct snd_soc_dapm_route tfa9874_dapm_routes[] = {
	{"PWUP", NULL, "HiFi Playback"},
	{"Speaker", NULL, "PWUP"},
};

static const struct snd_soc_component_driver tfa9874_component = {
	.dapm_widgets		= tfa9874_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tfa9874_dapm_widgets),
	.dapm_routes		= tfa9874_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tfa9874_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops tfa9874_dai_ops = {
	.mute_stream = tfa9874_digital_mute,
};

static struct snd_soc_dai_driver tfa9874_dai = {
	.name = "tfa9874-hifi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		.rates		= SNDRV_PCM_RATE_48000,
		.rate_min	= 48000,
		.rate_max	= 48000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &tfa9874_dai_ops,
};

static const struct regmap_config tfa9874_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
};

static const struct reg_sequence tfa9874a_defaults[] = {
	{ TFA9874_SYS_CTRL2,		0x22a8 },
	{ TFA9874_AUDIO_CTRL,		0x0020 },
	{ TFA9874_AMP_CFG,		0x57dc },
	{ TFA9874_KEY1_PWM_CFG,		0x16a4 },
	{ TFA9874_GAIN_ATT,		0x0110 },
	{ 0x66,				0x0701 }, //TODO: Name with appropriate #define
	{ TFA9874_CURSENSE_COMP,	0x00a3 },
	{ TFA9874_DCDC_CTRL0,		0x07f8 },
	{ 0x73,				0x0007 }, //TODO: Name with appropriate #define
	{ TFA9874_DCDC_CTRL4,		0x5068 },
	{ TFA9874_DCDC_CTRL5,		0x0d28 },
	{ TFA9874_KEY2_CS_CFG3,		0x0594 },
	{ TFA9874_KEY2_CS_CFG4,		0x0001 },
	{ TFA9874_KEY2_CS_CFG5,		0x0001 },
	{ 0x88,				0x0000 },  //TODO: Name with appropriate #define
	{ 0xc4,				0x2001 },
};

static const struct reg_sequence tfa9874b_defaults[] = {
	{ TFA9874_SYS_CTRL2,		0x22a8 },
	{ TFA9874_AUDIO_CTRL,		0x0020 },
	{ TFA9874_AMP_CFG,		0x57dc },
	{ TFA9874_KEY1_PWM_CFG,		0x16a4 },
	{ TFA9874_GAIN_ATT,		0x0110 },
	{ 0x66,				0x0701 }, //TODO: Name with appropriate #define
	{ TFA9874_CURSENSE_COMP,	0x00a3 },
	{ TFA9874_DCDC_CTRL0,		0x07f8 },
	{ 0x73,				0x0047 }, //TODO: Name with appropriate #define
	{ TFA9874_DCDC_CTRL4,		0x5068 },
	{ TFA9874_DCDC_CTRL5,		0x0d28 },
	{ TFA9874_KEY2_CS_CFG3,		0x0595 },
	{ TFA9874_KEY2_CS_CFG4,		0x0001 },
	{ TFA9874_KEY2_CS_CFG5,		0x0001 },
	{ 0x88,				0x0000 },  //TODO: Name with appropriate #define
	{ 0xc4,				0x2001 },
};

static const struct reg_sequence tfa9874c_defaults[] = {
	{ TFA9874_SYS_CTRL2,		0x22a8 },
	{ TFA9874_AUDIO_CTRL,		0x0020 },
	{ TFA9874_AMP_CFG,		0x57dc },
	{ TFA9874_KEY1_PWM_CFG,		0x16a4 },
	{ TFA9874_GAIN_ATT,		0x0110 },
	{ TFA9874_CURSENSE_COMP,	0x00a3 },
	{ TFA9874_DCDC_CTRL0,		0x07f8 },
	{ 0x73,				0x0047 }, //TODO: Name with appropriate #define
	{ TFA9874_DCDC_CTRL4,		0x5068 },
	{ TFA9874_DCDC_CTRL5,		0x0d28 },
	{ TFA9874_KEY2_CS_CFG3,		0x0595 },
	{ TFA9874_KEY2_CS_CFG4,		0x0001 },
	{ TFA9874_KEY2_CS_CFG5,		0x0001 },
	{ 0x88,				0x0000 },  //TODO: Name with appropriate #define
	{ 0xc4,				0x2001 },
};




static int tfa9874_i2c_probe(struct i2c_client *i2c) {
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	unsigned int val;
	const struct reg_sequence *seq;
	int ret, i, seq_len;

	regmap = devm_regmap_init_i2c(i2c, &tfa9874_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = regmap_read(regmap, TFA9874_REVISIONNUMBER, &val);
	if (ret)
		return ret;


	switch (val) {
		case 0x0a74:
			seq = tfa9874a_defaults;
			seq_len = ARRAY_SIZE(tfa9874a_defaults);
			break;
		case 0x0b74:
			seq = tfa9874b_defaults;
			seq_len = ARRAY_SIZE(tfa9874b_defaults);
			break;
		case 0x0c74:
			seq = tfa9874c_defaults;
			seq_len = ARRAY_SIZE(tfa9874c_defaults);
			break;
		default:
			dev_err(dev, "Chip not recognized\n");
			return -ENODEV;
	}

	/* Perform reset */
	regmap_write(regmap, TFA9874_SYS_CTRL0, TFA9874_SYS_CTRL0_I2CR);

	/* Unhide lock registers */
	regmap_write(regmap, 0x0f, 0x5a6b);

	/* Unlock key1 */
	regmap_read(regmap, 0xfb, &val);
	regmap_write(regmap, TFA9874_MTPKEY1_REG, val ^ 0x5a);

	/* Unlock key2 */
	regmap_update_bits(regmap, TFA9874_MTPKEY2_REG, 0xff, 0x5a);

	/* Hide lock registers */
	regmap_write(regmap, 0x0f, 0);

	ret = regmap_multi_reg_write(regmap, seq, seq_len);
	if (ret) {
		dev_err(dev, "failed to initialize registers: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(tfa9874_fields); ++i) {
		struct tfa98xx_cfg_field *field = &tfa9874_fields[i];
		if(of_property_read_u32(dev->of_node, field->prop_name, &val))
			continue;

		regmap_update_bits(regmap,
				   field->reg,
				   field->mask,
				   val << (ffs(field->mask) - 1));

		dev_info(dev, "Written %d to %s\n", val << (ffs(field->mask) - 1), field->prop_name);
	}

		/*regmap_update_bits(regmap, TFA9874_SYS_CTRL2,
				   ~(u16)TFA9874_SYS_CTRL2_FRACTDEL, 0x2dc8); */

	/* Turn on this thing */
	regmap_update_bits(regmap, TFA9874_SYS_CTRL0,
				   TFA9874_SYS_CTRL0_PWDN, 0);

	regmap_update_bits(regmap, TFA9874_SYS_CTRL1,
				   TFA9874_SYS_CTRL1_MANSCONF,
				   TFA9874_SYS_CTRL1_MANSCONF);

	return devm_snd_soc_register_component(dev, &tfa9874_component,
						    &tfa9874_dai, 1);
}

static const struct of_device_id tfa9874_of_match[] = {
	{ .compatible = "nxp,tfa9874", },
	{ }
};
MODULE_DEVICE_TABLE(of, tfa9874_of_match);

static struct i2c_driver tfa9874_i2c_driver = {
	.driver = {
		.name = "tfa9874",
		.of_match_table = tfa9874_of_match,
	},
	.probe_new = tfa9874_i2c_probe,
};
module_i2c_driver(tfa9874_i2c_driver);

MODULE_DESCRIPTION("ASoC NXP Semiconductors TFA9874 driver");
MODULE_LICENSE("GPL v2");
