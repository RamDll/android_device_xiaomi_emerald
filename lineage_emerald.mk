#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from device makefile.
$(call inherit-product, device/xiaomi/emerald/device.mk)

# Inherit some common LineageOS stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Identity from the stock 305 props: ro.product.vendor.*, *_for_attestation, the odm
# ro.vendor.build.fingerprint (also the stock OTA post-build) and ro.build.description
PRODUCT_NAME := lineage_emerald
PRODUCT_DEVICE := emerald
PRODUCT_MANUFACTURER := Xiaomi
PRODUCT_BRAND := Redmi
PRODUCT_MODEL := 23117RA68G

PRODUCT_SYSTEM_NAME := emerald_global
PRODUCT_SYSTEM_DEVICE := emerald

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="missi-user 16 BP2A.250605.031.A3 OS3.0.305.0.WNFMIXM release-keys" \
    BuildFingerprint=Redmi/miodm_emerald/emerald:16/BP2A.250605.031.A3/OS3.0.305.0.WNFMIXM:user/release-keys \
    DeviceName=$(PRODUCT_SYSTEM_DEVICE) \
    DeviceProduct=$(PRODUCT_SYSTEM_NAME)
