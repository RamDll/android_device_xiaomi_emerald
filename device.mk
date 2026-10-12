#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# clean-1: everything vendor-side comes from the stock 305 images (see BoardConfig.mk), so
# this file only configures the LineageOS system, system_ext, product, boot and vendor_boot.
# Product structure follows LineageOS motorola/mt6768-common and daylight/jagar.
#

# Enable project quotas and casefolding for emulated storage without sdcardfs
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Enforce generic ramdisk allow list
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

# Virtual A/B with compression (stock vendor: ro.virtual_ab.compression.enabled=true,
# userspace snapshots, xor, io_uring, batch writes, threads)
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/launch_with_vendor_ramdisk.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/vabc_features.mk)
PRODUCT_VIRTUAL_AB_COMPRESSION_METHOD := lz4

PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=erofs \
    POSTINSTALL_OPTIONAL_system=true

PRODUCT_PACKAGES += \
    otapreopt_script

# Dynamic partitions (stock super; as LineageOS daylight/jagar)
PRODUCT_USE_DYNAMIC_PARTITIONS := true
PRODUCT_BUILD_SUPER_PARTITION := true

# API levels (stock: ro.product.first_api_level=33)
PRODUCT_SHIPPING_API_LEVEL := 33

# Boot control: stock android.hardware.boot-service.mtk in vendor; recovery needs its own
# (AOSP default, as LineageOS daylight/jagar on mt6789)
PRODUCT_PACKAGES += \
    android.hardware.boot-service.default_recovery

# Fastbootd
PRODUCT_PACKAGES += \
    fastbootd

# Preloader partition links for OTA firmware updates (LineageOS hardware/mediatek)
PRODUCT_PACKAGES += \
    create_pl_dev \
    create_pl_dev.recovery

# Ramdisk (stock fstab, also used by first stage mount)
PRODUCT_PACKAGES += \
    fstab.mt6789

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/recovery/root/init.recovery.mt6789.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.mt6789.rc

# MediaTek system-side frameworks and overlays (LineageOS hardware/mediatek)
ENABLE_VENDOR_RIL_SERVICE := true

$(call inherit-product, hardware/lineage/compat/frameworks/compat.mk)
$(call inherit-product, hardware/mediatek/frameworks/mediatek-frameworks.mk)
$(call inherit-product, hardware/mediatek/overlay/mssi.mk)

PRODUCT_PACKAGES += \
    MtkInCallService

# MediaTek IMS (stock system_ext blobs, see proprietary-files.txt)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-com.mediatek.ims.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-com.mediatek.ims.xml \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-com.mediatek.telephony.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-com.mediatek.telephony.xml

# Library the stock VT blobs link against, built from source (stock ships it too)
PRODUCT_PACKAGES += \
    libhidltransport

# Platform
TARGET_BOARD_PLATFORM := mt6789

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
    hardware/mediatek

# Inherit the proprietary files
$(call inherit-product, vendor/xiaomi/emerald/emerald-vendor.mk)
