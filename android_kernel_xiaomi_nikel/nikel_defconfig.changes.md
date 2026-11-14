# nikel_defconfig vs SHIFT5me_defconfig

Everything not listed here is SHIFT's value.

| Option | SHIFT5me | nikel | Why |
|---|---|---|---|
| `LOCALVERSION` | "-SHIFT" | "-nikel" | kernel version suffix |
| `SHIFT5ME_PROJECT` | y | off | SHIFT5me-only code paths; check git grep before re-enabling anything |
| `ARCH_MTK_PROJECT` | "SHIFT5me" | "nikel" | project name (no nikel.dws exists, so DrvGen is skipped) |
| `BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES` | "SHIFT5me_mt6797" | "nikel" | builds arch/arm64/boot/dts/nikel.dts and appends it |
| `BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES` | "SHIFT5me" | off | nikel's LK has no odmdtbo partition |
| `BUILD_ARM64_DTB_OVERLAY_IMAGE` | (unset) | off | no overlay image |
| `MTK_DTBO_FEATURE` | (unset) | off | no overlay: single merged DTB like nikel stock |
| `MTK_AB_OTA_UPDATER` | y | off | nikel has no A/B partitions (also drops slotselect from DT fstab) |
| `CUSTOM_KERNEL_LCM` | "ft8716_fhd_dsi_cmd_shift5me" | "nt35596_fhd_tianma_phantom" | this phone's panel (lcm=1-nt35596_fhd_tianma_phantom) |
| `LCM_HEIGHT` | (unset) | "1920" | panel |
| `LCM_WIDTH` | (unset) | "1080" | panel |
| `MTK_LCM_DEVICE_TREE_SUPPORT` | (unset) | off | keeps lcm_i2c.c out (it would not conflict, but is unused) |
| `MTK_BQ25896_SUPPORT` | y | off | DANGER: SHIFT charger driver @0x6b; nikel's BQ24261 is also @0x6b with a different register map |
| `MTK_PUMP_EXPRESS_PLUS_SUPPORT` | y | off | DANGER: fast-charge up to 12 V; nikel never had it |
| `MTK_PUMP_EXPRESS_PLUS_20_SUPPORT` | y | off | DANGER: as above |
| `TOUCHSCREEN_MTK_FOCALTECH_TS` | y | off | DANGER: FT8716 driver @0x38 may auto-flash its firmware into nikel's FT5346 |
| `TOUCHSCREEN_MTK_FT5X0X` | (unset) | y | enables drivers/.../ft5x0x/, whose Makefile the overlay replaces to build ONLY nikel_ft5346.c (read-only, no firmware upgrade) |
| `FT_AUTO_UPGRADE_SUPPORT` | (unset) | off | never flash touch firmware (SHIFT's ft5x0x_upgrade.c carries FT5436 firmware) |
| `CUST_FTS_APK_DEBUG` | (unset) | off | no user-space register/flash access channel |
| `TPD_HAVE_CALIBRATION` | (unset) | off | not used by nikel |
| `MTK_BQ24296_SUPPORT` | (unset) | y | nikel's charger (stock binds bq24296 @0-006b); SHIFT MT6735 driver + Longcheer deltas |
| `MTK_HAFG_20` | y | off | nikel stock uses the classic gauge (no fg_20 in stock kernel); nikel DT tables are classic |
| `MTK_KERNEL_POWER_OFF_CHARGING` | y | off | needs a charging animation in the boot ramdisk; later |
| `USB_C_SWITCH_FUSB302` | y | off | nikel is micro-USB |
| `USB_POWER_DELIVERY` | y | off | nikel is micro-USB |
| `MTK_HDMI_SUPPORT` | y | off | MediaTek reference leftover, no HDMI chip |
| `MTK_HDMI_HDCP_SUPPORT` | y | off | as above |
| `CUSTOM_KERNEL_HDMI` | "Sii8348" | off | as above |
| `CUSTOM_KERNEL_BAROMETER` | y | off | nikel has no barometer |
| `MTK_LPS25HB` | y | off | SHIFT5me barometer driver |
| `MTK_ICM206XX_A` | y | off | SHIFT5me accel; nikel uses BMI160/LSM6 (later) |
| `MTK_ICM206XX_G` | y | off | SHIFT5me gyro; as above |
| `MTK_STK3X1X_NEW` | y | off | SHIFT5me light/prox; nikel uses LTR5xx/APDS (later) |
| `CUSTOM_KERNEL_ACCELEROMETER` | y | off | no nikel driver selected yet |
| `CUSTOM_KERNEL_GYROSCOPE` | y | off | no nikel driver selected yet |
| `CUSTOM_KERNEL_ALSPS` | y | off | no nikel driver selected yet |
| `MTK_IMGSENSOR` | y | y (V18) | camera probe: nikel power code in `imgsensor/src/mt6797/camera_project/nikel/camera_hw` |
| `CUSTOM_KERNEL_IMGSENSOR` | "imx230_mipi_raw imx258_mipi_raw s5k3l8_mipi_raw s5k3l8_mipi_raw_zk" | "s5k3l8_mipi_raw s5k5e8yx_mipi_raw nikel_slot" | nikel main (S5K3L8; OV13853 only reported) and front (S5K5E8YX), both built from `camera_project/nikel/` |
| `MTK_CAM_CAL` | y | off | as above |
| `CUSTOM_KERNEL_CAM_CAL_DRV` | "BRCB032GWZ_3 GT24c32a cat24c16" | off | as above |
| `MTK_LENS` | y | y (V19) | autofocus. Main AF chip on I2C bus 2 at 0x72 in nikel.dts (stock used DW9714AF/DW9718AF). Lens sub-options as SHIFT, plus DW9714AF and DW9718AF; SHIFT's `DW9763AF` line dropped (no such Kconfig symbol) |
| `MTK_FLASHLIGHT` | y | y (V18) | camera HAL needs /dev/kd_camera_flashlight; nikel's flash chip (LM3643, KTD2684 on some units) not ported yet |
| `CUSTOM_KERNEL_FLASHLIGHT` | "constant_flashlight" | "constant_flashlight" | as SHIFT: its LM3643 driver (same chip as nikel, `strobe_main@63` in nikel.dts). Works on nikel: torch and camera flash light up |
| `MTK_IRTX_SUPPORT` | (unset) | y | nikel IR blaster (in Ruben's nikel defconfig) |


