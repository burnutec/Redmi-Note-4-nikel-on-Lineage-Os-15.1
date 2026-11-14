# nikel: BMI160 accelerometer on I2C (converted from SHIFT's bmi160-spi)
# The I2C conversion leaves "obj" locals that only fed the old SPI argument
ccflags-y += -Wno-unused-variable
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/accelerometer/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
obj-y	:=  bmi160_acc.o
