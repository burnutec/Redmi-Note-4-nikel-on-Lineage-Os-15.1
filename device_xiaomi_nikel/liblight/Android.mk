# Legacy lights HAL for nikel (backlight, touch-key backlight, RGB
# notification LED). Wrapped by Lineage's android.hardware.light@2.0-impl.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := lights.mt6797
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := lights.c
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
