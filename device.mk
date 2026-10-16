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

# MediaTek telephony overlays that hardware/mediatek builds for vendor, copied to product (see
# overlays/*Product/Android.bp): MMTEL ImsService package, device VoLTE/VT/WFC availability.
PRODUCT_PACKAGES += \
    MssiFrameworkTelephonyOverlayProduct \
    MssiTelephonyOverlayProduct

# VoLTE/WFC on carriers whose AOSP carrier config says no (Tele2/Rostelecom 250 20 on the user's
# SIM, clean-1.1 2026-10-14: no toggle without these; with them VoLTE registered and worked). No VT
# override: the video call service is not shipped on 24.0 (see proprietary-files.txt).
PRODUCT_SYSTEM_PROPERTIES += \
    persist.dbg.volte_avail_ovr=1 \
    persist.dbg.wfc_avail_ovr=1

# Display
# libgui deadlocks in BLASTBufferQueue::initialize when maxAcquired + 4 > the frame event history
# size (default 8): the stock vendor's debug.sf.*duration props give maxAcquired 5 at 120 Hz, so the
# panel stayed on the lk logo (clean-1.0/1.1). 10 fixed it on the phone (2026-10-14, via the debug
# vendor_boot); see notes.md "Display root cause". The same code path is in libgui of 24.0.
PRODUCT_SYSTEM_PROPERTIES += \
    ro.lib_gui.frame_event_history_size=10

# Fingerprint (stock HAL): FOD touch mode + scanner state, and the UDFPS icon size / illumination
# overlays. The /dev/mi_display node for local HBM comes from system/core (clean-2 branch: the
# mi_display rule in rootdir/ueventd.rc; init reads only /system/etc/ueventd.rc). Same as clean-1.2.1,
# tested on clean-2.0 through an adb overlay (2026-10-16): enroll + unlock work.
PRODUCT_PACKAGES += \
    EmeraldUdfps \
    FrameworksResOverlayEmerald \
    SystemUIResOverlayEmerald

# Bluetooth profiles. The Bluetooth module enables a profile only when its sysprop is true
# (A2dpService.isEnabled() = isProfileA2dpSourceEnabled().orElse(false)); stock sets these in the
# HyperOS system + product build.prop, which clean-2 doesn't ship, so only GATT (BLE: the watch),
# HearingAid and SAP ran and headphones had no A2DP / HFP (clean-1.2.x and 2.0). Stock OS3.0.305.0
# set; LE Audio profiles off as on stock.
PRODUCT_PRODUCT_PROPERTIES += \
    bluetooth.profile.a2dp.source.enabled=true \
    bluetooth.profile.asha.central.enabled=true \
    bluetooth.profile.avrcp.target.enabled=true \
    bluetooth.profile.bas.client.enabled=true \
    bluetooth.profile.gatt.enabled=true \
    bluetooth.profile.hfp.ag.enabled=true \
    bluetooth.profile.hid.device.enabled=true \
    bluetooth.profile.hid.host.enabled=true \
    bluetooth.profile.map.server.enabled=true \
    bluetooth.profile.opp.enabled=true \
    bluetooth.profile.pan.nap.enabled=true \
    bluetooth.profile.pan.panu.enabled=true \
    bluetooth.profile.pbap.server.enabled=true \
    bluetooth.profile.sap.server.enabled=true \
    bluetooth.profile.bap.broadcast.assist.enabled=false \
    bluetooth.profile.bap.broadcast.source.enabled=false \
    bluetooth.profile.bap.unicast.client.enabled=false \
    bluetooth.profile.ccp.server.enabled=false \
    bluetooth.profile.csip.set_coordinator.enabled=false \
    bluetooth.profile.hap.client.enabled=false \
    bluetooth.profile.mcp.server.enabled=false \
    bluetooth.profile.vcp.controller.enabled=false

# Platform
TARGET_BOARD_PLATFORM := mt6789

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
    hardware/mediatek

# Inherit the proprietary files
$(call inherit-product, vendor/xiaomi/emerald/emerald-vendor.mk)

# GApps (MindTheGapps cinnamonbun, vendor/gapps in the local manifest; user, 2026-10-14 and
# 2026-10-15). Required: a build without them must fail instead of quietly shipping no Play services.
$(call inherit-product, vendor/gapps/arm64/arm64-vendor.mk)
