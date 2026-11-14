# nikel kernel overlay

Kernel changes for the **Xiaomi Redmi Note 4 MTK (nikel, MT6797 Helio X20)**,
used by the unofficial LineageOS 15.1 port.

This is not a full kernel. It is an *overlay* for SHIFTPHONES' open MT6797
Oreo kernel:

- **Base:** [SHIFTPHONES/android_kernel_shift_mt6797](https://github.com/SHIFTPHONES/android_kernel_shift_mt6797),
  branch `shift-8.0-mtk` (Linux 3.18)
- **This repository:** nikel's device tree and defconfig, nikel drivers, and
  numbered patches to SHIFT's files

The build-tools repository (`nikel_setup_and_build.sh`) clones SHIFT's kernel,
copies `kernel/` over it, applies the patches in order and builds
`Image.gz-dtb`. Each patch is applied only once: on later runs, a patch that is
already in the tree is detected and skipped.

## Layout

| Path | What it is |
|---|---|
| `kernel/` | new files, copied into the kernel tree at the same paths |
| `0001-…patch` … `0014-…patch` | changes to SHIFT's existing files, applied at the tree root with `git apply` |
| `nikel_defconfig.changes.md` | every config difference from SHIFT's `SHIFT5me_defconfig`, and why |
| `README-panel.md` | notes on the display panel driver |
| `nikel-cw2015-profile.md` | backup of the battery gauge profile read from a working nikel |
| `tools/` | scripts used to derive parts of the overlay from the stock firmware (device tree conversion, I2C map, panel init tables, defconfig) |

## New files (`kernel/`)

| File(s) | Purpose |
|---|---|
| `arch/arm64/boot/dts/nikel.dts` | nikel board description: pins, supplies, I2C devices, built on SHIFT's MT6797 base |
| `arch/arm64/configs/nikel_defconfig` | kernel configuration (Trustonic TEE, nikel drivers, SHIFT-only parts off) |
| `drivers/misc/mediatek/lcm/nt35596_fhd_tianma_phantom/` | Tianma NT35596 1080p panel |
| `drivers/input/touchscreen/mediatek/ft5x0x/nikel_ft5346.c` | FT5346 touch plus the 3 capacitive keys. Read-only: it never writes touch firmware |
| `drivers/misc/mediatek/power/mt6797/bq24296*` | BQ24296 charger (4.4 V, JEITA) |
| `drivers/misc/mediatek/power/mt6797/nikel_cw2015.c` | CW2015 fuel gauge, read-only |
| `drivers/misc/mediatek/sensors-1.0/…/bmi160*-i2c` | BMI160 accelerometer/gyroscope over I2C |
| `drivers/misc/mediatek/sensors-1.0/alsps/ltr579` | LTR-579 light and proximity |
| `drivers/misc/mediatek/sensors-1.0/magnetometer/yas537` | YAS537 compass, ported from mainline `yamaha-yas530` |
| `drivers/leds/leds-nikel-button.c` | capacitive key backlight |
| `drivers/misc/mediatek/cmdq/v2/cmdq_mdp_meta.{c,h}` | newer MDP command-queue ioctls (video colour conversion, scaling, rotation) |
| `drivers/input/misc/nikel_fpc_irq.c` | FPC1145 fingerprint sensor interrupt, reset and SPI clock for the TEE |
| `drivers/misc/mediatek/imgsensor/src/mt6797/camera_project/nikel/` | camera power sequence, S5K3L8 main and S5K5E8YX front sensor drivers |

## Patches

| Patch | What it changes |
|---|---|
| `0001-lcm-register-nt35596-tianma-nikel` | registers the nikel panel driver |
| `0002-nikel-cw2015-battery-percentage` | battery percentage from the CW2015 gauge, with fallback to the PMIC gauge |
| `0003-nikel-disp-abi-for-shift-hwcomposer` | display ioctl layout expected by SHIFT's Oreo hwcomposer |
| `0004-nikel-speaker-amp` | speaker amplifier enable pin |
| `0005-nikel-ccci-drv-ver` | modem driver version flag needed by the Oreo modem init |
| `0006-nikel-bmi160-i2c-hooks` | Kconfig/Makefile hooks for the BMI160 drivers |
| `0007-nikel-ltr579-hooks` | hooks for the LTR-579 driver |
| `0008-nikel-yas537-hooks` | hooks for the YAS537 driver |
| `0009-nikel-headers-install-autoconf` | lets `make headers_install` work (needed for LineageOS 16) |
| `0010-nikel-button-backlight-hooks` | hooks for the key backlight driver |
| `0011-nikel-cmdq-op-meta-ioctls` | wires the new CMDQ ioctls into the command-queue driver |
| `0012-nikel-fpc-irq-hooks` | hooks for the fingerprint interrupt driver |
| `0013-nikel-camera-sensor-list-order` | sensor list order that matches SHIFT's camera HAL slots |
| `0014-nikel-main-af-dw9718` | main camera focus motor (DW9718, protocol from the stock kernel) |

## Status

**Working:**
- Display, touch and keys, key backlight
- Charging and battery percentage
- Wi-Fi, Bluetooth, GPS, FM
- Modem (2G)
- Audio including the speaker
- Sensors: accelerometer, gyroscope, light, proximity, compass
- IR blaster
- Video playback (MDP)
- Fingerprint (FPC1145)
- Front and main camera, flash and torch

**Not working yet:**
- Main camera autofocus: the focus motor does not answer on I2C, most likely an unpowered supply
- Touch firmware upgrade: left out on purpose, so the driver never writes firmware

## Safety

This overlay only builds a kernel; only the `boot` partition is ever replaced.
Never flash `preloader`, `lk`, the modem images, `tee`, `scp`, `nvram`,
`nvdata`, `nvcfg`, `proinfo` or `protect1/2`, and never use them from another
device.

## Credits

- SHIFTPHONES, for the open MT6797 kernel this builds on
- The Linux kernel `yamaha-yas530` driver (YAS537 support)
- MediaTek and the original driver authors, as noted in each file

## License

GPL-2.0, like the Linux kernel. See `LICENSE`.
