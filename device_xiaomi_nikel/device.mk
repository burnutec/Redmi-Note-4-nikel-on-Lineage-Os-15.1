#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := device/xiaomi/nikel

# Blobs: SHIFT5me ShiftOS 8.1 vendor (SoC) + nikel stock (Trustonic)
$(call inherit-product, vendor/xiaomi/nikel/nikel-vendor.mk)

# Overlays
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Screen density (nikel stock: 480)
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xxhdpi
TARGET_SCREEN_HEIGHT := 1920
TARGET_SCREEN_WIDTH := 1080

# Boot ramdisk: SoC init from SHIFT's vendor (moved out of /vendor/etc/init/hw)
# plus nikel's own files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/fstab.mt6797:root/fstab.mt6797 \
    $(LOCAL_PATH)/rootdir/init.mt6797.rc:root/init.mt6797.rc \
    $(LOCAL_PATH)/rootdir/init.connectivity.rc:root/init.connectivity.rc \
    $(LOCAL_PATH)/rootdir/init.mt6797.usb.rc:root/init.mt6797.usb.rc \
    $(LOCAL_PATH)/rootdir/init.sensor_1_0.rc:root/init.sensor_1_0.rc \
    $(LOCAL_PATH)/rootdir/init.debug.rc:root/init.debug.rc \
    $(LOCAL_PATH)/rootdir/init.project.rc:root/init.project.rc \
    $(LOCAL_PATH)/rootdir/init.target.performance.rc:root/init.target.performance.rc \
    $(LOCAL_PATH)/rootdir/init.modem.rc:root/init.modem.rc \
    $(LOCAL_PATH)/rootdir/init.nikel.rc:root/init.nikel.rc \
    $(LOCAL_PATH)/rootdir/fstab.mt6797:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.mt6797

# ueventd: SHIFT's vendor ueventd.rc, also needed before /system is mounted
PRODUCT_COPY_FILES += \
    vendor/xiaomi/nikel/proprietary/vendor/ueventd.rc:root/ueventd.mt6797.rc

# Touch keys (nikel_ft5346 -> mtk-tpd-kpd: left=139, home=172, back=158)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/mtk-tpd-kpd.kl:system/usr/keylayout/mtk-tpd-kpd.kl

# NVRAM mountpoints inside system/vendor (see init.nikel.rc)
PRODUCT_PACKAGES += \
    nikel_vendor_mountpoints

# System-side libraries SHIFT's vendor blobs link against (found with
# nikel_check_deps.sh on the first build): libion for gralloc/hwcomposer/Mali,
# libxml2 for the audio parameter parser, the supplicant HIDL interface for
# wpa_supplicant, libcurl + libandroid_net for the GPS daemons.
PRODUCT_PACKAGES += \
    libion \
    libxml2 \
    libcurl \
    libandroid_net \
    android.hardware.wifi.supplicant@1.0

# Lights: our own HAL (liblight). SHIFT's light service drives a KTD2037 LED
# chip nikel does not have; it exits at start and system_server waits forever.
PRODUCT_PACKAGES += \
    lights.mt6797 \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service

# Modem start-up without the CDMA modem (MD3)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/etc/init/init.cccimdinit.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.cccimdinit.rc \
    $(LOCAL_PATH)/etc/init/init.cccifsd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.cccifsd.rc

# Audio routing: nikel's speaker amp input is the codec's speaker output
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio/audio_device.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_device.xml

# Audio: the HAL is SHIFT's android.hardware.audio@2.0-service-mediatek, and
# SHIFT's vendor already ships r_submix/usb modules, vndservicemanager and
# libtinyxml. Only A2DP (Bluetooth audio) comes from the Lineage build.
PRODUCT_PACKAGES += \
    audio.a2dp.default

# Later: FM radio app (MTK FMRadio needs libfmjni, not in Lineage 15.1)

# Properties
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=480 \
    ro.opengles.version=196610 \
    ro.config.low_ram=false \
    ro.mtk_wlan_support=1 \
    ro.mtk_bt_support=1 \
    ro.mtk_gps_support=1 \
    persist.sys.usb.config=mtp,adb \
    ro.adb.secure=0

# Dalvik / HWUI (3-4 GB RAM class)
$(call inherit-product-if-exists, frameworks/native/build/phone-xhdpi-4096-dalvik-heap.mk)
$(call inherit-product-if-exists, frameworks/native/build/phone-xxhdpi-4096-hwui-memory.mk)

# Consumer IR (IR blaster, kernel mt_irtx -> /dev/irtx, HAL from stock MIUI)
PRODUCT_PACKAGES += \
    android.hardware.ir@1.0-impl \
    android.hardware.ir@1.0-service

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.consumerir.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.consumerir.xml

# Fingerprint: MIUI's FPC HAL 2.0 (vendor/lib64/hw/fpc_fingerprint.miui.so)
# behind our 2.1 wrapper (fingerprint/), TA 0401... in mcRegistry,
# kernel nikel_fpc_irq for irq/reset/SPI clock.
PRODUCT_PACKAGES += \
    fingerprint.mt6797 \
    android.hardware.biometrics.fingerprint@2.1-service

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml
