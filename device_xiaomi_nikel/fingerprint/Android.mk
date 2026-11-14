# Fingerprint HAL wrapper for nikel: loads MIUI's HAL 2.0 for the sensor the
# bootloader detected (ro.boot.fpsensor) and presents it as HAL 2.1 to
# android.hardware.biometrics.fingerprint@2.1-service.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := fingerprint.mt6797
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MULTILIB := 64
LOCAL_SRC_FILES := fingerprint_wrapper.c
LOCAL_SHARED_LIBRARIES := liblog libcutils libdl
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
