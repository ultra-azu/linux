/*
 * sm5708.c - mfd core driver for the SM5708
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sm5708.h>
#include <linux/regulator/machine.h>
#include <linux/err.h>

#if defined(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#endif /* CONFIG_OF */
#define MFD_DEV_NAME "sm5708-mfd"


static const struct regmap_irq sm5708_irqs[] = {
	REGMAP_IRQ_REG(SM5708_VBUSPOK_IRQ,     0,  BIT(0)),
	REGMAP_IRQ_REG(SM5708_VBUSUVLO_IRQ,    0,  BIT(1)),
	REGMAP_IRQ_REG(SM5708_VBUSOVP_IRQ,     0,  BIT(2)),
	REGMAP_IRQ_REG(SM5708_VBUSLIMIT_IRQ,   0,  BIT(3)),

	REGMAP_IRQ_REG(SM5708_AICL_IRQ,        1,  BIT(0)),
	REGMAP_IRQ_REG(SM5708_BATOVP_IRQ,      1,  BIT(1)),
	REGMAP_IRQ_REG(SM5708_NOBAT_IRQ,       1,  BIT(2)),
	REGMAP_IRQ_REG(SM5708_CHGON_IRQ,       1,  BIT(3)),
	REGMAP_IRQ_REG(SM5708_Q4FULLON_IRQ,    1,  BIT(4)),
	REGMAP_IRQ_REG(SM5708_TOPOFF_IRQ,      1,  BIT(5)),
	REGMAP_IRQ_REG(SM5708_DONE_IRQ,        1,  BIT(6)),
	REGMAP_IRQ_REG(SM5708_WDTMROFF_IRQ,    1,  BIT(7)),

	REGMAP_IRQ_REG(SM5708_THEMREG_IRQ,     2,  BIT(0)),
	REGMAP_IRQ_REG(SM5708_THEMSHDN_IRQ,    2,  BIT(1)),
	REGMAP_IRQ_REG(SM5708_OTGFAIL_IRQ,     2,  BIT(2)),
	REGMAP_IRQ_REG(SM5708_DISLIMIT_IRQ,    2,  BIT(3)),
	REGMAP_IRQ_REG(SM5708_PRETMROFF_IRQ,   2,  BIT(4)),
	REGMAP_IRQ_REG(SM5708_FASTTMROFF_IRQ,  2,  BIT(5)),
	REGMAP_IRQ_REG(SM5708_LOWBATT_IRQ,     2,  BIT(6)),
	REGMAP_IRQ_REG(SM5708_nENQ4_IRQ,       2,  BIT(7)),

	REGMAP_IRQ_REG(SM5708_FLED1SHORT_IRQ,  3,  BIT(0)),
	REGMAP_IRQ_REG(SM5708_FLED1OPEN_IRQ,   3,  BIT(1)),
	REGMAP_IRQ_REG(SM5708_FLED2SHORT_IRQ,  3,  BIT(2)),
	REGMAP_IRQ_REG(SM5708_FLED2OPEN_IRQ,   3,  BIT(3)),
	REGMAP_IRQ_REG(SM5708_BOOSTPOK_NG_IRQ, 3,  BIT(4)),
	REGMAP_IRQ_REG(SM5708_BOOSTPOK_IRQ,    3,  BIT(5)),
	REGMAP_IRQ_REG(SM5708_ABSTMR1OFF_IRQ,  3,  BIT(6)),
	REGMAP_IRQ_REG(SM5708_SBPS_IRQ,        3,  BIT(7)),
};

static bool sm5708_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SM5708_REG_INT1 ... SM5708_REG_INT4:
	case SM5708_REG_STATUS1 ... SM5708_REG_STATUS4:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config sm5708_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= sm5708_volatile_reg,
	.max_register	= SM5708_REG_MAX,
};

static const struct regmap_irq_chip sm5708_irq_chip = {
	.name		= MFD_DEV_NAME,
	.status_base	= SM5708_REG_INT1,
	.mask_base	= SM5708_REG_INTMSK1,
	.mask_invert	= true,
	.num_regs	= 4,
	.irqs		= sm5708_irqs,
	.num_irqs	= ARRAY_SIZE(sm5708_irqs),
};

static struct mfd_cell sm5708_devs[] = {
	{ .name = "sm5708-usbldo", },
	{ .name = "sm5708-charger", },
	{ .name = "sm5708-rgb-leds", },
	{ .name = "sm5708-fled", },
};

static int sm5708_i2c_probe(struct i2c_client *i2c,	
		const struct i2c_device_id *dev_id)
{
	struct sm5708_dev *sm5708;
	struct sm5708_platform_data *pdata = i2c->dev.platform_data;
	int ret = 0;

	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);

	sm5708 = kzalloc(sizeof(struct sm5708_dev), GFP_KERNEL);
	if (!sm5708)
		return -ENOMEM;

	i2c_set_clientdata(i2c, sm5708);
	sm5708->dev = &i2c->dev;
	sm5708->i2c = i2c;
	sm5708->irq = i2c->irq;

	sm5708->regmap = devm_regmap_init_i2c(i2c,
			&sm5708_regmap_config);

	mutex_init(&sm5708->i2c_lock);

	ret = regmap_add_irq_chip(sm5708->regmap, sm5708->irq,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_NO_SUSPEND,
			0, &sm5708_irq_chip, &sm5708->irq_data);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n", 
				sm5708->irq, ret);
		goto err_irq_init;
	}

	ret = mfd_add_devices(sm5708->dev, -1, sm5708_devs,
			ARRAY_SIZE(sm5708_devs), NULL, 0, NULL);
	if (ret < 0)
		goto err_mfd;

	device_init_wakeup(sm5708->dev, 0);

	pr_info("%s:%s Probe done\n", MFD_DEV_NAME, __func__);

	return ret;

err_mfd:
	mfd_remove_devices(sm5708->dev);
err_irq_init:
	mutex_destroy(&sm5708->i2c_lock);
err:
	kfree(sm5708);
	return ret;
}

static int sm5708_i2c_remove(struct i2c_client *i2c)
{
	struct sm5708_dev *sm5708 = i2c_get_clientdata(i2c);

	mfd_remove_devices(sm5708->dev);
	kfree(sm5708);

	return 0;
}

static const struct i2c_device_id sm5708_i2c_id[] = {
	{ MFD_DEV_NAME, TYPE_SM5708 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sm5708_i2c_id);

static const struct of_device_id sm5708_i2c_dt_ids[] = {
	{ .compatible = "sm,sm5708" },
	{ },
};
MODULE_DEVICE_TABLE(of, sm5708_i2c_dt_ids);

#if defined(CONFIG_PM)
static int sm5708_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sm5708_dev *sm5708 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(sm5708->irq);

	disable_irq(sm5708->irq);

	return 0;
}

static int sm5708_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sm5708_dev *sm5708 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(sm5708->irq);

	enable_irq(sm5708->irq);

	return 0;
}
#endif /* CONFIG_PM */

static SIMPLE_DEV_PM_OPS(sm5708_pm, sm5708_suspend, sm5708_resume);

static struct i2c_driver sm5708_i2c_driver = {
	.driver		= {
		.name	= MFD_DEV_NAME,
		.owner	= THIS_MODULE,
#if defined(CONFIG_PM)
		.pm	= &sm5708_pm,
#endif /* CONFIG_PM */
		.of_match_table	= sm5708_i2c_dt_ids,
	},
	.probe		= sm5708_i2c_probe,
	.remove		= sm5708_i2c_remove,
	.id_table	= sm5708_i2c_id,
};

static int __init sm5708_i2c_init(void)
{
	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);
	return i2c_add_driver(&sm5708_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(sm5708_i2c_init);

static void __exit sm5708_i2c_exit(void)
{
	i2c_del_driver(&sm5708_i2c_driver);
}
module_exit(sm5708_i2c_exit);

MODULE_DESCRIPTION("SM5708 multi-function core driver");
MODULE_LICENSE("GPL");
