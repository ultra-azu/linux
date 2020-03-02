/*
 * Copyright (c) 2015-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/io.h>

#include "clk-alpha-pll.h"
#include "clk-rcg.h"
#include "common.h"
#include <linux/clk.h>

#define APCS_ALIAS1_CMD_RCGR		0xb011050
#define APCS_ALIAS1_CFG_OFF		0x4
#define APCS_ALIAS1_CORE_CBCR_OFF	0x8

enum {
	CLK_C0,
	CLK_C1,
	CLK_CCI,
	CLK_HFPLL,
	CLK_MAX,

	CLK_GPLL0,
	CLK_XO,
};

static const u8 apcs_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL]		= 0x08,
	[PLL_OFF_ALPHA_VAL]	= 0x10,
	[PLL_OFF_USER_CTL]	= 0x18,
	[PLL_OFF_CONFIG_CTL]	= 0x20,
	[PLL_OFF_CONFIG_CTL_U]	= 0x24,
	[PLL_OFF_STATUS]	= 0x28,
	[PLL_OFF_TEST_CTL]	= 0x30,
	[PLL_OFF_TEST_CTL_U]	= 0x34,
};

static const struct parent_map c0_c1_cci_parent_map[] = {
	{ CLK_XO, 0 },
	/* ??? */
	{ CLK_GPLL0, 4 },
	{ CLK_HFPLL, 5 },
};

static const char * const c0_c1_cci_parent_names[] = {
	"xo_a_clk_src",
	"gpll0_early",
	"apcs-hfpll",
};

static struct clk_alpha_pll apcs_hfpll = {
	.offset = 0x105000,
	.regs = apcs_pll_regs,
	.clkr.hw.init = CLK_HW_INIT("apcs-hfpll", "xo",
			&clk_alpha_pll_ops, 0)
};

static const struct freq_tbl c0_c1_freq_tbl [] = {
	//F(19200000, CLK_XO, 1, 0, 0),
	F(307200000, CLK_HFPLL, 2, 0, 0),
	F(614400000, CLK_HFPLL, 1, 0, 0),
	F(800000000, CLK_GPLL0, 1, 0, 0),
	{}
};

static const struct freq_tbl cci_freq_tbl [] = {
	F(19200000, CLK_XO, 1, 0, 0),
	{}
};

static struct clk_rcg2 apcs_c0_clk = {
	.cmd_rcgr = 0x100050,
	.hid_width = 5,
	.freq_tbl = c0_c1_freq_tbl,
	.parent_map = c0_c1_cci_parent_map,
	.clkr.hw.init = CLK_HW_INIT_PARENTS("apcs-c0-clk", c0_c1_cci_parent_names,
			&clk_rcg2_ops, CLK_IGNORE_UNUSED)

};

static struct clk_rcg2 apcs_c1_clk = {
	.cmd_rcgr = 0x000050,
	.hid_width = 5,
	.freq_tbl = c0_c1_freq_tbl,
	.parent_map = c0_c1_cci_parent_map,
	.clkr.hw.init = CLK_HW_INIT_PARENTS("apcs-c1-clk", c0_c1_cci_parent_names,
			&clk_rcg2_ops, CLK_IGNORE_UNUSED)
};

static struct clk_rcg2 apcs_cci_clk = {
	.cmd_rcgr = 0x1c0050,
	.hid_width = 5,
	.freq_tbl = cci_freq_tbl,
	.parent_map = c0_c1_cci_parent_map,
	.clkr.hw.init = CLK_HW_INIT_PARENTS("apcs-cci-clk", c0_c1_cci_parent_names,
			&clk_rcg2_ops, CLK_IS_CRITICAL )
};

static int apcs_msm8953_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct clk_hw_onecell_data *clk_data;
	struct clk_regmap *regmap_clks [] = {
		&apcs_hfpll.clkr, &apcs_c0_clk.clkr,
		&apcs_c1_clk.clkr, &apcs_cci_clk.clkr,
		(struct clk_regmap*) NULL,
	};
	struct clk_regmap **rclk = &regmap_clks[0];
	struct regmap_config config = {
		.reg_bits	= 32,
		.reg_stride	= 4,
		.val_bits	= 32,
		.fast_io	= true,
	};
	struct alpha_pll_config pll_config = {
		.config_ctl_val		= 0x200d4828,
		.config_ctl_hi_val	= 0x6,
		.test_ctl_val		= 0x1c000000,
		.test_ctl_hi_val	= 0x4000,
		.main_output_mask	= BIT(0),
		.early_output_mask	= BIT(3),
		.pre_div_mask		= BIT(12),
		.post_div_val		= BIT(8),
		.post_div_mask		= GENMASK(9, 8),
	};
	void __iomem *base;
	int ret;
	struct resource *res;

	clk_data = (struct clk_hw_onecell_data*) devm_kzalloc(dev,
			struct_size (clk_data, hws, CLK_MAX), GFP_KERNEL);
	if (IS_ERR(clk_data))
		return -ENOMEM;

	clk_data->num = CLK_MAX;
	clk_data->hws[CLK_C0]	= &apcs_c0_clk.clkr.hw;
	clk_data->hws[CLK_C1]	= &apcs_c1_clk.clkr.hw;
	clk_data->hws[CLK_CCI]	= &apcs_cci_clk.clkr.hw;
	clk_data->hws[CLK_HFPLL]= &apcs_hfpll.clkr.hw;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	config.max_register = resource_size(res) - 4;

	regmap = devm_regmap_init_mmio(dev, base, &config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (dev_get_regmap(dev, NULL) != regmap) {
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}


	for (ret = 0; *rclk && !ret; rclk++) {
		ret = devm_clk_register_regmap(dev, *rclk);
	}

	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		return ret;
	}

	clk_alpha_pll_configure(&apcs_hfpll, regmap, &pll_config);

	ret = clk_set_rate(apcs_hfpll.clkr.hw.clk, 614400000);
	if (ret) {
		dev_err(dev, "failed to set pll rate: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(apcs_hfpll.clkr.hw.clk);
	if (ret) {
		dev_err(dev, "failed to enable pll: %d\n", ret);
		return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret) {
		dev_err(dev, "failed to add clock provider: %d\n", ret);
	}

	return ret;
}

static const struct of_device_id apcs_msm8953_match_table[] = {
	{ .compatible = "qcom,apcs-msm8953" },
	{},
};

static struct platform_driver apcs_msm8953_driver = {
	.probe = apcs_msm8953_probe,
	.driver = {
		.name = "qcom-apcs-msm8953-clk",
		.of_match_table = apcs_msm8953_match_table,
		.owner = THIS_MODULE,
	},
};

static int __init msm_apcs_init(void)
{
	return platform_driver_register(&apcs_msm8953_driver);
}

static int __init cpu_clock_pwr_init(void)
{
	void __iomem  *base;
	int regval = 0;
	struct device_node *ofnode = of_find_compatible_node(NULL, NULL,
						"qcom,apcs-msm8953");
	if (!ofnode)
		return 0;

	/* Initialize the PLLs */
	base = ioremap(APCS_ALIAS1_CMD_RCGR, SZ_8);
	regval = readl_relaxed(base);

	/* Source GPLL0 and at the rate of GPLL0 */
	regval = 0x401; /* source - 4, div - 1  */
	writel_relaxed(regval, base + APCS_ALIAS1_CFG_OFF);
	/* Make sure src sel and src div is set before update bit */
	mb();

	/* update bit */
	regval = readl_relaxed(base);
	regval |= BIT(0);
	writel_relaxed(regval, base);
	/* Make sure src sel and src div is set before update bit */
	mb();

	/* Enable the branch */
	regval =  readl_relaxed(base + APCS_ALIAS1_CORE_CBCR_OFF);
	regval |= BIT(0);
	writel_relaxed(regval, base + APCS_ALIAS1_CORE_CBCR_OFF);
	/* Branch enable should be complete */
	mb();
	iounmap(base);

	pr_info("Power cluster clocks configured\n");

	return 0;
}

arch_initcall(msm_apcs_init);
early_initcall(cpu_clock_pwr_init);
