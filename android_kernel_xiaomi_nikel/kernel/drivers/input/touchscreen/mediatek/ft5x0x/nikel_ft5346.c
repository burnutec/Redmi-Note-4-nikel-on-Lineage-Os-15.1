/*
 * nikel (Xiaomi Redmi Note 4 MTK): FocalTech FT5346 touch panel on the
 * MediaTek tpd framework of SHIFT's MT6797 kernel.
 *
 * Based on SHIFT's ft5x0x_driver.c (MediaTek reference driver), reworked for
 * nikel's board as found in the stock device tree and stock behaviour:
 *   - I2C bus 4, address 0x38 (DT node "mediatek,cap_touch")
 *   - interrupt, reset and power-enable pins via the pinctrl states of the
 *     "mediatek,mt6797-touch" node: EINT GPIO85, RST GPIO68, power GPIO58
 *     (state_enable_regulator_output0/1). No PMIC "vtouch" regulator.
 *   - 1080x1920, up to 10 fingers, three touch keys below the screen
 *     (positions and key codes from the DT, handled by tpd_button()).
 *
 * READ-ONLY BY DESIGN: this driver never writes the touch controller's
 * flash. There is no firmware upgrade, no bundled firmware, no APK debug
 * channel and no register access from user space. The only writes are the
 * same run-time (RAM) settings stock uses: report rate (0x88) and power
 * mode before suspend (0xA5).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>

#include "tpd.h"

#define NK_NAME			"nikel_ft5346"
#define NK_MAX_POINTS		10
#define NK_POINT_LEN		6
#define NK_READ_LEN		(3 + NK_POINT_LEN * NK_MAX_POINTS)	/* 63 */
#define NK_RESET_TRIES		3

/* FocalTech FT5x46 registers (read unless noted) */
#define NK_REG_DEVICE_MODE	0x00
#define NK_REG_REPORT_RATE	0x88	/* written: 0x08, as stock does */
#define NK_REG_CHIP_ID		0xA3
#define NK_REG_POWER_MODE	0xA5	/* written: 0x03 = hibernate, before power-off */
#define NK_REG_FW_VER		0xA6
#define NK_REG_VENDOR_ID	0xA8

#define NK_EVENT_DOWN		0
#define NK_EVENT_CONTACT	2

extern int tpd_type_cap;

static struct i2c_client *nk_client;
static struct task_struct *nk_thread;
static DECLARE_WAIT_QUEUE_HEAD(nk_waiter);
static int nk_flag;
static unsigned int nk_irq;
static int nk_suspended;
static int nk_key_active;	/* key strip currently touched (for the debug log) */
static u8 *nk_tx;		/* kmalloc'd: the I2C controller may DMA */
static u8 *nk_rx;

/* GPIO58 touch power switch, a state of the same pinctrl handle mtk_tpd uses */
static struct pinctrl *nk_pinctrl;
static struct pinctrl_state *nk_pwr_on, *nk_pwr_off;

static void nk_pins_init(void)
{
	struct pinctrl *p;

	if (!tpd || !tpd->tpd_dev) {
		pr_err(NK_NAME ": no tpd platform device, power pin not controlled\n");
		return;
	}
	p = pinctrl_get(tpd->tpd_dev);
	if (IS_ERR(p)) {
		pr_err(NK_NAME ": pinctrl_get failed (%ld)\n", PTR_ERR(p));
		return;
	}
	nk_pwr_on = pinctrl_lookup_state(p, "state_enable_regulator_output1");
	nk_pwr_off = pinctrl_lookup_state(p, "state_enable_regulator_output0");
	if (IS_ERR(nk_pwr_on) || IS_ERR(nk_pwr_off)) {
		pr_err(NK_NAME ": no state_enable_regulator_output0/1 in DT\n");
		pinctrl_put(p);
		return;
	}
	nk_pinctrl = p;
}

static void nk_power(int on)
{
	if (nk_pinctrl)
		pinctrl_select_state(nk_pinctrl, on ? nk_pwr_on : nk_pwr_off);
}

static void nk_reset(unsigned int settle_ms)
{
	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(20);
	tpd_gpio_output(GTP_RST_PORT, 1);
	msleep(settle_ms);
}

/* read the whole touch report (registers 0x00..0x3E) in one transfer */
static int nk_read_report(void)
{
	struct i2c_msg msgs[2] = {
		{ .addr = nk_client->addr, .flags = 0, .len = 1, .buf = nk_tx },
		{ .addr = nk_client->addr, .flags = I2C_M_RD, .len = NK_READ_LEN, .buf = nk_rx },
	};

	nk_tx[0] = NK_REG_DEVICE_MODE;
	return i2c_transfer(nk_client->adapter, msgs, 2) == 2 ? 0 : -EIO;
}

static void nk_release_all(void)
{
	input_report_key(tpd->dev, BTN_TOUCH, 0);
	input_mt_sync(tpd->dev);
	input_sync(tpd->dev);
	if (tpd_dts_data.use_tpd_button && tpd->btn_state)
		tpd_button(0, 0, 0);
}

static void nk_report(const u8 *buf)
{
	int i, n, contacts = 0;
	int key_x = -1, key_y = -1;

	n = buf[2] & 0x0f;
	if (n > NK_MAX_POINTS)
		return;		/* invalid report */

	for (i = 0; i < n; i++) {
		const u8 *p = buf + 3 + NK_POINT_LEN * i;
		int event = p[0] >> 6;
		int x = ((p[0] & 0x0f) << 8) | p[1];
		int y = ((p[2] & 0x0f) << 8) | p[3];
		int id = p[2] >> 4;

		if (event != NK_EVENT_DOWN && event != NK_EVENT_CONTACT)
			continue;	/* lift-up or no event */
		if (id >= NK_MAX_POINTS)
			continue;
		if ((unsigned long)y >= TPD_RES_Y) {
			/* below the display: touch key area */
			if (key_x < 0) {
				key_x = x;
				key_y = y;
			}
			continue;
		}
		input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id);
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 1);
		input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
		input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
		input_mt_sync(tpd->dev);
		contacts++;
	}

	if (contacts) {
		input_report_key(tpd->dev, BTN_TOUCH, 1);
	} else {
		input_report_key(tpd->dev, BTN_TOUCH, 0);
		input_mt_sync(tpd->dev);
	}
	input_sync(tpd->dev);

	if (tpd_dts_data.use_tpd_button) {
		if (key_x >= 0 && contacts == 0) {
			tpd_button(key_x, key_y, 1);
			if (!nk_key_active) {
				/* debug log: one line per key press (pr_debug: off by default) */
				pr_debug(NK_NAME ": key strip touch x=%d y=%d -> matched key mask 0x%x\n",
					key_x, key_y, tpd->btn_state);
				nk_key_active = 1;
			}
		} else {
			if (nk_key_active) {
				pr_debug(NK_NAME ": key strip release (mask was 0x%x)\n", tpd->btn_state);
				nk_key_active = 0;
			}
			if (tpd->btn_state)
				tpd_button(0, 0, 0);
		}
	}
}

static int nk_event_thread(void *unused)
{
	struct sched_param param = { .sched_priority = 4 };

	sched_setscheduler(current, SCHED_RR, &param);

	while (!kthread_should_stop()) {
		wait_event_interruptible(nk_waiter, nk_flag != 0 || kthread_should_stop());
		nk_flag = 0;
		if (kthread_should_stop())
			break;
		if (nk_read_report() < 0)
			continue;
		if ((nk_rx[0] & 0x70) != 0)
			continue;	/* not in normal operating mode */
		nk_report(nk_rx);
	}
	return 0;
}

static irqreturn_t nk_irq_handler(int irq, void *dev_id)
{
	nk_flag = 1;
	wake_up_interruptible(&nk_waiter);
	return IRQ_HANDLED;
}

static int nk_irq_register(void)
{
	struct device_node *node;
	int ret;

	/* the interrupt is on the "mediatek,mt6797-touch" node, as on stock */
	node = of_find_matching_node(NULL, touch_of_match);
	if (!node) {
		pr_err(NK_NAME ": touch DT node not found\n");
		return -ENODEV;
	}
	nk_irq = irq_of_parse_and_map(node, 0);
	of_node_put(node);
	if (!nk_irq) {
		pr_err(NK_NAME ": no interrupt in touch DT node\n");
		return -EINVAL;
	}
	ret = request_irq(nk_irq, nk_irq_handler, IRQF_TRIGGER_FALLING, NK_NAME, NULL);
	if (ret)
		pr_err(NK_NAME ": request_irq %u failed (%d)\n", nk_irq, ret);
	return ret;
}

static int nk_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int mode = -1, tries, ret;

	nk_client = client;
	nk_tx = kzalloc(4, GFP_KERNEL);
	nk_rx = kzalloc(NK_READ_LEN + 1, GFP_KERNEL);
	if (!nk_tx || !nk_rx) {
		ret = -ENOMEM;
		goto err_free;
	}

	nk_pins_init();
	nk_power(1);
	msleep(10);

	/* stock: reset, wait, expect DEVICE_MODE == 0 (normal operation) */
	for (tries = 0; tries < NK_RESET_TRIES; tries++) {
		nk_reset(300);
		mode = i2c_smbus_read_byte_data(client, NK_REG_DEVICE_MODE);
		if (mode == 0)
			break;
		pr_warn(NK_NAME ": no answer / not in normal mode (%d), try %d\n", mode, tries + 1);
	}
	if (mode != 0) {
		pr_err(NK_NAME ": controller not responding at 0x%02x, giving up\n", client->addr);
		ret = -ENODEV;
		goto err_power;
	}

	pr_info(NK_NAME ": chip id 0x%02x, firmware 0x%02x, vendor 0x%02x\n",
		i2c_smbus_read_byte_data(client, NK_REG_CHIP_ID) & 0xff,
		i2c_smbus_read_byte_data(client, NK_REG_FW_VER) & 0xff,
		i2c_smbus_read_byte_data(client, NK_REG_VENDOR_ID) & 0xff);

	if (i2c_smbus_write_byte_data(client, NK_REG_REPORT_RATE, 0x08) < 0)
		pr_warn(NK_NAME ": report rate write failed\n");

	nk_thread = kthread_run(nk_event_thread, NULL, NK_NAME);
	if (IS_ERR(nk_thread)) {
		ret = PTR_ERR(nk_thread);
		nk_thread = NULL;
		goto err_power;
	}

	tpd_gpio_as_int(GTP_INT_PORT);
	ret = nk_irq_register();
	if (ret)
		goto err_thread;

	tpd_load_status = 1;
	pr_info(NK_NAME ": ready (irq %u, %lux%lu, keys %s)\n", nk_irq, TPD_RES_X, TPD_RES_Y,
		tpd_dts_data.use_tpd_button ? "on" : "off");
	return 0;

err_thread:
	kthread_stop(nk_thread);
	nk_thread = NULL;
err_power:
	nk_power(0);
err_free:
	kfree(nk_tx);
	kfree(nk_rx);
	nk_tx = NULL;
	nk_rx = NULL;
	nk_client = NULL;
	return ret;
}

static int nk_remove(struct i2c_client *client)
{
	if (nk_irq)
		free_irq(nk_irq, NULL);
	if (nk_thread)
		kthread_stop(nk_thread);
	nk_power(0);
	return 0;
}

static const struct i2c_device_id nk_id[] = { { "ft5x0x", 0 }, { } };
static const struct of_device_id nk_dt_match[] = {
	{ .compatible = "mediatek,cap_touch" },
	{ },
};
MODULE_DEVICE_TABLE(of, nk_dt_match);

static struct i2c_driver nk_i2c_driver = {
	.driver = {
		.name = "ft5x0x",
		.of_match_table = nk_dt_match,
	},
	.probe = nk_probe,
	.remove = nk_remove,
	.id_table = nk_id,
};

static void nk_suspend(struct device *h)
{
	if (!nk_client || nk_suspended)
		return;
	disable_irq(nk_irq);
	i2c_smbus_write_byte_data(nk_client, NK_REG_POWER_MODE, 0x03);
	msleep(10);
	nk_power(0);
	nk_release_all();
	nk_suspended = 1;
}

static void nk_resume(struct device *h)
{
	if (!nk_client || !nk_suspended)
		return;
	nk_power(1);
	msleep(10);
	nk_reset(200);
	nk_release_all();
	nk_suspended = 0;
	enable_irq(nk_irq);
}

static int nk_local_init(void)
{
	if (i2c_add_driver(&nk_i2c_driver) != 0) {
		pr_err(NK_NAME ": i2c_add_driver failed\n");
		return -1;
	}
	if (tpd_dts_data.use_tpd_button)
		tpd_button_setting(tpd_dts_data.tpd_key_num, tpd_dts_data.tpd_key_local,
				   tpd_dts_data.tpd_key_dim_local);
	tpd_type_cap = 1;
	return 0;
}

static struct tpd_driver_t nk_tpd_driver = {
	.tpd_device_name = "FT5x0x",
	.tpd_local_init = nk_local_init,
	.suspend = nk_suspend,
	.resume = nk_resume,
};

static int __init nk_init(void)
{
	tpd_get_dts_info();
	if (tpd_driver_add(&nk_tpd_driver) < 0)
		pr_err(NK_NAME ": tpd_driver_add failed\n");
	return 0;
}

static void __exit nk_exit(void)
{
	tpd_driver_remove(&nk_tpd_driver);
}

module_init(nk_init);
module_exit(nk_exit);
MODULE_DESCRIPTION("nikel FT5346 touch (read-only, no firmware upgrade)");
MODULE_LICENSE("GPL v2");
