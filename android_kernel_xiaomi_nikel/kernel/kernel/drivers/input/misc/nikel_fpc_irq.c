/*
 * Redmi Note 4 MTK (nikel): FPC1145 fingerprint sensor, TEE mode.
 *
 * The sensor itself is read by the FPC trusted app in Trustonic (TEE) over
 * SPI1; Linux never talks to it. What Linux has to provide is what MIUI's
 * lib_fpc_tac_shared.so expects from the "fpc_interrupt" platform device
 * (it finds the device by "fpc_interrupt" in its modalias):
 *
 *   irq            read: "1"/"0" = level of the sensor's IRQ line (GPIO86),
 *                  pollable (sysfs_notify on every interrupt);
 *                  write: acknowledge, ignored
 *   wakeup_enable  "enable" / "disable": finger touch may wake the system
 *   reset          "high" / "low": reset line (GPIO43)
 *   clk_enable     "1" / "0": SPI1 clock (on by default), kept for MIUI's rc
 *   hw_reset       "reset": full reset pulse
 *
 * The SPI side: a small spi driver binds to the "fpc,fpc1020" child of
 * spi@11012000 only to switch the SPI1 clock on while the system is awake,
 * since the trusted app uses the controller but cannot gate its clock.
 *
 * Power (GPIO139), reset and IRQ pins come from pinctrl states of the
 * fpc_interrupt@0 node, see nikel.dts.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/err.h>

#define FPC_TAG "[nikel-fpc] "

/* from drivers/spi/mediatek/mt6797/spi.c */
extern void mt_spi_enable_master_clk(struct spi_device *ms);
extern void mt_spi_disable_master_clk(struct spi_device *ms);

/* ------------------------------------------------------------------ */
/* SPI1 clock                                                          */

static DEFINE_MUTEX(fpc_clk_lock);
static struct spi_device *fpc_spi;
static bool fpc_clk_want = true;	/* what user space asked for */
static bool fpc_clk_on;			/* what is enabled right now */
static bool fpc_suspended;

static void fpc_clk_apply_locked(void)
{
	bool on = fpc_spi && fpc_clk_want && !fpc_suspended;

	if (on == fpc_clk_on)
		return;
	if (on)
		mt_spi_enable_master_clk(fpc_spi);
	else
		mt_spi_disable_master_clk(fpc_spi);
	fpc_clk_on = on;
}

static int fpc_spi_probe(struct spi_device *spi)
{
	mutex_lock(&fpc_clk_lock);
	fpc_spi = spi;
	fpc_suspended = false;
	fpc_clk_apply_locked();
	mutex_unlock(&fpc_clk_lock);
	pr_info(FPC_TAG "spi%d ready, clock %s\n", spi->master->bus_num,
		fpc_clk_on ? "on" : "off");
	return 0;
}

static int fpc_spi_remove(struct spi_device *spi)
{
	mutex_lock(&fpc_clk_lock);
	if (fpc_clk_on)
		mt_spi_disable_master_clk(spi);
	fpc_clk_on = false;
	fpc_spi = NULL;
	mutex_unlock(&fpc_clk_lock);
	return 0;
}

/* the SPI master unprepares its clock in suspend: drop ours first */
static int fpc_spi_suspend(struct device *dev)
{
	mutex_lock(&fpc_clk_lock);
	fpc_suspended = true;
	fpc_clk_apply_locked();
	mutex_unlock(&fpc_clk_lock);
	return 0;
}

static int fpc_spi_resume(struct device *dev)
{
	mutex_lock(&fpc_clk_lock);
	fpc_suspended = false;
	fpc_clk_apply_locked();
	mutex_unlock(&fpc_clk_lock);
	return 0;
}

static const struct dev_pm_ops fpc_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(fpc_spi_suspend, fpc_spi_resume)
};

static const struct of_device_id fpc_spi_of_match[] = {
	{ .compatible = "fpc,fpc1020" },
	{},
};
MODULE_DEVICE_TABLE(of, fpc_spi_of_match);

static struct spi_driver fpc_spi_driver = {
	.driver = {
		.name = "nikel-fpc-spi",
		.owner = THIS_MODULE,
		.of_match_table = fpc_spi_of_match,
		.pm = &fpc_spi_pm,
	},
	.probe = fpc_spi_probe,
	.remove = fpc_spi_remove,
};

/* ------------------------------------------------------------------ */
/* fpc_interrupt platform device                                       */

struct fpc_data {
	struct device *dev;
	struct pinctrl *pctrl;
	struct pinctrl_state *st_reset_high, *st_reset_low;
	struct pinctrl_state *st_power_on, *st_irq;
	int irq_gpio;
	int irq;
	bool wakeup_enabled;
	struct wakeup_source ws;
	struct mutex lock;
};

static int fpc_select(struct fpc_data *fpc, struct pinctrl_state *st)
{
	if (IS_ERR_OR_NULL(st))
		return -ENODEV;
	return pinctrl_select_state(fpc->pctrl, st);
}

static void fpc_hw_reset(struct fpc_data *fpc)
{
	fpc_select(fpc, fpc->st_reset_high);
	usleep_range(1000, 1100);
	fpc_select(fpc, fpc->st_reset_low);
	usleep_range(2000, 2100);
	fpc_select(fpc, fpc->st_reset_high);
	usleep_range(1250, 1350);
}

static ssize_t irq_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 gpio_get_value(fpc->irq_gpio) ? 1 : 0);
}

/* user space writes "1" after each poll wake-up: nothing to do */
static ssize_t irq_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR(irq, 0660, irq_show, irq_store);

static ssize_t wakeup_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	bool want;

	if (sysfs_streq(buf, "enable"))
		want = true;
	else if (sysfs_streq(buf, "disable"))
		want = false;
	else
		return -EINVAL;

	mutex_lock(&fpc->lock);
	if (want != fpc->wakeup_enabled) {
		if (want)
			enable_irq_wake(fpc->irq);
		else
			disable_irq_wake(fpc->irq);
		fpc->wakeup_enabled = want;
	}
	mutex_unlock(&fpc->lock);
	return count;
}

static ssize_t wakeup_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 fpc->wakeup_enabled ? "enable" : "disable");
}
static DEVICE_ATTR(wakeup_enable, 0660, wakeup_enable_show, wakeup_enable_store);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	int err;

	if (sysfs_streq(buf, "high"))
		err = fpc_select(fpc, fpc->st_reset_high);
	else if (sysfs_streq(buf, "low"))
		err = fpc_select(fpc, fpc->st_reset_low);
	else
		return -EINVAL;
	return err ? err : count;
}
static DEVICE_ATTR(reset, 0220, NULL, reset_store);

static ssize_t hw_reset_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);

	if (!sysfs_streq(buf, "reset"))
		return -EINVAL;
	mutex_lock(&fpc->lock);
	fpc_hw_reset(fpc);
	mutex_unlock(&fpc->lock);
	return count;
}
static DEVICE_ATTR(hw_reset, 0220, NULL, hw_reset_store);

static ssize_t clk_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	bool on;

	if (strtobool(buf, &on))
		return -EINVAL;
	mutex_lock(&fpc_clk_lock);
	fpc_clk_want = on;
	fpc_clk_apply_locked();
	mutex_unlock(&fpc_clk_lock);
	return count;
}

static ssize_t clk_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", fpc_clk_on ? 1 : 0);
}
static DEVICE_ATTR(clk_enable, 0660, clk_enable_show, clk_enable_store);

static struct attribute *fpc_attrs[] = {
	&dev_attr_irq.attr,
	&dev_attr_wakeup_enable.attr,
	&dev_attr_reset.attr,
	&dev_attr_hw_reset.attr,
	&dev_attr_clk_enable.attr,
	NULL,
};

static const struct attribute_group fpc_attr_group = {
	.attrs = fpc_attrs,
};

static irqreturn_t fpc_irq_handler(int irq, void *handle)
{
	struct fpc_data *fpc = handle;

	/* keep the system up long enough for the TA to read the sensor */
	if (fpc->wakeup_enabled)
		__pm_wakeup_event(&fpc->ws, 1000);
	sysfs_notify(&fpc->dev->kobj, NULL, dev_attr_irq.attr.name);
	return IRQ_HANDLED;
}

static int fpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct fpc_data *fpc;
	int err;

	fpc = devm_kzalloc(dev, sizeof(*fpc), GFP_KERNEL);
	if (!fpc)
		return -ENOMEM;
	fpc->dev = dev;
	mutex_init(&fpc->lock);
	dev_set_drvdata(dev, fpc);

	fpc->pctrl = devm_pinctrl_get(dev);
	if (IS_ERR(fpc->pctrl)) {
		pr_err(FPC_TAG "no pinctrl: %ld\n", PTR_ERR(fpc->pctrl));
		return PTR_ERR(fpc->pctrl);
	}
	fpc->st_reset_high = pinctrl_lookup_state(fpc->pctrl, "reset_high");
	fpc->st_reset_low = pinctrl_lookup_state(fpc->pctrl, "reset_low");
	fpc->st_power_on = pinctrl_lookup_state(fpc->pctrl, "power_on");
	fpc->st_irq = pinctrl_lookup_state(fpc->pctrl, "irq");
	if (IS_ERR(fpc->st_reset_high) || IS_ERR(fpc->st_reset_low) ||
	    IS_ERR(fpc->st_irq)) {
		pr_err(FPC_TAG "pinctrl states reset_high/reset_low/irq missing\n");
		return -ENODEV;
	}

	fpc->irq_gpio = of_get_named_gpio(np, "fpc,irq-gpio", 0);
	if (!gpio_is_valid(fpc->irq_gpio)) {
		pr_err(FPC_TAG "fpc,irq-gpio missing: %d\n", fpc->irq_gpio);
		return -ENODEV;
	}

	/* power, IRQ pin as EINT, then a clean reset */
	if (!IS_ERR(fpc->st_power_on)) {
		fpc_select(fpc, fpc->st_power_on);
		usleep_range(5000, 5500);
	}
	fpc_select(fpc, fpc->st_irq);
	fpc_hw_reset(fpc);

	fpc->irq = irq_of_parse_and_map(np, 0);
	if (!fpc->irq)
		fpc->irq = gpio_to_irq(fpc->irq_gpio);
	if (fpc->irq <= 0) {
		pr_err(FPC_TAG "no irq\n");
		return -ENODEV;
	}

	wakeup_source_init(&fpc->ws, "fpc_irq");
	device_init_wakeup(dev, true);

	err = devm_request_threaded_irq(dev, fpc->irq, NULL, fpc_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"fpc_irq", fpc);
	if (err) {
		pr_err(FPC_TAG "request irq %d failed: %d\n", fpc->irq, err);
		goto err_ws;
	}

	err = sysfs_create_group(&dev->kobj, &fpc_attr_group);
	if (err) {
		pr_err(FPC_TAG "sysfs failed: %d\n", err);
		goto err_ws;
	}

	pr_info(FPC_TAG "ready: irq %d (gpio %d, level %d)\n", fpc->irq,
		fpc->irq_gpio, gpio_get_value(fpc->irq_gpio));
	return 0;

err_ws:
	wakeup_source_trash(&fpc->ws);
	return err;
}

static int fpc_remove(struct platform_device *pdev)
{
	struct fpc_data *fpc = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &fpc_attr_group);
	if (fpc->wakeup_enabled)
		disable_irq_wake(fpc->irq);
	wakeup_source_trash(&fpc->ws);
	return 0;
}

static const struct of_device_id fpc_of_match[] = {
	{ .compatible = "fpc,fpc_irq" },
	{},
};
MODULE_DEVICE_TABLE(of, fpc_of_match);

static struct platform_driver fpc_platform_driver = {
	.driver = {
		.name = "nikel-fpc-irq",
		.owner = THIS_MODULE,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_probe,
	.remove = fpc_remove,
};

static int __init fpc_init(void)
{
	int err;

	err = spi_register_driver(&fpc_spi_driver);
	if (err) {
		pr_err(FPC_TAG "spi driver: %d\n", err);
		return err;
	}
	err = platform_driver_register(&fpc_platform_driver);
	if (err) {
		pr_err(FPC_TAG "platform driver: %d\n", err);
		spi_unregister_driver(&fpc_spi_driver);
	}
	return err;
}

static void __exit fpc_exit(void)
{
	platform_driver_unregister(&fpc_platform_driver);
	spi_unregister_driver(&fpc_spi_driver);
}

/* after the SPI master (device_initcall) */
late_initcall(fpc_init);
module_exit(fpc_exit);

MODULE_DESCRIPTION("nikel FPC1145 fingerprint (TEE) interrupt/reset/clock helper");
MODULE_LICENSE("GPL");
