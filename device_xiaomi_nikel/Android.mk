#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),nikel)

# Mountpoints for nikel's NVRAM/calibration partitions. SHIFT's Oreo binaries
# expect them under /vendor, which on nikel is the read-only /system/vendor,
# so the empty directories must already exist in the system image.
include $(CLEAR_VARS)
LOCAL_MODULE := nikel_vendor_mountpoints
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := etc/nikel_vendor_mountpoints.txt
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc
LOCAL_POST_INSTALL_CMD := \
    mkdir -p $(addprefix $(TARGET_OUT_VENDOR)/,nvdata nvcfg protect_f protect_s persist)
include $(BUILD_PREBUILT)

# Boot logo (MTK logo partition), flashed by the ROM zip: see releasetools.py
$(call add-radio-file,radio/logo.bin)

# Sub-directories last: their Android.mk files change LOCAL_PATH
include $(call all-makefiles-under,$(LOCAL_PATH))

endif
