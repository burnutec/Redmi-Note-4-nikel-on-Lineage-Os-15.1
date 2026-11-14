# nikel: build ONLY the read-only FT5346 driver.
# SHIFT's ft5x0x_driver.c and ft5x0x_upgrade.c (firmware flashing, FT5436
# firmware) stay in the tree but are deliberately NOT compiled.
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

obj-y	+=  nikel_ft5346.o
