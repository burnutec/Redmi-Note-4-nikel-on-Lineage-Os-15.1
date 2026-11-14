/*
 * Yamaha YAS537 (MS-3T) magnetometer for the MediaTek sensors-1.0 framework.
 * Written for the Redmi Note 4 MTK (nikel): I2C bus 1, address 0x2e,
 * DT node "mediatek,msensor".
 *
 * Chip handling (OTP calibration readout, power-on sequence, measurement and
 * x/y1/y2 -> x/y/z conversion) follows the mainline Linux IIO driver
 * drivers/iio/magnetometer/yamaha-yas530.c (YAS537 part, GPL-2.0), adapted
 * to kernel 3.18 and plain i2c transfers of at most 8 bytes (MT6797 I2C
 * without DMA).
 *
 * Neither SHIFT's sensor HAL nor Android calibrates the magnetometer here,
 * so the driver does a simple hard-iron calibration itself: it tracks the
 * min/max of each axis while the phone is turned around and subtracts the
 * centre. The reported accuracy rises as the covered range grows (wave the
 * phone in a figure 8 to calibrate).
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
#include <asm/unaligned.h>

#include "cust_mag.h"
#include "mag.h"

#define YAS_DEV_NAME		"yas537"
#define YAS_TAG			"[yas537] "
#define YAS_ERR(fmt, args...)	pr_err(YAS_TAG fmt, ##args)
#define YAS_LOG(fmt, args...)	pr_info(YAS_TAG fmt, ##args)

/* registers (mainline names, original Yamaha names in comments) */
#define YAS5XX_DEVICE_ID	0x80
#define YAS537_MEASURE		0x81	/* CMDR */
#define YAS537_CONFIG		0x82	/* CONFR */
#define YAS537_MEASURE_INTERVAL	0x83	/* INTRVLR */
#define YAS537_OFFSET_X		0x84	/* OXR */
#define YAS537_AVR		0x87
#define YAS537_HCK		0x88
#define YAS537_LCK		0x89
#define YAS537_SRST		0x90
#define YAS537_ADCCAL		0x91
#define YAS537_MTC		0x93
#define YAS537_OC		0x9E
#define YAS537_TRM		0x9F
#define YAS537_CAL		0xC0
#define YAS5XX_MEASURE_DATA	0xB0

#define YAS537_DEVICE_ID	0x07
#define YAS_MEASURE_START	BIT(0)
#define YAS_MEASURE_CONT	BIT(5)
#define YAS_DATA_BUSY		BIT(7)

/* bit-field helpers (no linux/bitfield.h in 3.18) */
#define YAS_GET(v, hi, lo)	(((v) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1))

/* framework: values are uT * YAS_DIV */
#define YAS_DIV			100
/* hard-iron calibration, in nT */
#define CAL_SPAN_HIGH		60000	/* all axes swept over 60 uT: accuracy high */
#define CAL_SPAN_MED		30000
#define CAL_MAX_FIELD		2400000	/* drop saturated samples (range is about 2000 uT) */
/*
 * The phone's own parts (speaker magnets etc.) can add a fixed offset of
 * hundreds of uT. If the calibrated field stays far above Earth's (25-65 uT)
 * the offset estimate is stale: start learning again.
 */
#define CAL_STALE_FIELD		150000	/* nT */
#define CAL_STALE_SAMPLES	15

struct yas537_cal {
	s16 Cx, Cy1, Cy2;
	s8 a2, a3, a4, a6, a7, a8;
	s16 a5, a9;
	u8 k;
};

struct yas537_priv {
	struct i2c_client *client;
	struct mag_hw hw;
	struct hwmsen_convert cvt;
	struct mutex lock;
	struct yas537_cal cal;
	unsigned int version;
	bool enabled, flush_pending;
	/* hard-iron calibration (nT, chip axes after layout) */
	s32 min[3], max[3];
	bool cal_valid;
	int stale;
	s32 last[3];		/* last calibrated sample, nT */
};

static struct yas537_priv *yas_obj;
/*
 * In-driver hard-iron calibration. On by default: tested on nikel, the
 * phone's own offset (~100 uT) is removed reliably this way; AKM's library
 * in SHIFT's HAL then only sees centred data. Off: echo 0 > .../msensor/kcal
 */
static int yas537_kcal = 1;
/*
 * Output scale in percent. Tested on nikel: a full sweep spans ~100 uT per
 * axis at 100, i.e. Earth's ~50 uT, so the conversion needs no correction.
 * Tune live: echo 110 > /sys/bus/platform/drivers/msensor/scale
 */
static unsigned int yas537_scale = 100;
static int yas537_init_flag = -1;
static int yas537_local_init(void);
static int yas537_local_uninit(void);

static struct mag_init_info yas537_init_info = {
	.name = YAS_DEV_NAME,
	.init = yas537_local_init,
	.uninit = yas537_local_uninit,
};

/* ------------------------------------------------------------- bus access */
static int yas_read(struct i2c_client *c, u8 reg, u8 *buf, int len)
{
	while (len > 0) {
		int n = min(len, 8);
		struct i2c_msg msg[2] = {
			{ .addr = c->addr, .flags = 0, .len = 1, .buf = &reg },
			{ .addr = c->addr, .flags = I2C_M_RD, .len = n, .buf = buf },
		};
		int ret = i2c_transfer(c->adapter, msg, 2);

		if (ret != 2) {
			YAS_ERR("read 0x%02x failed: %d\n", reg, ret);
			return -EIO;
		}
		reg += n;
		buf += n;
		len -= n;
	}
	return 0;
}

static int yas_write(struct i2c_client *c, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = c->addr, .flags = 0, .len = 2, .buf = buf };
	int ret = i2c_transfer(c->adapter, &msg, 1);

	if (ret != 1) {
		YAS_ERR("write 0x%02x=0x%02x failed: %d\n", reg, val, ret);
		return -EIO;
	}
	return 0;
}

static int yas_write2(struct i2c_client *c, u8 reg, u8 v0, u8 v1)
{
	u8 buf[3] = { reg, v0, v1 };
	struct i2c_msg msg = { .addr = c->addr, .flags = 0, .len = 3, .buf = buf };

	return i2c_transfer(c->adapter, &msg, 1) == 1 ? 0 : -EIO;
}

/* ------------------------------------------------------------- chip setup */
static int yas537_get_calibration_data(struct yas537_priv *obj)
{
	struct i2c_client *c = obj->client;
	struct yas537_cal *cal = &obj->cal;
	u8 d[17];
	u32 v1, v2, v3, v4;
	int i, err;

	err = yas_write(c, YAS537_SRST, BIT(1));
	if (err)
		return err;
	err = yas_read(c, YAS537_CAL, d, sizeof(d));
	if (err)
		return err;
	obj->version = YAS_GET(d[16], 7, 6);

	switch (obj->version) {
	case 0:
		for (i = 0; i < 12; i++)
			err |= yas_write(c, YAS537_MTC + i, d[i]);
		for (i = 0; i < 3; i++)
			err |= yas_write(c, YAS537_OFFSET_X + i, d[i + 12]);
		for (i = 0; i < 2; i++)
			err |= yas_write(c, YAS537_HCK + i, d[i + 15]);
		break;
	case 1:
		for (i = 0; i < 3; i++)
			err |= yas_write(c, YAS537_MTC + i, d[i]);
		for (i = 0; i < 3; i++)
			err |= yas_write(c, YAS537_OFFSET_X + i, d[i + 12]);
		err |= yas_write(c, YAS537_MTC + 3, (YAS_GET(d[3], 7, 5) & 0xff) | BIT(4));
		err |= yas_write(c, YAS537_HCK, YAS_GET(d[15], 7, 4) & 0x1f);
		err |= yas_write(c, YAS537_LCK, YAS_GET(d[15], 3, 0) & 0x1f);
		err |= yas_write(c, YAS537_OC, YAS_GET(d[16], 5, 0));
		v1 = get_unaligned_be32(&d[0]);
		v2 = get_unaligned_be32(&d[3]);
		v3 = get_unaligned_be32(&d[6]);
		v4 = get_unaligned_be32(&d[9]);
		cal->Cx  = (s16)YAS_GET(v1, 31, 23) - 256;
		cal->Cy1 = (s16)YAS_GET(v1, 22, 14) - 256;
		cal->Cy2 = (s16)YAS_GET(v1, 13, 5) - 256;
		cal->a2  = (s8)((int)YAS_GET(v2, 28, 22) - 64);
		cal->a3  = (s8)((int)YAS_GET(v2, 21, 15) - 64);
		cal->a4  = (s8)((int)YAS_GET(v2, 14, 7) - 128);
		cal->a5  = (s16)YAS_GET(v3, 30, 22) - 112;
		cal->a6  = (s8)((int)YAS_GET(v3, 21, 15) - 64);
		cal->a7  = (s8)((int)YAS_GET(v3, 14, 7) - 128);
		cal->a8  = (s8)((int)YAS_GET(v4, 30, 24) - 64);
		cal->a9  = (s16)YAS_GET(v4, 23, 15) - 112;
		cal->k   = YAS_GET(v4, 14, 8);
		break;
	default:
		YAS_ERR("unknown YAS537 version %u\n", obj->version);
		return -EINVAL;
	}
	return err ? -EIO : 0;
}

static int yas537_power_on(struct yas537_priv *obj)
{
	struct i2c_client *c = obj->client;
	int err;

	/* ADCCAL = 0x03f8 (GENMASK(9,3), big endian), TRM = 0xff */
	err = yas_write2(c, YAS537_ADCCAL, 0x03, 0xf8);
	err |= yas_write(c, YAS537_TRM, 0xff);
	/* 50 ms interval: (50000 - 1500) / 4100 */
	err |= yas_write(c, YAS537_MEASURE_INTERVAL, (50000 - 1500) / 4100);
	/* averaging 32 */
	err |= yas_write(c, YAS537_AVR, 0x70);
	/* rcoil */
	err |= yas_write(c, YAS537_CONFIG, BIT(3));
	udelay(100);
	return err ? -EIO : 0;
}

/* ------------------------------------------------------------- measuring */
/* one measurement in nT, chip axes */
static int yas537_measure(struct yas537_priv *obj, s32 xyz[3])
{
	struct yas537_cal *c = &obj->cal;
	const int half = 1 << 13;
	u8 d[8];
	u16 v[3];
	s32 s[3], h[3];
	int i, tries;

	if (yas_write(obj->client, YAS537_MEASURE, YAS_MEASURE_START | YAS_MEASURE_CONT))
		return -EIO;
	for (tries = 0; tries < 40; tries++) {
		usleep_range(500, 700);
		if (yas_read(obj->client, YAS5XX_MEASURE_DATA, d, sizeof(d)))
			return -EIO;
		if (!(d[2] & YAS_DATA_BUSY))
			break;
	}
	if (tries == 40) {
		YAS_ERR("measurement timeout\n");
		return -ETIMEDOUT;
	}

	v[0] = get_unaligned_be16(&d[2]) & 0x3fff;
	v[1] = get_unaligned_be16(&d[4]);
	v[2] = get_unaligned_be16(&d[6]);

	if (obj->version == 1) {
		for (i = 0; i < 3; i++)
			s[i] = (s32)v[i] - half;
		h[0] = (c->k * (128 * s[0] + c->a2 * s[1] + c->a3 * s[2])) / half;
		h[1] = (c->k * (c->a4 * s[0] + c->a5 * s[1] + c->a6 * s[2])) / half;
		h[2] = (c->k * (c->a7 * s[0] + c->a8 * s[1] + c->a9 * s[2])) / half;
		for (i = 0; i < 3; i++)
			v[i] = clamp(h[i], -half, half - 1) + half;
	}

	xyz[0] = ((s32)v[0] - (1 << 13)) * 300;
	xyz[1] = ((s32)v[1] - (s32)v[2]) * 1732 / 10;
	xyz[2] = (-(s32)v[1] - (s32)v[2] + (1 << 14)) * 300;
	return 0;
}

static void yas537_remap(struct yas537_priv *obj, s32 xyz[3])
{
	s32 t[3] = { xyz[0], xyz[1], xyz[2] };
	int i;

	for (i = 0; i < 3; i++)
		xyz[obj->cvt.map[i]] = obj->cvt.sign[i] * t[i];
}

static void yas537_cal_reset(struct yas537_priv *obj)
{
	int i;

	for (i = 0; i < 3; i++) {
		obj->min[i] = INT_MAX;
		obj->max[i] = INT_MIN;
	}
	obj->cal_valid = false;
	obj->stale = 0;
}

/* hard-iron: track the swept range, return accuracy 1..3 */
static int yas537_cal_update(struct yas537_priv *obj, const s32 xyz[3])
{
	s32 span, minspan = INT_MAX;
	int i;

	for (i = 0; i < 3; i++)
		if (abs(xyz[i]) > CAL_MAX_FIELD)
			goto status;
	for (i = 0; i < 3; i++) {
		if (xyz[i] < obj->min[i])
			obj->min[i] = xyz[i];
		if (xyz[i] > obj->max[i])
			obj->max[i] = xyz[i];
	}
	obj->cal_valid = true;
	/* offset estimate stale? */
	{
		s64 m = 0, d;

		for (i = 0; i < 3; i++) {
			d = xyz[i] - (obj->max[i] + obj->min[i]) / 2;
			m += d * d;
		}
		if (m > (s64)CAL_STALE_FIELD * CAL_STALE_FIELD) {
			if (++obj->stale >= CAL_STALE_SAMPLES) {
				YAS_LOG("offset changed, recalibrating\n");
				yas537_cal_reset(obj);
				for (i = 0; i < 3; i++)
					obj->min[i] = obj->max[i] = xyz[i];
				obj->cal_valid = true;
			}
		} else {
			obj->stale = 0;
		}
	}
status:
	if (!obj->cal_valid)
		return SENSOR_STATUS_UNRELIABLE;
	for (i = 0; i < 3; i++) {
		span = obj->max[i] - obj->min[i];
		if (span < minspan)
			minspan = span;
	}
	if (minspan >= CAL_SPAN_HIGH)
		return SENSOR_STATUS_ACCURACY_HIGH;
	if (minspan >= CAL_SPAN_MED)
		return SENSOR_STATUS_ACCURACY_MEDIUM;
	return SENSOR_STATUS_ACCURACY_LOW;
}

static int yas537_get_data(int *x, int *y, int *z, int *status)
{
	s32 v[3];
	int i, st = SENSOR_STATUS_UNRELIABLE, err;

	if (!yas_obj)
		return -1;
	mutex_lock(&yas_obj->lock);
	err = yas537_measure(yas_obj, v);
	if (!err) {
		yas537_remap(yas_obj, v);
		st = yas537_cal_update(yas_obj, v);
		if (!yas537_kcal)
			st = SENSOR_STATUS_ACCURACY_HIGH;	/* the HAL library calibrates */
		else if (yas_obj->cal_valid)
			for (i = 0; i < 3; i++)
				v[i] -= (yas_obj->max[i] + yas_obj->min[i]) / 2;
		for (i = 0; i < 3; i++)
			yas_obj->last[i] = v[i];
	}
	mutex_unlock(&yas_obj->lock);
	if (err)
		return -1;
	/* nT -> uT * YAS_DIV, with the board scale */
	*x = (int)((s64)v[0] * yas537_scale / 100 / (1000 / YAS_DIV));
	*y = (int)((s64)v[1] * yas537_scale / 100 / (1000 / YAS_DIV));
	*z = (int)((s64)v[2] * yas537_scale / 100 / (1000 / YAS_DIV));
	*status = st;
	return 0;
}

/* ------------------------------------------------------------- framework */
static int yas537_enable(int en)
{
	if (!yas_obj)
		return -1;
	yas_obj->enabled = en;
	if (en && yas_obj->flush_pending) {
		yas_obj->flush_pending = false;
		mag_flush_report();
	}
	return 0;
}

static int yas537_set_delay(u64 ns)
{
	return 0;
}

static int yas537_open_report_data(int open)
{
	return 0;
}

static int yas537_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return 0;
}

static int yas537_flush(void)
{
	if (!yas_obj)
		return -1;
	if (!yas_obj->enabled) {
		yas_obj->flush_pending = true;
		return 0;
	}
	return mag_flush_report();
}

/* ------------------------------------------------------------- debug sysfs */
static ssize_t yas537_show_data(struct device_driver *ddri, char *buf)
{
	s32 v[3];
	int i;

	if (!yas_obj)
		return 0;
	mutex_lock(&yas_obj->lock);
	if (yas537_measure(yas_obj, v)) {
		mutex_unlock(&yas_obj->lock);
		return scnprintf(buf, PAGE_SIZE, "read error\n");
	}
	yas537_remap(yas_obj, v);
	mutex_unlock(&yas_obj->lock);
	for (i = 0; i < 3; i++)
		v[i] /= 100;	/* 0.1 uT */
	return scnprintf(buf, PAGE_SIZE,
			 "raw uT x10: %d %d %d  |  calibrated uT x10: %d %d %d  |  span uT: %d %d %d\n",
			 v[0], v[1], v[2],
			 yas_obj->last[0] / 100, yas_obj->last[1] / 100, yas_obj->last[2] / 100,
			 yas_obj->cal_valid ? (yas_obj->max[0] - yas_obj->min[0]) / 1000 : 0,
			 yas_obj->cal_valid ? (yas_obj->max[1] - yas_obj->min[1]) / 1000 : 0,
			 yas_obj->cal_valid ? (yas_obj->max[2] - yas_obj->min[2]) / 1000 : 0);
}

static ssize_t yas537_show_layout(struct device_driver *ddri, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", yas_obj ? yas_obj->hw.direction : -1);
}

/* echo 0..7 > layout   (also resets the calibration) */
static ssize_t yas537_store_layout(struct device_driver *ddri, const char *buf, size_t count)
{
	int layout;

	if (!yas_obj || kstrtoint(buf, 10, &layout) || layout < 0 || layout > 7)
		return -EINVAL;
	mutex_lock(&yas_obj->lock);
	if (!hwmsen_get_convert(layout, &yas_obj->cvt)) {
		yas_obj->hw.direction = layout;
		yas537_cal_reset(yas_obj);
	}
	mutex_unlock(&yas_obj->lock);
	return count;
}

/* echo 1 > cali_reset */
static ssize_t yas537_store_cal_reset(struct device_driver *ddri, const char *buf, size_t count)
{
	if (!yas_obj)
		return -EINVAL;
	mutex_lock(&yas_obj->lock);
	yas537_cal_reset(yas_obj);
	mutex_unlock(&yas_obj->lock);
	return count;
}

static ssize_t yas537_show_kcal(struct device_driver *ddri, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", yas537_kcal);
}

static ssize_t yas537_store_kcal(struct device_driver *ddri, const char *buf, size_t count)
{
	int v;

	if (kstrtoint(buf, 10, &v))
		return -EINVAL;
	yas537_kcal = !!v;
	return count;
}

static ssize_t yas537_show_scale(struct device_driver *ddri, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", yas537_scale);
}

static ssize_t yas537_store_scale(struct device_driver *ddri, const char *buf, size_t count)
{
	unsigned int v;

	if (kstrtouint(buf, 0, &v) || v < 10 || v > 1000)
		return -EINVAL;
	yas537_scale = v;
	return count;
}

static DRIVER_ATTR(data, S_IRUGO, yas537_show_data, NULL);
static DRIVER_ATTR(scale, S_IRUGO | S_IWUSR, yas537_show_scale, yas537_store_scale);
static DRIVER_ATTR(kcal, S_IRUGO | S_IWUSR, yas537_show_kcal, yas537_store_kcal);
static DRIVER_ATTR(layout, S_IRUGO | S_IWUSR, yas537_show_layout, yas537_store_layout);
static DRIVER_ATTR(cali_reset, S_IWUSR, NULL, yas537_store_cal_reset);

static struct driver_attribute *yas537_attrs[] = {
	&driver_attr_data, &driver_attr_layout, &driver_attr_cali_reset, &driver_attr_kcal, &driver_attr_scale,
};

/* ------------------------------------------------------------- i2c driver */
static int yas537_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct yas537_priv *obj;
	struct mag_control_path ctl = {0};
	struct mag_data_path data = {0};
	u8 devid = 0;
	int err, i;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;
	err = get_mag_dts_func(client->dev.of_node, &obj->hw);
	if (err < 0) {
		YAS_ERR("no msensor DT config\n");
		goto fail;
	}
	if (hwmsen_get_convert(obj->hw.direction, &obj->cvt)) {
		YAS_ERR("invalid direction %d, using 0\n", obj->hw.direction);
		obj->hw.direction = 0;
		hwmsen_get_convert(0, &obj->cvt);
	}
	obj->client = client;
	mutex_init(&obj->lock);
	yas537_cal_reset(obj);
	i2c_set_clientdata(client, obj);

	err = yas_read(client, YAS5XX_DEVICE_ID, &devid, 1);
	if (err)
		goto fail;
	if (devid != YAS537_DEVICE_ID) {
		YAS_ERR("device id 0x%02x, not a YAS537\n", devid);
		err = -ENODEV;
		goto fail;
	}
	err = yas537_get_calibration_data(obj);
	if (err)
		goto fail;
	err = yas537_power_on(obj);
	if (err)
		goto fail;
	yas_obj = obj;
	YAS_LOG("YAS537 version %u, direction %d\n", obj->version, obj->hw.direction);

	for (i = 0; i < ARRAY_SIZE(yas537_attrs); i++)
		if (driver_create_file(&yas537_init_info.platform_diver_addr->driver, yas537_attrs[i]))
			YAS_ERR("debug attr %d not created\n", i);

	ctl.enable = yas537_enable;
	ctl.set_delay = yas537_set_delay;
	ctl.open_report_data = yas537_open_report_data;
	ctl.batch = yas537_batch;
	ctl.flush = yas537_flush;
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = false;
	ctl.is_use_common_factory = false;
	/*
	 * SHIFT's sensor HAL reads maglibinfo and dlopen()s lib<libname>.so for
	 * the calibration/fusion library; without one it reports nothing. Only
	 * libakl.so (AKM) ships, so present ourselves as an AK09911 (0x05) with
	 * the identity axis pattern 0x06: data is already in Android axes, uT*100.
	 */
	strlcpy(ctl.libinfo.libname, "akl", sizeof(ctl.libinfo.libname));
	ctl.libinfo.layout = 0x06;
	ctl.libinfo.deviceid = 0x05;
	err = mag_register_control_path(&ctl);
	if (err)
		goto fail_reg;
	data.div = YAS_DIV;
	data.get_data = yas537_get_data;
	err = mag_register_data_path(&data);
	if (err)
		goto fail_reg;

	yas537_init_flag = 0;
	YAS_LOG("probe ok\n");
	return 0;

fail_reg:
	YAS_ERR("framework registration failed: %d\n", err);
	yas_obj = NULL;
fail:
	i2c_set_clientdata(client, NULL);
	kfree(obj);
	yas537_init_flag = -1;
	return err;
}

static int yas537_i2c_remove(struct i2c_client *client)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yas537_attrs); i++)
		driver_remove_file(&yas537_init_info.platform_diver_addr->driver, yas537_attrs[i]);
	kfree(i2c_get_clientdata(client));
	yas_obj = NULL;
	return 0;
}

static const struct i2c_device_id yas537_i2c_id[] = { {YAS_DEV_NAME, 0}, {} };

#ifdef CONFIG_OF
static const struct of_device_id yas537_of_match[] = {
	{ .compatible = "mediatek,msensor" },
	{},
};
#endif

static struct i2c_driver yas537_i2c_driver = {
	.driver = {
		.name = YAS_DEV_NAME,
#ifdef CONFIG_OF
		.of_match_table = yas537_of_match,
#endif
	},
	.probe = yas537_i2c_probe,
	.remove = yas537_i2c_remove,
	.id_table = yas537_i2c_id,
};

static int yas537_local_init(void)
{
	if (i2c_add_driver(&yas537_i2c_driver)) {
		YAS_ERR("add driver error\n");
		return -1;
	}
	return yas537_init_flag == -1 ? -1 : 0;
}

static int yas537_local_uninit(void)
{
	i2c_del_driver(&yas537_i2c_driver);
	return 0;
}

static int __init yas537_init(void)
{
	mag_driver_add(&yas537_init_info);
	return 0;
}

static void __exit yas537_exit(void)
{
}

module_init(yas537_init);
module_exit(yas537_exit);

MODULE_DESCRIPTION("YAS537 magnetometer driver (nikel)");
MODULE_LICENSE("GPL");
