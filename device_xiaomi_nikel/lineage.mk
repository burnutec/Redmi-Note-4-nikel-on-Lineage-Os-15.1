#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Lineage common
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Device
$(call inherit-product, device/xiaomi/nikel/device.mk)

PRODUCT_NAME := lineage_nikel
PRODUCT_DEVICE := nikel
PRODUCT_BRAND := Xiaomi
PRODUCT_MODEL := Redmi Note 4
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

# Stock nikel identity (MIUI V10.2.2.0.MBFCNXM)
PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=nikel \
    PRIVATE_BUILD_DESC="nikel-user 6.0 MRA58K V10.2.2.0.MBFCNXM release-keys"

BUILD_FINGERPRINT := Xiaomi/nikel/nikel:6.0/MRA58K/V10.2.2.0.MBFCNXM:user/release-keys
