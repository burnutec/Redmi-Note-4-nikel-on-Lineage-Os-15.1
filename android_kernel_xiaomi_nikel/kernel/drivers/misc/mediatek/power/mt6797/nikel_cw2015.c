/*
 * nikel (Xiaomi Redmi Note 4 MTK): CellWise CW2015 fuel gauge, READ-ONLY.
 *
 * Stock binds the CW2015 at I2C bus 1, address 0x62 through the DT node
 * "mediatek,chargepump" (driver name "CW2015"). The gauge is powered by the
 * battery itself, so it keeps the battery profile and its running state that
 * stock loaded into it. This driver only READS:
 *   0x00 VERSION, 0x02/0x03 VCELL, 0x04/0x05 SOC, 0x08 CONFIG, 0x0A MODE,
 *   0x10..0x4F battery profile (logged once, as a backup copy).
 * It never writes a register: no profile download, no wake-up, no reset.
 *
 * nikel_cw2015_get_soc() is called by battery_meter.c (nikel patch). It
 * returns the gauge's percentage only when the gauge is awake and reports a
 * loaded profile; otherwise it returns an error and MediaTek's own gauge
 * percentage is used, as before.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/of.h>

#define CW_NAME			"nikel_cw2015"

#define CW_REG_VERSION		0x00
#define CW_REG_VCELL		0x02	/* 14 bit, 305 uV per LSB */
#define CW_REG_SOC		0x04	/* integer percent; 0x05 = 1/256 percent */
#define CW_REG_CONFIG		0x08	/* bit 1: UFG, battery profile loaded */
#define CW_REG_MODE		0x0A	/* bits 7:6 = 11: sleep */
#define CW_REG_BATINFO		0x10
#define CW_BATINFO_SIZE		64

#define CW_CONFIG_UFG		0x02
#define CW_MODE_SLEEP_MASK	0xC0

#define CW_LOG_INTERVAL		(60 * HZ)

static struct i2c_client *cw_client;
static DEFINE_MUTEX(cw_lock);
static unsigned long cw_next_log;
static int cw_last_state = -100;	/* last reason logged, to avoid repeats */

static int cw_read(u8 reg)
{
	return i2c_smbus_read_byte_data(cw_client, reg);
}

/* log why the gauge value is not used, but only when the reason changes */
static void cw_state(int state, const char *why, int val)
{
	if (state != cw_last_state) {
		pr_info(CW_NAME ": %s (0x%02x), using MediaTek gauge\n", why, val & 0xff);
		cw_last_state = state;
	}
}

int nikel_cw2015_get_soc(int *soc, int mtk_soc)
{
	int mode, config, s, s_frac, vh, vl, ret = -ENODEV;

	if (!cw_client)
		return -ENODEV;

	mutex_lock(&cw_lock);

	mode = cw_read(CW_REG_MODE);
	if (mode < 0) {
		cw_state(1, "MODE read failed", mode);
		ret = -EIO;
		goto out;
	}
	if ((mode & CW_MODE_SLEEP_MASK) == CW_MODE_SLEEP_MASK) {
		cw_state(2, "gauge is in sleep mode", mode);
		ret = -EAGAIN;
		goto out;
	}
	config = cw_read(CW_REG_CONFIG);
	if (config < 0 || !(config & CW_CONFIG_UFG)) {
		cw_state(3, "no battery profile loaded (CONFIG)", config);
		ret = -EINVAL;
		goto out;
	}
	s = cw_read(CW_REG_SOC);
	if (s < 0 || s > 100) {
		cw_state(4, "SOC invalid", s);
		ret = -EINVAL;
		goto out;
	}

	*soc = s;
	ret = 0;
	if (cw_last_state != 0) {
		pr_info(CW_NAME ": gauge valid, percentage now from CW2015\n");
		cw_last_state = 0;
	}

	if (time_after_eq(jiffies, cw_next_log)) {
		s_frac = cw_read(CW_REG_SOC + 1);
		vh = cw_read(CW_REG_VCELL);
		vl = cw_read(CW_REG_VCELL + 1);
		pr_info(CW_NAME ": soc cw=%d.%02d%% (mtk=%d%%) vcell=%d mV\n",
			s, s_frac < 0 ? 0 : (s_frac * 100) >> 8, mtk_soc,
			(vh < 0 || vl < 0) ? -1 : ((((vh & 0x3f) << 8) | vl) * 305) / 1000);
		cw_next_log = jiffies + CW_LOG_INTERVAL;
	}
out:
	mutex_unlock(&cw_lock);
	return ret;
}

static void cw_dump_profile(struct i2c_client *client)
{
	u8 info[CW_BATINFO_SIZE];
	int i, v;

	for (i = 0; i < CW_BATINFO_SIZE; i++) {
		v = i2c_smbus_read_byte_data(client, CW_REG_BATINFO + i);
		if (v < 0) {
			pr_err(CW_NAME ": profile read failed at 0x%02x (%d)\n", CW_REG_BATINFO + i, v);
			return;
		}
		info[i] = v;
	}
	for (i = 0; i < CW_BATINFO_SIZE; i += 16)
		pr_info(CW_NAME ": profile[%02d] %16ph\n", i, info + i);
}

static int cw_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int version, config, mode;

	version = i2c_smbus_read_byte_data(client, CW_REG_VERSION);
	if (version < 0) {
		pr_err(CW_NAME ": no answer at %d-%04x (%d)\n",
		       i2c_adapter_id(client->adapter), client->addr, version);
		return -ENODEV;
	}
	config = i2c_smbus_read_byte_data(client, CW_REG_CONFIG);
	mode = i2c_smbus_read_byte_data(client, CW_REG_MODE);
	pr_info(CW_NAME ": found at %d-%04x: version 0x%02x, config 0x%02x, mode 0x%02x (read-only driver)\n",
		i2c_adapter_id(client->adapter), client->addr, version & 0xff, config & 0xff, mode & 0xff);
	cw_dump_profile(client);

	mutex_lock(&cw_lock);
	cw_client = client;
	cw_next_log = jiffies;
	mutex_unlock(&cw_lock);
	return 0;
}

static int cw_remove(struct i2c_client *client)
{
	mutex_lock(&cw_lock);
	cw_client = NULL;
	mutex_unlock(&cw_lock);
	return 0;
}

static const struct i2c_device_id cw_id[] = { { "CW2015", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, cw_id);

static const struct of_device_id cw_dt_match[] = {
	{ .compatible = "mediatek,chargepump" },	/* nikel stock DT name for the CW2015 */
	{ },
};
MODULE_DEVICE_TABLE(of, cw_dt_match);

static struct i2c_driver cw_driver = {
	.driver = {
		.name = "CW2015",
		.of_match_table = cw_dt_match,
	},
	.probe = cw_probe,
	.remove = cw_remove,
	.id_table = cw_id,
};

module_i2c_driver(cw_driver);

MODULE_DESCRIPTION("nikel CW2015 fuel gauge, read-only");
MODULE_LICENSE("GPL v2");
