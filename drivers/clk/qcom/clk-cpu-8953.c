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

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/clk.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-regmap-mux-div.h"

enum {
	APCC_CLK_C0,
	APCC_CLK_C1,
	APCC_CLK_CCI,
	APCC_CLK_HFPLL_C0,
	APCC_CLK_HFPLL_C1,
	APCC_CLK_HFPLL_CCI,
};

static struct clk_ops msm8953_hfpll_ops;
static struct clk_ops msm8953_mux_div_ops;
static struct clk_ops sdm632_mux_div_ops;

struct qcom_apcc_msm8953_data {
	struct qcom_cc_desc desc;
	struct alpha_pll_config *cpu_config;
	struct alpha_pll_config *cci_config;
	struct notifier_block nb;
};

static const u8 hfpll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL]		= 0x08,
	[PLL_OFF_ALPHA_VAL]	= 0x10,
	[PLL_OFF_USER_CTL]	= 0x18,
	[PLL_OFF_CONFIG_CTL]	= 0x20,
	[PLL_OFF_CONFIG_CTL_U]	= 0x24,
	[PLL_OFF_STATUS]	= 0x28,
	[PLL_OFF_TEST_CTL]	= 0x30,
	[PLL_OFF_TEST_CTL_U]	= 0x34,
};

static struct clk_alpha_pll common_c0_hfpll = {
	.offset = 0x105000,
	.regs = hfpll_regs,
	.clkr.hw.init = CLK_HW_INIT("apcc-hfpll-c0", "xo", &msm8953_hfpll_ops, 0)
};

static struct clk_alpha_pll sdm632_c1_hfpll = {
	.offset = 0x005000,
	.regs = hfpll_regs,
	.clkr.hw.init = CLK_HW_INIT("apcc-hfpll-c1", "xo", &msm8953_hfpll_ops, 0)
};

static struct clk_alpha_pll sdm632_cci_hfpll = {
	.offset = 0x1bf000,
	.regs = hfpll_regs,
	.clkr.hw.init = CLK_HW_INIT("apcc-hfpll-cci", "xo", &msm8953_hfpll_ops, 0)
};

static const struct clk_parent_data c1_parent_data[] = {
	{ .fw_name = "gpll", .name = "gpll0_early" },
	{ .fw_name = "pll1", .name = "apcc-hfpll-c1" },
};

static const struct clk_parent_data cci_parent_data[] = {
	{ .fw_name = "gpll", .name = "gpll0_early" },
	// { .fw_name = "ccipll", .name = "apcc-hfpll-cci" },
	// FIXME Dyna PLL is not supported
};

static const struct clk_parent_data c0_c1_cci_parent_data[] = {
	{ .fw_name = "gpll", .name = "gpll0_early" },
	{ .fw_name = "pll0", .name = "apcc-hfpll-c0" },
};

static const u32 apcc_mux_parent_map[] = { 4, 5 };
static const u32 apcc_mux_keep_rate_map[] = { 1, 0 };
static struct clk_regmap_mux_div common_apcc_c0_clk = {
	.reg_offset = 0x100050,
	.hid_width = 5,
	.src_width = 3,
	.src_shift = 8,
	.keep_rate_map = apcc_mux_keep_rate_map,
	.parent_map = apcc_mux_parent_map,
	.clkr = {
		.enable_reg = 0x100058,
		.enable_mask = BIT(0),
		.hw.init = CLK_HW_INIT_PARENTS_DATA("apcc-c0-clk", c0_c1_cci_parent_data,
			&msm8953_mux_div_ops, CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT)
	},
};

static struct clk_regmap_mux_div msm8953_apcc_c1_clk = {
	.reg_offset = 0x000050,
	.hid_width = 5,
	.src_width = 3,
	.src_shift = 8,
	.keep_rate_map = apcc_mux_keep_rate_map,
	.parent_map = apcc_mux_parent_map,
	.clkr = {
		.enable_reg = 0x000058,
		.enable_mask = BIT(0),
		.hw.init = CLK_HW_INIT_PARENTS_DATA("apcc-c1-clk", c0_c1_cci_parent_data,
			&msm8953_mux_div_ops, CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT)
	},
};

static struct clk_regmap_mux_div msm8953_apcc_cci_clk = {
	.reg_offset = 0x1c0050,
	.hid_width = 5,
	.src_width = 3,
	.src_shift = 8,
	.parent_map = apcc_mux_parent_map,
	.clkr = {
		.enable_reg = 0x1c0058,
		.enable_mask = BIT(0),
		.hw.init = CLK_HW_INIT_PARENTS_DATA("apcc-cci-clk", c0_c1_cci_parent_data,
			&msm8953_mux_div_ops, CLK_IGNORE_UNUSED | CLK_IS_CRITICAL)
	},
};

static struct clk_regmap_mux_div sdm632_apcc_c1_clk = {
	.reg_offset = 0x000050,
	.hid_width = 5,
	.src_width = 3,
	.src_shift = 8,
	.keep_rate_map = apcc_mux_keep_rate_map,
	.parent_map = apcc_mux_parent_map,
	.clkr = {
		.enable_reg = 0x000058,
		.enable_mask = BIT(0),
		.hw.init = CLK_HW_INIT_PARENTS_DATA("apcc-c1-clk", c1_parent_data,
			&sdm632_mux_div_ops, CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT),
	},
};

static struct clk_regmap_mux_div sdm632_apcc_cci_clk = {
	.reg_offset = 0x1c0050,
	.hid_width = 5,
	.src_width = 3,
	.src_shift = 8,
	.parent_map = apcc_mux_parent_map,
	.clkr = {
		.enable_reg = 0x1c0058,
		.enable_mask = BIT(0),
		.hw.init = CLK_HW_INIT_PARENTS_DATA("apcc-cci-clk", cci_parent_data,
			&sdm632_mux_div_ops, CLK_IGNORE_UNUSED | CLK_IS_CRITICAL),
	},
};

static struct alpha_pll_config msm8953_pll_config = {
	.config_ctl_val		= 0x200d4828,
	.config_ctl_hi_val	= 0x6,
	.test_ctl_val		= 0x1c000000,
	.test_ctl_hi_val	= 0x4000,
	.main_output_mask	= BIT(0),
	.early_output_mask	= BIT(3),
	.pre_div_val		= 0,
	.pre_div_mask		= BIT(12),
	.post_div_val		= BIT(8),
	.post_div_mask		= GENMASK(9, 8),
};

static struct alpha_pll_config sdm632_pll_config = {
	.config_ctl_val		= 0x200d4828,
	.config_ctl_hi_val	= 0x6,
	.test_ctl_val		= 0x1c000000,
	.test_ctl_hi_val	= 0x4000,
	.main_output_mask	= BIT(0),
	.early_output_mask	= BIT(3),
};

static struct alpha_pll_config sdm632_cci_pll_config = {
	.config_ctl_val		= 0x4001055b,
	.early_output_mask	= BIT(3),
};

struct regmap_config apcc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x1c0100,
};

static struct clk_regmap *qcom_apcc_msm8953_clocks[] = {
	[APCC_CLK_C0] = &common_apcc_c0_clk.clkr,
	[APCC_CLK_C1] = &msm8953_apcc_c1_clk.clkr,
	[APCC_CLK_CCI] = &msm8953_apcc_cci_clk.clkr,
	[APCC_CLK_HFPLL_C0] = &common_c0_hfpll.clkr,
};

static struct clk_regmap *qcom_apcc_sdm632_clocks[] = {
	[APCC_CLK_C0] = &common_apcc_c0_clk.clkr,
	[APCC_CLK_C1] = &sdm632_apcc_c1_clk.clkr,
	[APCC_CLK_CCI] = &sdm632_apcc_cci_clk.clkr,
	[APCC_CLK_HFPLL_C0] = &common_c0_hfpll.clkr,
	[APCC_CLK_HFPLL_C1] = &sdm632_c1_hfpll.clkr,
	[APCC_CLK_HFPLL_CCI] = &sdm632_cci_hfpll.clkr,
};

static struct qcom_apcc_msm8953_data apcc_msm8953_desc = {
	.desc = {
		.config = &apcc_regmap_config,
		.clks = qcom_apcc_msm8953_clocks,
		.num_clks = ARRAY_SIZE(qcom_apcc_msm8953_clocks),
	},
	.cpu_config = &msm8953_pll_config,
};

static struct qcom_apcc_msm8953_data apcc_sdm632_desc = {
	.desc = {
		.config = &apcc_regmap_config,
		.clks = qcom_apcc_sdm632_clocks,
		.num_clks = ARRAY_SIZE(qcom_apcc_sdm632_clocks),
	},
	.cpu_config = &sdm632_pll_config,
	.cci_config = &sdm632_cci_pll_config,
};

static int cci_mux_notifier(struct notifier_block *nb,
				unsigned long event,
				void *unused)
{
	struct qcom_apcc_msm8953_data *data = container_of(nb,
				struct qcom_apcc_msm8953_data, nb);

	long long c0 = clk_hw_get_rate(&data->desc.clks[APCC_CLK_C0]->hw);
	long long c1 = clk_hw_get_rate(&data->desc.clks[APCC_CLK_C1]->hw);
	long long cci_rate;

	if (event == PRE_RATE_CHANGE) {
		cci_rate = min(c0, c1);
	} else if (event == POST_RATE_CHANGE) {
		cci_rate = max(c0, c1);
	} else return 0;

	cci_rate = cci_rate * 2 / 5;

	if (cci_rate < 320000000)
		cci_rate = 320000000;

	clk_set_rate(data->desc.clks[APCC_CLK_CCI]->hw.clk, cci_rate);

	return 0;
}

static long qcom_hfpll_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	return rounddown(rate, *prate);
}

static int msm8953_apcs_mux_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_hw *other = (hw == &common_apcc_c0_clk.clkr.hw) ?
		               &msm8953_apcc_c1_clk.clkr.hw : &common_apcc_c0_clk.clkr.hw;

	if (hw != &msm8953_apcc_cci_clk.clkr.hw) {
		req->best_parent_hw = NULL;
		req->best_parent_rate = max(clk_hw_get_rate(other), req->rate);
	}
	return clk_regmap_mux_div_ops.determine_rate(hw, req);
}

static int sdm632_apcs_mux_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	if (hw != &sdm632_apcc_cci_clk.clkr.hw) {
		req->best_parent_hw = NULL;
		req->best_parent_rate = req->rate;
	}
	return clk_regmap_mux_div_ops.determine_rate(hw, req);
}

static int qcom_apcc_msm8953_probe(struct platform_device *pdev)
{
	struct qcom_apcc_msm8953_data *data;
	struct alpha_pll_config *cfg;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *base;
	int ret, i;

	data = (struct qcom_apcc_msm8953_data*) of_device_get_match_data(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, data->desc.config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	msm8953_hfpll_ops = clk_alpha_pll_huayra_ops;
	msm8953_hfpll_ops.round_rate = qcom_hfpll_round_rate;
	msm8953_mux_div_ops = sdm632_mux_div_ops = clk_regmap_mux_div_ops;
	msm8953_mux_div_ops.determine_rate = msm8953_apcs_mux_determine_rate;
	sdm632_mux_div_ops.determine_rate = sdm632_apcs_mux_determine_rate;
	data->nb.notifier_call = cci_mux_notifier;

	ret = qcom_cc_really_probe(pdev, &data->desc, regmap);
	if (ret)
		return ret;

	cfg = data->cpu_config;

	for (i = APCC_CLK_C0; i <= APCC_CLK_CCI; i++) {
		struct clk_regmap_mux_div *md = container_of(data->desc.clks[i],
				struct clk_regmap_mux_div, clkr);

		if (i < APCC_CLK_CCI)
			clk_notifier_register(md->clkr.hw.clk, &data->nb);

		mux_div_set_src_div(md, 4, (i == APCC_CLK_CCI) ? 5 : 2);
	}

	for (i = APCC_CLK_HFPLL_C0; i < data->desc.num_clks; i++) {
		if (i >= APCC_CLK_HFPLL_CCI)
			cfg = data->cci_config;

		if (!cfg)
			continue;

		clk_alpha_pll_configure(container_of(data->desc.clks[i],
					struct clk_alpha_pll, clkr),
					regmap, cfg);

		clk_set_rate(data->desc.clks[i]->hw.clk, 806400000);
		clk_prepare_enable(data->desc.clks[i]->hw.clk);
	}

	return 0;
}

static const struct of_device_id qcom_cpu_clk_msm8953_match_table[] = {
	{ .compatible = "qcom,msm8953-apcc", .data = &apcc_msm8953_desc },
	{ .compatible = "qcom,sdm632-apcc", .data = &apcc_sdm632_desc },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpu_clk_msm8953_match_table);

static struct platform_driver qcom_cpu_clk_msm8953_driver = {
	.probe = qcom_apcc_msm8953_probe,
	.driver = {
		.name = "qcom-msm8953-apcc",
		.of_match_table = qcom_cpu_clk_msm8953_match_table,
	},
};

static int __init qcom_apcc_msm8953_init(void)
{
	return platform_driver_register(&qcom_cpu_clk_msm8953_driver);
}
arch_initcall(qcom_apcc_msm8953_init);
