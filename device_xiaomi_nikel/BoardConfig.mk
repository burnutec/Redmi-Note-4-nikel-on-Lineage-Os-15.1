#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Redmi Note 4 (MTK, "nikel") - LineageOS 15.1, non-Treble, bring-up build.
# SoC userspace: SHIFT5me ShiftOS 8.1 vendor blobs. Kernel: SHIFT's MT6797
# Oreo kernel + nikel board drivers (built separately, used as a prebuilt).

DEVICE_PATH := device/xiaomi/nikel

# Architecture (MT6797 / Helio X20: 2x A72 + 8x A53)
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := cortex-a53

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a53

TARGET_BOARD_PLATFORM := mt6797
TARGET_BOOTLOADER_BOARD_NAME := mt6797
TARGET_NO_BOOTLOADER := true
BOARD_HAS_MTK_HARDWARE := true
MTK_HARDWARE := true

# Kernel: prebuilt Image.gz-dtb from our kernel build (nikel_setup_and_build.sh).
# Copy nikel-Image.gz-dtb to prebuilt/Image.gz-dtb before building.
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/Image.gz-dtb
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb

# boot.img header values as on nikel (identical to the TWRP image that boots
# our kernel): kernel 0x40080000, ramdisk 0x45000000, tags 0x44000000
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00080000
BOARD_RAMDISK_OFFSET := 0x05000000
BOARD_KERNEL_TAGS_OFFSET := 0x04000000
BOARD_MKBOOTIMG_ARGS := \
    --kernel_offset $(BOARD_KERNEL_OFFSET) \
    --ramdisk_offset $(BOARD_RAMDISK_OFFSET) \
    --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)

BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2 androidboot.hardware=mt6797
# Bring-up: SELinux permissive (the vendor services have no nikel policy yet)
BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive
# Note: nikel's LK keeps only ~100 characters of this command line, so the
# modem options SHIFT's radio daemons need are set in init.nikel.rc instead.

# Partitions (bytes) - measured on nikel: /proc/partitions, 16 GB unit.
# userdata differs on 32/64 GB units; it is only used for userdata.img, which
# is never flashed.
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_CACHEIMAGE_PARTITION_SIZE := 452984832
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11118033920
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# No vendor partition: vendor lives in /system/vendor
TARGET_COPY_OUT_VENDOR := system/vendor

# Display (5.5" 1080x1920)
TARGET_SCREEN_HEIGHT := 1920
TARGET_SCREEN_WIDTH := 1080
USE_OPENGL_RENDERER := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
# SHIFT's hwcomposer.mt6797.so is a HWC2 module: SurfaceFlinger must use
# the HWC2 path (through the composer@2.1 service), or it calls HWC1 hooks
# that do not exist and crashes in queryDisplayProperties().
TARGET_USES_HWC2 := true

# Wi-Fi: wifi HAL service, libwifi-hal, wpa_supplicant and hostapd all come prebuilt
# from SHIFT's vendor. BOARD_WLAN_DEVICE stays unset: "MediaTek" would make the
# build compile libwifi-hal-mt66xx, whose source is not in Lineage.
WIFI_DRIVER_FW_PATH_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_FW_PATH_STA := STA
WIFI_DRIVER_FW_PATH_AP := AP
WIFI_DRIVER_FW_PATH_P2P := P2P
WIFI_HIDL_FEATURE_DISABLE_AP_MAC_RANDOMIZATION := true

# Bluetooth (HAL prebuilt from SHIFT's vendor: android.hardware.bluetooth@1.0-service-mediatek)
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_MTK := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# HIDL: SHIFT's vendor manifest minus the HALs this build does not start
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml

# Recovery (Lineage recovery; TWRP stays installed separately)
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/fstab.mt6797
TARGET_RECOVERY_PIXEL_FORMAT := "RGBA_8888"

# SELinux: empty device policy for now (permissive bring-up)
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy

# Misc
TARGET_EXFAT_DRIVER := sdfat
BLOCK_BASED_OTA := false

# SoC / telephony properties for SHIFT's blobs (see system.prop)
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop

# Proprietary blobs
-include vendor/xiaomi/nikel/BoardConfigVendor.mk

# Boot animation: nikel's own story animation (bootanimation/bootanim.py renders it)
TARGET_BOOTANIMATION := $(DEVICE_PATH)/bootanimation/bootanimation.zip

# ROM zip also flashes the boot logo (releasetools.py, radio/logo.bin)
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)
