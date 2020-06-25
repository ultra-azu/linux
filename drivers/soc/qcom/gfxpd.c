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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>

struct msm_gfxpd {
	struct device		*dev;
	struct mutex		lock;
	struct generic_pm_domain genpd;
	bool			enabled;
	u32			corner;
	u32			num_pds;
	struct device		*pds[];
};

static int msm_gfxpd_power_on(struct generic_pm_domain *domain)
{
	struct msm_gfxpd *gfxpd = container_of(domain, struct msm_gfxpd, genpd);
	int ret, i;

	mutex_lock(&gfxpd->lock);

	if (gfxpd->enabled)
		goto enabled;

	for (i = 0; i < gfxpd->num_pds; i++) {
		if (i == 0)
			dev_pm_genpd_set_performance_state(gfxpd->pds[i], gfxpd->corner);

		ret = pm_runtime_get_sync(gfxpd->pds[i]);
		if (ret < 0)
			goto fail;

		pm_runtime_forbid(gfxpd->pds[i]);
	}

	gfxpd->enabled = true;

enabled:
	mutex_unlock(&gfxpd->lock);

	return 0;

fail:
	for (i--; i >= 0; i--) {
		pm_runtime_allow(gfxpd->pds[i]);
		pm_runtime_put(gfxpd->pds[i]);
	}
	dev_pm_genpd_set_performance_state(gfxpd->pds[0], 0);
	mutex_unlock(&gfxpd->lock);
	return ret;
}

static int msm_gfxpd_power_off(struct generic_pm_domain *domain)
{
	struct msm_gfxpd *gfxpd = container_of(domain, struct msm_gfxpd, genpd);
	int i;

	mutex_lock(&gfxpd->lock);

	if (!gfxpd->enabled)
		goto disabled;

	for (i = gfxpd->num_pds - 1; i >= 0; i--) {
		pm_runtime_allow(gfxpd->pds[i]);
		pm_runtime_put(gfxpd->pds[i]);
		if (i == 0)
			dev_pm_genpd_set_performance_state(gfxpd->pds[i], 0);
	}

	gfxpd->enabled = false;

disabled:
	mutex_unlock(&gfxpd->lock);

	return 0;
}

static int msm_gfxpd_set_performance_state(struct generic_pm_domain *domain,
				     unsigned int corner)
{
	struct msm_gfxpd *gfxpd = container_of(domain, struct msm_gfxpd, genpd);

	mutex_lock(&gfxpd->lock);

	gfxpd->corner = corner;

	if (gfxpd->enabled && gfxpd->num_pds)
		dev_pm_genpd_set_performance_state(gfxpd->pds[0], corner);

	mutex_unlock(&gfxpd->lock);

	return 0;
}

static int msm_gfxpd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm_gfxpd *gfxpd;
	int ret, num_pds, i;

	num_pds = of_property_count_strings(dev->of_node, "power-domain-names");
	if (num_pds <= 0)
		return -EINVAL;

	gfxpd = devm_kzalloc(dev, sizeof(*gfxpd) + num_pds * sizeof(gfxpd->pds[0]),
			     GFP_KERNEL);

	if (!gfxpd)
		return -ENOMEM;

	mutex_init(&gfxpd->lock);

	platform_set_drvdata(pdev, gfxpd);

	for (i = 0; i < num_pds; i++) {
		gfxpd->pds[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR_OR_NULL(gfxpd->pds[i]))
			goto fail;
	}

	gfxpd->dev = &pdev->dev;
	gfxpd->enabled = false;
	gfxpd->num_pds = num_pds;
	gfxpd->genpd.name = devm_kstrdup_const(dev, dev->of_node->full_name,
					  GFP_KERNEL);
	if (!gfxpd->genpd.name)
		return -ENOMEM;

	gfxpd->genpd.power_on = msm_gfxpd_power_on;
	gfxpd->genpd.power_off = msm_gfxpd_power_off;
	gfxpd->genpd.set_performance_state = msm_gfxpd_set_performance_state;

	ret = pm_genpd_init(&gfxpd->genpd, NULL, true);
	if (ret)
		return ret;

	ret = of_genpd_add_provider_simple(dev->of_node, &gfxpd->genpd);
	if (ret)
		return ret;

	return 0;

fail:
	for (i--; i >= 0; i--)
		dev_pm_domain_detach(gfxpd->pds[i], false);
	return ret;
}

static int msm_gfxpd_remove(struct platform_device *pdev)
{
	struct msm_gfxpd *gfxpd = platform_get_drvdata(pdev);
	int i;

	msm_gfxpd_power_off(&gfxpd->genpd);

	for (i = 0; i < gfxpd->num_pds; i++)
		dev_pm_domain_detach(gfxpd->pds[i], false);

	of_genpd_del_provider(pdev->dev.of_node);

	pm_genpd_remove(&gfxpd->genpd);

	return 0;
}

static const struct of_device_id msm_gfxpd_match_table[] = {
	{ .compatible = "qcom-gfxpd", },
	{}
};

static struct platform_driver msm_gfxpd_driver = {
	.driver	= {
		.name		= "qcom-gfxpd",
		.of_match_table = msm_gfxpd_match_table,
		.owner		= THIS_MODULE,
	},
	.probe	= msm_gfxpd_probe,
	.remove	= msm_gfxpd_remove,
};

module_platform_driver(msm_gfxpd_driver);

MODULE_DESCRIPTION("QCOM GFX Power domain driver");
MODULE_LICENSE("GPL v2");
