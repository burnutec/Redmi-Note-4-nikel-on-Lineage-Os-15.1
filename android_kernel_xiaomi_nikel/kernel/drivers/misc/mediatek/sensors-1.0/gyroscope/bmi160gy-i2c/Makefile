# nikel: BMI160 gyroscope on I2C (register access via the accel driver)
# The I2C conversion leaves "obj" locals that only fed the old SPI argument
ccflags-y += -Wno-unused-variable
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/gyroscope/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/accelerometer/bmi160-i2c
obj-y	:=  bmi160_gyro.o
