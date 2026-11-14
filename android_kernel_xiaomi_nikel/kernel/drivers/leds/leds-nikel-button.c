/*
 * Redmi Note 4 MTK (nikel): backlight of the capacitive keys.
 *
 * The keys are lit by one GPIO (GPIO252 on the stock board data), switched
 * between two pinctrl states: "button_backlight_output0" (low = off) and
 * "button_backlight_output1" (high = on). It is on/off only, so any
 * brightness > 0 turns it on.
 *
 * Registers the LED class device "button-backlight", which the lights HAL
 * (device/xiaomi/nikel/liblight) drives for LIGHT_ID_BUTTONS.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/mutex.h>

#define NB_TAG "[nikel-keys-bl] "

struct nikel_btn_bl {
	struct led_classdev cdev;
	struct pinctrl *pctrl;
	struct pinctrl_state *off, *on;
	struct mutex lock;
	int state;			/* -1 unknown, 0 off, 1 on */
};

static void nikel_btn_bl_set(struct led_classdev *cdev, enum led_brightness value)
{
	struct nikel_btn_bl *bl = container_of(cdev, struct nikel_btn_bl, cdev);
	int want = value > 0;
	int err;

	mutex_lock(&bl->lock);
	if (want != bl->state) {
		err = pinctrl_select_state(bl->pctrl, want ? bl->on : bl->off);
		if (err)
			pr_err(NB_TAG "select %s failed: %d\n", want ? "on" : "off", err);
		else
			bl->state = want;
	}
	mutex_unlock(&bl->lock);
}

static int nikel_btn_bl_probe(struct platform_device *pdev)
{
	struct nikel_btn_bl *bl;
	int err;

	bl = devm_kzalloc(&pdev->dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;
	mutex_init(&bl->lock);
	bl->state = -1;

	bl->pctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(bl->pctrl)) {
		pr_err(NB_TAG "no pinctrl: %ld\n", PTR_ERR(bl->pctrl));
		return PTR_ERR(bl->pctrl);
	}
	bl->off = pinctrl_lookup_state(bl->pctrl, "button_backlight_output0");
	bl->on = pinctrl_lookup_state(bl->pctrl, "button_backlight_output1");
	if (IS_ERR(bl->off) || IS_ERR(bl->on)) {
		pr_err(NB_TAG "pinctrl states missing\n");
		return -ENODEV;
	}

	bl->cdev.name = "button-backlight";
	bl->cdev.max_brightness = 255;
	bl->cdev.brightness_set = nikel_btn_bl_set;
	err = led_classdev_register(&pdev->dev, &bl->cdev);
	if (err) {
		pr_err(NB_TAG "led register failed: %d\n", err);
		return err;
	}
	platform_set_drvdata(pdev, bl);
	nikel_btn_bl_set(&bl->cdev, LED_OFF);
	pr_info(NB_TAG "ready\n");
	return 0;
}

static int nikel_btn_bl_remove(struct platform_device *pdev)
{
	struct nikel_btn_bl *bl = platform_get_drvdata(pdev);

	led_classdev_unregister(&bl->cdev);
	return 0;
}

static const struct of_device_id nikel_btn_bl_of_match[] = {
	{ .compatible = "nikel,button-backlight" },
	{},
};
MODULE_DEVICE_TABLE(of, nikel_btn_bl_of_match);

static struct platform_driver nikel_btn_bl_driver = {
	.probe = nikel_btn_bl_probe,
	.remove = nikel_btn_bl_remove,
	.driver = {
		.name = "nikel-button-backlight",
		.of_match_table = nikel_btn_bl_of_match,
	},
};
module_platform_driver(nikel_btn_bl_driver);

MODULE_DESCRIPTION("nikel capacitive key backlight");
MODULE_LICENSE("GPL");
