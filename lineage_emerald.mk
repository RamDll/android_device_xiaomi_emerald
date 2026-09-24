#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit from emerald device
$(call inherit-product, device/xiaomi/emerald/device.mk)

PRODUCT_DEVICE := emerald
PRODUCT_NAME := lineage_emerald
# Present as the POCO M6 Pro (2312FPCA6G) — the variant this build is used on.
PRODUCT_BRAND := POCO
PRODUCT_MODEL := 2312FPCA6G
PRODUCT_MANUFACTURER := xiaomi

PRODUCT_SYSTEM_NAME := emerald_p_id
PRODUCT_SYSTEM_DEVICE := emerald

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

# Fingerprint of the stock POCO build this tree's blobs come from (OS2.0.208.0.VNFIDXM).
PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildFingerprint=POCO/emerald_p_id/emerald:15/AP3A.240905.015.A2/OS2.0.208.0.VNFIDXM:user/release-keys \
    DeviceName=$(PRODUCT_SYSTEM_DEVICE) \
    DeviceProduct=$(PRODUCT_SYSTEM_NAME)
