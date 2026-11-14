/*
 * Lite-On LTR-579ALS-01 ambient light + proximity sensor for the MediaTek
 * sensors-1.0 framework (alsps). Written for the Redmi Note 4 MTK (nikel):
 * I2C bus 1, address 0x53, DT node "mediatek,alsps" (MIUI: LTR_579ALS).
 *
 * Both sensors are polled by the alsps framework (no interrupt needed):
 *  - light: 20-bit ALS count at gain 1x, 18-bit / 100 ms, mapped to lux with
 *    the als_level / als_value table from the board DT (Xiaomi's tuning);
 *  - proximity: 11-bit count compared with ps_threshold_high / _low
 *    (0 = near, 1 = far, MediaTek convention).
 *
 * Register map: Lite-On LTR-579ALS-01 datasheet (MAIN_CTRL 0x00 ... ALS 0x0D).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/workqueue.h>

#include "cust_alsps.h"
#include "alsps.h"

#define LTR579_DEV_NAME		"ltr579"
#define LTR_TAG			"[ltr579] "
#define LTR_ERR(fmt, args...)	pr_err(LTR_TAG fmt, ##args)
#define LTR_LOG(fmt, args...)	pr_info(LTR_TAG fmt, ##args)
#define LTR_DBG(fmt, args...)	pr_debug(LTR_TAG fmt, ##args)

/* registers */
#define LTR579_MAIN_CTRL	0x00	/* bit0 PS_EN, bit1 ALS_EN, bit4 SW_RESET */
#define LTR579_PS_LED		0x01
#define LTR579_PS_PULSES	0x02
#define LTR579_PS_MEAS_RATE	0x03
#define LTR579_ALS_MEAS_RATE	0x04	/* [6:4] resolution, [2:0] rate */
#define LTR579_ALS_GAIN		0x05	/* 0:1x 1:3x 2:6x 3:9x 4:18x */
#define LTR579_PART_ID		0x06	/* 0xB1 */
#define LTR579_MAIN_STATUS	0x07
#define LTR579_PS_DATA_0	0x08	/* 11 bit: [7:0] + [2:0] of 0x09, bit3 = overflow */
#define LTR579_ALS_DATA_0	0x0D	/* 20 bit: 0x0D, 0x0E, [3:0] of 0x0F */

#define LTR579_PS_EN		0x01
#define LTR579_ALS_EN		0x02
#define LTR579_SW_RESET		0x10
#define LTR579_PART_ID_VAL	0xB1

#define LTR579_ALS_MEAS_18BIT_100MS	0x22

struct ltr579_priv {
	struct i2c_client *client;
	struct alsps_hw hw;
	struct mutex lock;
	u8 main_ctrl;
	bool als_on, ps_on;
	int last_ps;		/* 0 near, 1 far */
	u32 als_raw;
	u16 ps_raw;
	struct delayed_work ps_work;	/* the framework does not poll PS */
	int gain_idx;		/* index into ltr_gains[] */
	int last_lux;
	int als_level_num, als_value_num;
};

static struct ltr579_priv *ltr_obj;
static int ltr579_init_flag = -1;
static int ltr579_local_init(void);
static int ltr579_local_uninit(void);

static struct alsps_init_info ltr579_init_info = {
	.name = LTR579_DEV_NAME,
	.init = ltr579_local_init,
	.uninit = ltr579_local_uninit,
};

/* ------------------------------------------------------------- bus access */
static int ltr_read(struct i2c_client *c, u8 reg, u8 *buf, int len)
{
	struct i2c_msg msg[2] = {
		{ .addr = c->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = c->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	int ret = i2c_transfer(c->adapter, msg, 2);

	if (ret != 2) {
		LTR_ERR("read 0x%02x failed: %d\n", reg, ret);
		return -EIO;
	}
	return 0;
}

static int ltr_write(struct i2c_client *c, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = c->addr, .flags = 0, .len = 2, .buf = buf };
	int ret = i2c_transfer(c->adapter, &msg, 1);

	if (ret != 1) {
		LTR_ERR("write 0x%02x=0x%02x failed: %d\n", reg, val, ret);
		return -EIO;
	}
	return 0;
}

static int ltr_set_enable(struct ltr579_priv *obj, u8 bit, bool on)
{
	u8 v = on ? (obj->main_ctrl | bit) : (obj->main_ctrl & ~bit);
	int err;

	if (v == obj->main_ctrl)
		return 0;
	err = ltr_write(obj->client, LTR579_MAIN_CTRL, v);
	if (!err)
		obj->main_ctrl = v;
	return err;
}

/* ------------------------------------------------------------- conversion */
static int ltr_read_als(struct ltr579_priv *obj, u32 *raw)
{
	u8 b[3];
	int err = ltr_read(obj->client, LTR579_ALS_DATA_0, b, 3);

	if (err)
		return err;
	*raw = b[0] | (b[1] << 8) | ((b[2] & 0x0f) << 16);
	return 0;
}

static int ltr_read_ps(struct ltr579_priv *obj, u16 *raw)
{
	u8 b[2];
	int err = ltr_read(obj->client, LTR579_PS_DATA_0, b, 2);

	if (err)
		return err;
	*raw = b[0] | ((b[1] & 0x07) << 8);
	return 0;
}

/*
 * Auto-ranging: the light window on nikel is dark, so dim rooms give only a
 * few counts at 1x. Gain moves between 1x and 18x; readings are scaled to
 * 3x-equivalent counts, the gain the stock lux table (als_level) was tuned at.
 */
static const u8 ltr_gain_reg[] = { 0, 1, 2, 3, 4 };
static const int ltr_gains[] = { 1, 3, 6, 9, 18 };
#define LTR_GAIN_DEFAULT	4	/* 18x */
#define LTR_RAW_HIGH		200000	/* 18-bit full scale is 262143 */
#define LTR_RAW_LOW		1500

/* returns 1 if the gain was changed (the current sample is then not used) */
static int ltr_autorange(struct ltr579_priv *obj, u32 raw)
{
	int idx = obj->gain_idx;

	if (raw > LTR_RAW_HIGH && idx > 0)
		idx = raw >= 262000 ? 0 : idx - 1;	/* saturated: go straight to 1x */
	else if (raw < LTR_RAW_LOW && idx < ARRAY_SIZE(ltr_gains) - 1)
		idx++;
	if (idx == obj->gain_idx)
		return 0;
	if (ltr_write(obj->client, LTR579_ALS_GAIN, ltr_gain_reg[idx]))
		return 0;
	obj->gain_idx = idx;
	return 1;
}

/* counts at the current gain -> counts at 3x, times 16 (keeps dim-light detail) */
static u64 ltr_norm(struct ltr579_priv *obj, u32 raw)
{
	return (u64)raw * 3 * 16 / ltr_gains[obj->gain_idx];
}

/*
 * Lux correction in percent for the nikel glass. Tested: a monitor at 10 cm
 * read ~31 lux with the plain table; 2500 (x25) chosen on the phone.
 * Tune live: echo 800 > /sys/bus/platform/drivers/als_ps/lux_scale
 */
static unsigned int ltr_lux_scale = 2500;

/* norm16 -> lux: piecewise linear over the board's als_level/als_value, then scaled */
static int ltr_als_to_lux(struct ltr579_priv *obj, u64 n16)
{
	u64 lvl_lo, lvl_hi, val_lo, val_hi, lux16;
	int idx;

	for (idx = 0; idx < obj->als_level_num; idx++)
		if (n16 < (u64)obj->hw.als_level[idx] * 16)
			break;
	if (idx >= obj->als_level_num) {
		lux16 = (u64)obj->hw.als_value[min(idx, obj->als_value_num - 1)] * 16;
	} else {
		lvl_hi = (u64)obj->hw.als_level[idx] * 16;
		lvl_lo = idx ? (u64)obj->hw.als_level[idx - 1] * 16 : 0;
		val_hi = obj->hw.als_value[idx];
		val_lo = idx ? obj->hw.als_value[idx - 1] : 0;
		if (lvl_hi <= lvl_lo || val_hi <= val_lo)
			lux16 = val_lo * 16;
		else
			lux16 = val_lo * 16 + (n16 - lvl_lo) * (val_hi - val_lo) * 16 / (lvl_hi - lvl_lo);
	}
	return (int)min_t(u64, lux16 * ltr_lux_scale / 1600, 100000);
}

/* ------------------------------------------------------------- framework */
static int als_open_report_data(int open)
{
	return 0;
}

static int als_enable_nodata(int en)
{
	int err;

	if (!ltr_obj)
		return -1;
	mutex_lock(&ltr_obj->lock);
	err = ltr_set_enable(ltr_obj, LTR579_ALS_EN, en);
	if (!err)
		ltr_obj->als_on = en;
	mutex_unlock(&ltr_obj->lock);
	LTR_DBG("als %s (%d)\n", en ? "on" : "off", err);
	return err ? -1 : 0;
}

static int als_set_delay(u64 ns)
{
	return 0;
}

static int als_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return 0;
}

static int als_flush(void)
{
	return als_flush_report();
}

static int als_get_data(int *value, int *status)
{
	u32 raw;
	int err;

	if (!ltr_obj)
		return -1;
	mutex_lock(&ltr_obj->lock);
	err = ltr_read_als(ltr_obj, &raw);
	if (!err) {
		ltr_obj->als_raw = raw;
		if (!ltr_autorange(ltr_obj, raw))
			ltr_obj->last_lux = ltr_als_to_lux(ltr_obj, ltr_norm(ltr_obj, raw));
	}
	mutex_unlock(&ltr_obj->lock);
	if (err)
		return -1;
	*value = ltr_obj->last_lux;
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	return 0;
}

static int ps_open_report_data(int open)
{
	return 0;
}

/* PS near/far with hysteresis; returns the state (0 near, 1 far) or -1 */
static int ltr_ps_update(struct ltr579_priv *obj)
{
	u16 raw;

	if (ltr_read_ps(obj, &raw))
		return -1;
	obj->ps_raw = raw;
	if (raw > obj->hw.ps_threshold_high)
		obj->last_ps = 0;		/* near */
	else if (raw < obj->hw.ps_threshold_low)
		obj->last_ps = 1;		/* far */
	return obj->last_ps;
}

/*
 * SHIFT's alsps framework has PS polling compiled out (#if 0) and expects an
 * interrupt-driven driver, so poll here and report changes like an interrupt.
 */
#define LTR_PS_POLL_MS	100
static void ltr_ps_work(struct work_struct *work)
{
	struct ltr579_priv *obj = container_of(to_delayed_work(work), struct ltr579_priv, ps_work);
	int old, now;
	bool on;

	mutex_lock(&obj->lock);
	old = obj->last_ps;
	now = ltr_ps_update(obj);
	on = obj->ps_on;
	mutex_unlock(&obj->lock);
	if (now >= 0 && now != old)
		ps_report_interrupt_data(now);
	if (on)
		schedule_delayed_work(&obj->ps_work, msecs_to_jiffies(LTR_PS_POLL_MS));
}

static int ps_enable_nodata(int en)
{
	int err;

	if (!ltr_obj)
		return -1;
	if (!en)
		cancel_delayed_work_sync(&ltr_obj->ps_work);
	mutex_lock(&ltr_obj->lock);
	err = ltr_set_enable(ltr_obj, LTR579_PS_EN, en);
	if (!err) {
		ltr_obj->ps_on = en;
		ltr_obj->last_ps = 1;		/* the framework reports "far" on enable */
	}
	mutex_unlock(&ltr_obj->lock);
	if (!err && en)
		schedule_delayed_work(&ltr_obj->ps_work, msecs_to_jiffies(LTR_PS_POLL_MS));
	LTR_DBG("ps %s (%d)\n", en ? "on" : "off", err);
	return err ? -1 : 0;
}

static int ps_set_delay(u64 ns)
{
	return 0;
}

static int ps_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return 0;
}

static int ps_flush(void)
{
	return ps_flush_report();
}

static int ps_get_data(int *value, int *status)
{
	int v;

	if (!ltr_obj)
		return -1;
	mutex_lock(&ltr_obj->lock);
	v = ltr_ps_update(ltr_obj);
	mutex_unlock(&ltr_obj->lock);
	if (v < 0)
		return -1;
	*value = v;
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	return 0;
}

/* ------------------------------------------------------------- debug sysfs */
static ssize_t ltr579_show_raw(struct device_driver *ddri, char *buf)
{
	u32 als = 0;
	u16 ps = 0;

	if (!ltr_obj)
		return 0;
	mutex_lock(&ltr_obj->lock);
	ltr_read_als(ltr_obj, &als);
	ltr_read_ps(ltr_obj, &ps);
	mutex_unlock(&ltr_obj->lock);
	return scnprintf(buf, PAGE_SIZE, "als_raw=%u gain=%dx lux=%d ps_raw=%u ps=%s main_ctrl=0x%02x thd=%u/%u\n",
			 als, ltr_gains[ltr_obj->gain_idx], ltr_als_to_lux(ltr_obj, ltr_norm(ltr_obj, als)),
			 ps, ltr_obj->last_ps ? "far" : "near",
			 ltr_obj->main_ctrl, ltr_obj->hw.ps_threshold_high, ltr_obj->hw.ps_threshold_low);
}

static ssize_t ltr579_show_reg(struct device_driver *ddri, char *buf)
{
	u8 r[0x10];
	int i, n = 0;

	if (!ltr_obj || ltr_read(ltr_obj->client, 0x00, r, sizeof(r)))
		return 0;
	for (i = 0; i < sizeof(r); i++)
		n += scnprintf(buf + n, PAGE_SIZE - n, "%02x:%02x%s", i, r[i], (i % 8 == 7) ? "\n" : " ");
	return n;
}

static DRIVER_ATTR(raw, S_IRUGO, ltr579_show_raw, NULL);
/* debug: echo "05 04" > reg  writes 0x04 to register 0x05 (hex) */
static ssize_t ltr579_store_reg(struct device_driver *ddri, const char *buf, size_t count)
{
	unsigned int reg, val;

	if (!ltr_obj || sscanf(buf, "%x %x", &reg, &val) != 2 || reg > 0x26 || val > 0xff)
		return -EINVAL;
	mutex_lock(&ltr_obj->lock);
	ltr_write(ltr_obj->client, reg, val);
	if (reg == LTR579_MAIN_CTRL)
		ltr_obj->main_ctrl = val;
	if (reg == LTR579_ALS_GAIN && val < ARRAY_SIZE(ltr_gains))
		ltr_obj->gain_idx = val;
	mutex_unlock(&ltr_obj->lock);
	return count;
}

static DRIVER_ATTR(reg, S_IRUGO | S_IWUSR, ltr579_show_reg, ltr579_store_reg);

static ssize_t ltr579_show_scale(struct device_driver *ddri, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", ltr_lux_scale);
}

static ssize_t ltr579_store_scale(struct device_driver *ddri, const char *buf, size_t count)
{
	unsigned int v;

	if (kstrtouint(buf, 0, &v) || v < 10 || v > 10000)
		return -EINVAL;
	ltr_lux_scale = v;
	return count;
}

static DRIVER_ATTR(lux_scale, S_IRUGO | S_IWUSR, ltr579_show_scale, ltr579_store_scale);

static struct driver_attribute *ltr579_attrs[] = { &driver_attr_raw, &driver_attr_reg, &driver_attr_lux_scale };

/* ------------------------------------------------------------- i2c driver */
static int ltr579_init_chip(struct ltr579_priv *obj)
{
	u8 id = 0;
	int err;

	err = ltr_read(obj->client, LTR579_PART_ID, &id, 1);
	if (err)
		return err;
	if (id != LTR579_PART_ID_VAL)
		LTR_ERR("unexpected part id 0x%02x (want 0x%02x), continuing\n", id, LTR579_PART_ID_VAL);
	ltr_write(obj->client, LTR579_MAIN_CTRL, LTR579_SW_RESET);
	msleep(20);
	obj->main_ctrl = 0;
	err = ltr_write(obj->client, LTR579_MAIN_CTRL, 0);
	err |= ltr_write(obj->client, LTR579_ALS_MEAS_RATE, LTR579_ALS_MEAS_18BIT_100MS);
	obj->gain_idx = LTR_GAIN_DEFAULT;
	err |= ltr_write(obj->client, LTR579_ALS_GAIN, ltr_gain_reg[obj->gain_idx]);
	LTR_LOG("part id 0x%02x, ps thresholds %u/%u, %d lux levels\n", id,
		obj->hw.ps_threshold_high, obj->hw.ps_threshold_low, obj->als_level_num);
	return err ? -EIO : 0;
}

static int ltr579_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ltr579_priv *obj;
	struct als_control_path als_ctl = {0};
	struct als_data_path als_data = {0};
	struct ps_control_path ps_ctl = {0};
	struct ps_data_path ps_data = {0};
	int err, i;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;
	err = get_alsps_dts_func(client->dev.of_node, &obj->hw);
	if (err < 0) {
		LTR_ERR("no alsps DT config\n");
		goto fail;
	}
	obj->client = client;
	mutex_init(&obj->lock);
	INIT_DELAYED_WORK(&obj->ps_work, ltr_ps_work);
	obj->last_ps = 1;
	obj->als_level_num = ARRAY_SIZE(obj->hw.als_level);
	obj->als_value_num = ARRAY_SIZE(obj->hw.als_value);
	i2c_set_clientdata(client, obj);

	err = ltr579_init_chip(obj);
	if (err)
		goto fail;
	ltr_obj = obj;

	for (i = 0; i < ARRAY_SIZE(ltr579_attrs); i++)
		if (driver_create_file(&ltr579_init_info.platform_diver_addr->driver, ltr579_attrs[i]))
			LTR_ERR("debug attr %d not created\n", i);

	als_ctl.open_report_data = als_open_report_data;
	als_ctl.enable_nodata = als_enable_nodata;
	als_ctl.set_delay = als_set_delay;
	als_ctl.batch = als_batch;
	als_ctl.flush = als_flush;
	als_ctl.is_report_input_direct = false;
	als_ctl.is_support_batch = false;
	als_ctl.is_polling_mode = true;
	als_ctl.is_use_common_factory = false;
	err = als_register_control_path(&als_ctl);
	if (err)
		goto fail_reg;
	als_data.get_data = als_get_data;
	als_data.vender_div = 100;
	err = als_register_data_path(&als_data);
	if (err)
		goto fail_reg;

	ps_ctl.open_report_data = ps_open_report_data;
	ps_ctl.enable_nodata = ps_enable_nodata;
	ps_ctl.set_delay = ps_set_delay;
	ps_ctl.batch = ps_batch;
	ps_ctl.flush = ps_flush;
	ps_ctl.is_report_input_direct = false;
	ps_ctl.is_support_batch = false;
	ps_ctl.is_polling_mode = true;		/* no interrupt: poll near/far */
	ps_ctl.is_use_common_factory = false;
	err = ps_register_control_path(&ps_ctl);
	if (err)
		goto fail_reg;
	ps_data.get_data = ps_get_data;
	ps_data.vender_div = 100;
	err = ps_register_data_path(&ps_data);
	if (err)
		goto fail_reg;

	ltr579_init_flag = 0;
	LTR_LOG("probe ok\n");
	return 0;

fail_reg:
	LTR_ERR("framework registration failed: %d\n", err);
	ltr_obj = NULL;
fail:
	i2c_set_clientdata(client, NULL);
	kfree(obj);
	ltr579_init_flag = -1;
	return err;
}

static int ltr579_i2c_remove(struct i2c_client *client)
{
	struct ltr579_priv *obj = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < ARRAY_SIZE(ltr579_attrs); i++)
		driver_remove_file(&ltr579_init_info.platform_diver_addr->driver, ltr579_attrs[i]);
	if (obj) {
		cancel_delayed_work_sync(&obj->ps_work);
		ltr_write(client, LTR579_MAIN_CTRL, 0);
	}
	ltr_obj = NULL;
	kfree(obj);
	return 0;
}

static const struct i2c_device_id ltr579_i2c_id[] = { {LTR579_DEV_NAME, 0}, {} };

#ifdef CONFIG_OF
static const struct of_device_id ltr579_of_match[] = {
	{ .compatible = "mediatek,alsps" },
	{},
};
#endif

static struct i2c_driver ltr579_i2c_driver = {
	.driver = {
		.name = LTR579_DEV_NAME,
#ifdef CONFIG_OF
		.of_match_table = ltr579_of_match,
#endif
	},
	.probe = ltr579_i2c_probe,
	.remove = ltr579_i2c_remove,
	.id_table = ltr579_i2c_id,
};

static int ltr579_local_init(void)
{
	if (i2c_add_driver(&ltr579_i2c_driver)) {
		LTR_ERR("add driver error\n");
		return -1;
	}
	return ltr579_init_flag == -1 ? -1 : 0;
}

static int ltr579_local_uninit(void)
{
	i2c_del_driver(&ltr579_i2c_driver);
	return 0;
}

static int __init ltr579_init(void)
{
	alsps_driver_add(&ltr579_init_info);
	return 0;
}

static void __exit ltr579_exit(void)
{
}

module_init(ltr579_init);
module_exit(ltr579_exit);

MODULE_DESCRIPTION("LTR-579 ALS/PS driver (nikel)");
MODULE_LICENSE("GPL");
