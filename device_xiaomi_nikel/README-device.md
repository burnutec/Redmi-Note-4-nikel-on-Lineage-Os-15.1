# Device tree for Redmi Note 4 MTK (nikel) - LineageOS 15.1

| | |
|---:|:---|
| SoC | MediaTek Helio X20 (MT6797), Mali-T880 MP4 |
| Display | 5.5" 1080x1920, Tianma NT35596 (others not ported) |
| Touch | FocalTech FT5x46 (id 0x54) + 3 capacitive keys |
| Charger / gauge | TI BQ24296 / CellWise CW2015, 4100 mAh |
| TEE | Trustonic t-base 302C |
| Shipped | MIUI, Android 6.0 |

## Where things come from

| Part | Source |
|---|---|
| Kernel | SHIFT's MT6797 Oreo kernel (`shift-8.0-mtk`) + nikel drivers, built separately, used as `prebuilt/Image.gz-dtb` |
| SoC userspace (HALs, GPU, CONSYS, RIL, audio, media) | SHIFT5me ShiftOS 8.1 vendor image -> `proprietary-files.txt` (1150 files; 125 left out, each with a reason in `proprietary-files.excluded.txt`) |
| Trustonic TEE files, IR HAL, FPC fingerprint HAL | nikel stock MIUI V10.2.2.0.MBFCNXM -> `proprietary-files-nikel.txt` |
| Fingerprint | `fingerprint/`: our HAL 2.1 wrapper around MIUI's HAL 2.0; picks the vendor HAL from `ro.boot.fpsensor` (default `fpc`; nikel's bootloader does not set it, so Goodix detection is still to do). FPC1145 works |
| Init scripts | SHIFT's `vendor/etc/init/hw/*.rc` moved into the ramdisk (`rootdir/`), nikel's own `fstab.mt6797`, `init.nikel.rc`, `init.project.rc` |
| HIDL manifest | SHIFT's, minus boot control and keymaster/gatekeeper |

## Layout decisions

- **Non-Treble, vendor inside /system** (`TARGET_COPY_OUT_VENDOR := system/vendor`).
  No partition is changed. Because /vendor is not mounted when init and ueventd
  start, SHIFT's `init/hw/*.rc` and `ueventd.rc` are copied into the ramdisk.
- **NVRAM partitions are never formattable.** They are not in the fstab at all
  (recovery uses it too); `init.nikel.rc` mounts them at the /vendor paths
  SHIFT's binaries expect. Mountpoints are created in the system image.
- **Bring-up settings:** SELinux permissive, `ro.adb.secure=0`, software
  keystore/gatekeeper. All to be tightened later.

## Build

See `nikel_build/BUILDING.md` in this repository.

## Never flash

preloader, lk/lk2, md1img/md1dsp/md1arm7/md3img, tee1/tee2, scp1/scp2, nvram,
nvdata, nvcfg, proinfo, protect1/protect2.

## Camera

- HAL: SHIFT's `camerahalserver` (HAL1 device@1.0 behind `ICameraProvider/internal/0`), in
  `proprietary-files.txt` and `manifest.xml`. `init.nikel.rc` restarts it whenever cameraserver restarts.
- Sensors: the kernel lists them in SHIFT's slot order (see the kernel overlay README, patch 0013),
  so the HAL uses SHIFT's S5K3L8 tuning for the main camera and the S5K3P9SX slot for the front S5K5E8YX.
- Lens shading: SHIFT's 3A library got 0 sensor modes and built 0x0 shading tables (TSF crash, capture stall).
  `camera/patch_libcameracustom.py` also patches `libcam.hal3a.v3.so` to assume 5 modes. Shading and TSF stay on.
- Autofocus (not working yet; the motor does not answer on I2C, probably a power rail): `camera/patch_libcameracustom.py`, run by `extract-files.sh`, gives the main camera's
  sensor (S5K3L8, 0x30c8) the DW9714AF lens driver in SHIFT's lens list (on SHIFT the S5K3L8 is a front camera).
  The module's motor is really a DW9718 (stock MIUI lens table); the kernel serves the "DW9714AF" name
  with its DW9718 driver (kernel overlay patch 0014).
