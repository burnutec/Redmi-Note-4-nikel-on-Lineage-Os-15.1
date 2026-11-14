"""Turn SHIFT's BMI160 SPI accel/gyro drivers into I2C drivers for nikel.

nikel wires the BMI160 to I2C bus 1 at 0x68 (DT nodes mediatek,gsensor @0x68
and mediatek,gyro @0x69, same as MIUI's bmi160_acc / bmi160_gyro). All chip
access in SHIFT's drivers goes through BMP160_spi_read_bytes/_write_bytes, so
only those two functions and the probe/registration code change.
"""
import re, sys, os
SRC, DST = sys.argv[1], sys.argv[2]   # kernel tree root (SHIFT), output root

def sub1(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)

# ------------------------------------------------------------------ accel
p = os.path.join(SRC, 'drivers/misc/mediatek/sensors-1.0/accelerometer/bmi160-spi/bmi160_acc.c')
s = open(p).read()
s = sub1(s, '#include <linux/spi/spi.h>\n#include <linux/spi/spidev.h>\n/* MTK header */\n#include "mtk_spi.h"\n#include "mtk_spi_hal.h"\n',
         '#include <linux/spi/spi.h>\t/* only for the struct type in BMP160_spi_* */\n#include <linux/i2c.h>\n')
s = sub1(s, 'static int bmi160_acc_spi_probe(struct spi_device *spi);\nstatic int bmi160_acc_spi_remove(struct spi_device *spi);\n',
         'static int bmi160_acc_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);\nstatic int bmi160_acc_i2c_remove(struct i2c_client *client);\n')
s = sub1(s, '\tstruct spi_device *spi;\n\tu8 *spi_buffer;\t\t/* only used for SPI transfer internal */\n\tstruct mt_chip_conf spi_mcc;\n',
         '\tstruct i2c_client *client;\n')
# drop SPI controller config, clock and speed helpers, and the SPI register access
a = s.index('const struct mt_chip_conf spi_ctrdata = {')
b = s.index('int BMP160_spi_read_bytes(struct spi_device *spi, u16 addr, u8 *rx_buf, u32 data_len)')
c = s.index('int BMP160_spi_write_bytes(struct spi_device *spi, u16 addr, u8 *tx_buf, u32 data_len)')
# end of write function: first "\n}\n" after c
e = s.index('\n}\n', c) + 3
s = s[:a] + '''/*
 * nikel: I2C register access. Same names and arguments as SHIFT's SPI
 * helpers (the gyro driver calls them too). MT6797's I2C controller moves at
 * most 8 bytes per message without DMA, so longer reads are split; the FIFO
 * data register (0x24) is read repeatedly instead of advancing the address.
 */
#define NIKEL_BMI_I2C_CHUNK	8
#define NIKEL_BMI_FIFO_DATA_REG	0x24

int BMP160_spi_read_bytes(struct spi_device *spi, u16 addr, u8 *rx_buf, u32 data_len)
{
	struct i2c_client *client;
	u8 reg;
	int ret = 0;

	if (!obj_data || !obj_data->client)
		return -ENODEV;
	client = obj_data->client;
	mutex_lock(&obj_data->spi_lock);
	while (data_len > 0) {
		u32 n = min_t(u32, data_len, NIKEL_BMI_I2C_CHUNK);
		struct i2c_msg msg[2] = {
			{ .addr = client->addr, .flags = 0, .len = 1, .buf = &reg },
			{ .addr = client->addr, .flags = I2C_M_RD, .len = n, .buf = rx_buf },
		};

		reg = (u8)addr;
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret != 2) {
			GSE_ERR("i2c read 0x%02x len %u failed: %d\\n", reg, n, ret);
			ret = -EIO;
			break;
		}
		ret = 0;
		rx_buf += n;
		data_len -= n;
		if (addr != NIKEL_BMI_FIFO_DATA_REG)
			addr += n;
	}
	mutex_unlock(&obj_data->spi_lock);
	return ret;
}

int BMP160_spi_write_bytes(struct spi_device *spi, u16 addr, u8 *tx_buf, u32 data_len)
{
	struct i2c_client *client;
	u8 buf[NIKEL_BMI_I2C_CHUNK];
	int ret = 0;

	if (!obj_data || !obj_data->client)
		return -ENODEV;
	client = obj_data->client;
	mutex_lock(&obj_data->spi_lock);
	while (data_len > 0) {
		u32 n = min_t(u32, data_len, NIKEL_BMI_I2C_CHUNK - 1);
		struct i2c_msg msg = { .addr = client->addr, .flags = 0, .len = n + 1, .buf = buf };

		buf[0] = (u8)addr;
		memcpy(&buf[1], tx_buf, n);
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret != 1) {
			GSE_ERR("i2c write 0x%02x len %u failed: %d\\n", buf[0], n, ret);
			ret = -EIO;
			break;
		}
		ret = 0;
		tx_buf += n;
		data_len -= n;
		addr += n;
	}
	mutex_unlock(&obj_data->spi_lock);
	return ret;
}
''' + s[e:]
# driver struct
s = sub1(s, '''static struct spi_driver bmi160_acc_spi_driver = {
	.driver = {
		   .name = BMI160_DEV_NAME,
		   .bus = &spi_bus_type,
#ifdef CONFIG_OF
		   .of_match_table = gsensor_of_match,
#endif
		   },
	.probe = bmi160_acc_spi_probe,
	.remove = bmi160_acc_spi_remove,
};''', '''static const struct i2c_device_id bmi160_acc_i2c_id[] = { {BMI160_DEV_NAME, 0}, {} };

static struct i2c_driver bmi160_acc_i2c_driver = {
	.driver = {
		   .name = BMI160_DEV_NAME,
#ifdef CONFIG_OF
		   .of_match_table = gsensor_of_match,
#endif
		   },
	.probe = bmi160_acc_i2c_probe,
	.remove = bmi160_acc_i2c_remove,
	.id_table = bmi160_acc_i2c_id,
};''')
s = sub1(s, 'static int bmi160acc_setup_eint(struct spi_device *spi)', 'static int bmi160acc_setup_eint(struct i2c_client *spi)')
# probe
s = sub1(s, 'static int bmi160_acc_spi_probe(struct spi_device *spi)\n{', 'static int bmi160_acc_i2c_probe(struct i2c_client *spi, const struct i2c_device_id *id)\n{')
# this kernel's get_accel_dts_func() fills *hw and returns 0/-1 (SHIFT's SPI copy was stale)
s = sub1(s, '''	hw = get_accel_dts_func(spi->dev.of_node, hw);
	if (!hw) {''', '''	if (get_accel_dts_func(spi->dev.of_node, hw)) {''')
s = sub1(s, '''	/* Initialize the driver data */
	obj->spi = spi;

	/* setup SPI parameters */
	/* CPOL=CPHA=0, speed 1MHz */
	obj->spi->mode = SPI_MODE_0;
	obj->spi->bits_per_word = 8;
	obj->spi->max_speed_hz = 1 * 1000 * 1000;
	memcpy(&obj->spi_mcc, &spi_ctrdata, sizeof(struct mt_chip_conf));
	obj->spi->controller_data = (void *)&obj->spi_mcc;

	spi_setup(obj->spi);
	spi_set_drvdata(spi, obj);

	/* allocate buffer for SPI transfer */
	obj->spi_buffer = kzalloc(bufsiz, GFP_KERNEL);
	if (obj->spi_buffer == NULL) {
		GSE_ERR("kzalloc spi_buffer fail.\\n");
		goto err_buf;
	}

	spi_clk_enable(obj, 1);
	/* set spi to high speed */
	spi_setup_conf(obj, 4, FIFO_TRANSFER);

	obj_data = obj;
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	mutex_init(&obj->lock);
	mutex_init(&obj->spi_lock);
''', '''	/* Initialize the driver data */
	obj->client = spi;
	i2c_set_clientdata(spi, obj);

	obj_data = obj;
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	mutex_init(&obj->lock);
	mutex_init(&obj->spi_lock);
''')
s = sub1(s, '''	bmi160_acc_init_flag = 0;
	spi_clk_enable(obj, 0);
	GSE_LOG("%s: is ok.\\n", __func__);''', '''	bmi160_acc_init_flag = 0;
	GSE_LOG("%s: is ok.\\n", __func__);''')
s = sub1(s, '''exit_init_failed:
	spi_clk_enable(obj, 0);
err_buf:
	kfree(obj->spi_buffer);
	spi_set_drvdata(spi, NULL);
	obj->spi = NULL;
exit_kfree:''', '''exit_init_failed:
	i2c_set_clientdata(spi, NULL);
	obj->client = NULL;
	obj_data = NULL;
exit_kfree:''')
s = sub1(s, '''static int bmi160_acc_spi_remove(struct spi_device *spi)
{
	struct bmi160_acc_data *obj = spi_get_drvdata(spi);''', '''static int bmi160_acc_i2c_remove(struct i2c_client *spi)
{
	struct bmi160_acc_data *obj = i2c_get_clientdata(spi);''')
s = sub1(s, '''	if (obj->spi_buffer != NULL) {
		kfree(obj->spi_buffer);
		obj->spi_buffer = NULL;
	}
	spi_clk_enable(obj, 0);
	spi_set_drvdata(spi, NULL);
	obj->spi = NULL;
''', '''	i2c_set_clientdata(spi, NULL);
	obj->client = NULL;
''')
s = sub1(s, 'if (spi_register_driver(&bmi160_acc_spi_driver)) {', 'if (i2c_add_driver(&bmi160_acc_i2c_driver)) {')
s = sub1(s, 'spi_unregister_driver(&bmi160_acc_spi_driver);', 'i2c_del_driver(&bmi160_acc_i2c_driver);')
s = sub1(s, 'MODULE_DESCRIPTION("BMI160_ACC SPI driver");', 'MODULE_DESCRIPTION("BMI160_ACC I2C driver (nikel)");')
s = re.sub(r'static unsigned bufsiz = \(15 \* 1024\);\n', '', s)
# the first argument of BMP160_spi_* is unused: pass NULL instead of the SPI device
s = re.sub(r'(BMP160_spi_(?:read|write)_bytes\()\s*(?:obj|obj_data)->spi\s*,', r'\1NULL,', s)
left = [l for l in s.split('\n') if re.search(r'spi_(setup|sync|message|clk|buffer|mcc|ctrdata)|obj->spi\b|->spi\b|mt_chip_conf', l)]
assert not left, left
od = os.path.join(DST, 'drivers/misc/mediatek/sensors-1.0/accelerometer/bmi160-i2c')
os.makedirs(od, exist_ok=True)
open(os.path.join(od, 'bmi160_acc.c'), 'w').write(s)
open(os.path.join(od, 'bmi160_acc.h'), 'w').write(open(p.replace('.c', '.h')).read())
open(os.path.join(od, 'Makefile'), 'w').write('''# nikel: BMI160 accelerometer on I2C (converted from SHIFT's bmi160-spi)
# The I2C conversion leaves "obj" locals that only fed the old SPI argument
ccflags-y += -Wno-unused-variable
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/accelerometer/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
obj-y	:=  bmi160_acc.o
''')
open(os.path.join(od, 'Kconfig'), 'w').write('''config MTK_BMI160_I2C
	bool "BMI160 accelerometer on I2C (nikel)"
	default n
	help
	  Bosch BMI160 accelerometer connected over I2C, as on the
	  Redmi Note 4 MTK (nikel). Converted from MTK_BMI160_SPI.
''')

# ------------------------------------------------------------------ gyro
p = os.path.join(SRC, 'drivers/misc/mediatek/sensors-1.0/gyroscope/bmi160gy-spi/bmi160_gyro.c')
s = open(p).read()
s = sub1(s, '#include <linux/spi/spi.h>\n#include <linux/spi/spidev.h>\n', '#include <linux/spi/spi.h>\t/* only for the struct type in BMP160_spi_* */\n#include <linux/i2c.h>\n')
s = s.replace('#include "mtk_spi.h"\n', '').replace('#include "mtk_spi_hal.h"\n', '')
s = sub1(s, 'static int bmg_spi_probe(struct spi_device *spi)', 'static int bmg_spi_probe(struct i2c_client *spi, const struct i2c_device_id *id)')
s = sub1(s, 'static int bmg_spi_remove(struct spi_device *spi)', 'static int bmg_spi_remove(struct i2c_client *spi)')
s = sub1(s, '''	hw = get_gyro_dts_func(spi->dev.of_node, hw);
	if (!hw)''', '''	if (get_gyro_dts_func(spi->dev.of_node, hw))''')
m = re.search(r'static struct spi_driver bmg_spi_driver = \{.*?\n\};', s, re.S)
drv = m.group(0)
new = drv.replace('static struct spi_driver bmg_spi_driver', 'static const struct i2c_device_id bmg_i2c_id[] = { {BMG_DEV_NAME, 0}, {} };\n\nstatic struct i2c_driver bmg_spi_driver')
new = re.sub(r'\n\s*\.bus = &spi_bus_type,', '', new)
new = new.replace('\t.remove = bmg_spi_remove,', '\t.remove = bmg_spi_remove,\n\t.id_table = bmg_i2c_id,')
s = s.replace(drv, new)
s = s.replace('spi_unregister_driver(&bmg_spi_driver);', 'i2c_del_driver(&bmg_spi_driver);')
s = s.replace('spi_register_driver(&bmg_spi_driver)', 'i2c_add_driver(&bmg_spi_driver)')
open('/tmp/claude-0/-home-claude/1cf3abac-322d-5669-95f3-631b5883bc58/scratchpad/gyro_left.txt', 'w').write(
    '\n'.join(l for l in s.split('\n') if re.search(r'spi', l) and 'BMP160_spi' not in l))
od = os.path.join(DST, 'drivers/misc/mediatek/sensors-1.0/gyroscope/bmi160gy-i2c')
os.makedirs(od, exist_ok=True)
open(os.path.join(od, 'bmi160_gyro.c'), 'w').write(s)
open(os.path.join(od, 'bmi160_gyro.h'), 'w').write(open(p.replace('.c', '.h')).read())
open(os.path.join(od, 'Makefile'), 'w').write('''# nikel: BMI160 gyroscope on I2C (register access via the accel driver)
ccflags-y += -Wno-unused-variable
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/gyroscope/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/accelerometer/bmi160-i2c
obj-y	:=  bmi160_gyro.o
''')
open(os.path.join(od, 'Kconfig'), 'w').write('''config MTK_BMI160GY_I2C
	bool "BMI160 gyroscope on I2C (nikel)"
	depends on MTK_BMI160_I2C
	default n
	help
	  Gyroscope part of the Bosch BMI160 on I2C (nikel). Uses the
	  register access of MTK_BMI160_I2C.
''')
print('ok')
