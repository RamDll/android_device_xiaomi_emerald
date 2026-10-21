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

# Video calls (ViLTE / ViWiFi) off. The stock vendor build.prop turns them on, but the video call service
# (vtservice, media.VTS) is not shipped on 24.0. With either prop at 1, com.mediatek.ims calls
# libmtk_vt_wrapper from a NetworkCallback.onAvailable (ImsVTProviderUtil) and getVTClient waits for
# media.VTS forever, blocking the app's ConnectivityThread: no later network callback arrives, so after
# the first IMS PDN loss (LTE off/on, a short LTE drop) ImsDataSynchronizer never confirms the release to
# the modem and IMS stays unregistered until a reboot (clean-2.4 bugreports, 2026-10-19). Product props
# are loaded after vendor's and override them.
PRODUCT_PRODUCT_PROPERTIES += \
    persist.vendor.vilte_support=0 \
    persist.vendor.viwifi_support=0

# Audio: libaudiohal (AIDL) passes setParameters keys it does not know to the HAL only through an
# IHalAdapterVendorExtension service; without one they are dropped, so the stock MediaTek HAL never
# got MtkInCallService's volumeIndex (in-call volume stuck at the framework's -56 gain step at 15/15,
# clean-2.2 call log 2026-10-18). Stock HyperOS ships the same service in system_ext and sets the prop
# in system/build.prop. See audio/parameter_parser.
PRODUCT_PACKAGES += \
    android.hardware.audio.parameter_parser.service

PRODUCT_SYSTEM_PROPERTIES += \
    ro.audio.ihaladaptervendorextension_enabled=true

# Audio: the stock MediaTek "default" (primary) AIDL module does not implement
# Module::populateConnectedDevicePort, so connectExternalDevice works only with device connection
# simulation on: without it every wired headset plug failed (APM -38, tester + our phone on 2.2,
# 2026-10-18; worked again after `service call media.audio_flinger 63 i32 1`). Our frameworks/av
# (clean-2) turns simulation on for the modules listed here, right after loading them.
PRODUCT_SYSTEM_PROPERTIES += \
    ro.audio.simulate_device_connections=primary

# Dolby: the stock vendor's DAX3 DAP (libswdapaidl) has no system-side app on LineageOS. XiaomiDolby
# (dolby/, from AOSPA) attaches it to the output mix; our frameworks/av (clean-2) keeps the stock
# Dolby volume listeners on music sessions from suspending it. It replaces AudioFX.
PRODUCT_PACKAGES += \
    XiaomiDolby

# Camera: Aperture's lens buttons and 60 fps video (ApertureOverlayEmerald). 60 fps needs our Aperture
# (clean-2 branch of the RamDll fork), which requests MediaTek HFPS above 30 fps.
PRODUCT_PACKAGES += \
    ApertureOverlayEmerald

# MTP: report the phone as a mobile handset (perceived device type 3); with the default 0 Windows
# shows a generic media player icon. MtpDatabase reads it at the start of every MTP session.
PRODUCT_PRODUCT_PROPERTIES += \
    sys.usb.mtp.device_type=3

# Display
# libgui deadlocks in BLASTBufferQueue::initialize when maxAcquired + 4 > the frame event history
# size (default 8): the stock vendor's debug.sf.*duration props give maxAcquired 5 at 120 Hz, so the
# panel stayed on the lk logo (clean-1.0/1.1). 10 fixed it on the phone (2026-10-14, via the debug
# vendor_boot); see notes.md "Display root cause". The same code path is in libgui of 24.0.
PRODUCT_SYSTEM_PROPERTIES += \
    ro.lib_gui.frame_event_history_size=10

# Frame timestamps to MediaTek GED (frameworks/native clean-2 "libgui: Report frame timestamps to
# MediaTek GED KPI"; the stock HyperOS libgui always reports them): without them the GPU stays at
# 470-500 MHz in activity transitions, Settings 16 % janky frames -> 6 % with (jank_trans.sh, clean-2.2,
# 2026-10-18). A debug prop, so it can be switched off with adb for A/B runs.
PRODUCT_SYSTEM_PROPERTIES += \
    debug.lib_gui.mtk_ged_kpi=1

# SurfaceFlinger RenderEngine on Vulkan, as on the old r26-r30.3 and clean-1.2 trees; HWUI already
# uses Vulkan (stock vendor ro.hwui.use_vulkan=true). Runtime test on clean-2.8 (2026-10-20): with
# blur the shade janked 3.5-3.8 % instead of 4.2-4.4 % on SkiaGL and SF missed 4-11 frames instead of
# 24-30; the protected Vulkan context initializes. Nothing sets it on the stock partitions; a debug
# prop, so `setprop debug.renderengine.backend skiaglthreaded` + stop/start switches back for A/B runs.
PRODUCT_PRODUCT_PROPERTIES += \
    debug.renderengine.backend=skiavkthreaded

# Brightness map for the AOSP lights HAL path (stock's is for the HyperOS one); the minimum used to be
# backlight level 1 = black. Goes with the brightness range in FrameworksResOverlayEmerald.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/displayconfig/display_port_0.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/displayconfig/display_port_0.xml

# Refresh rate selection as the official LineageOS xiaomi_mt6895-common (MediaTek): content detection
# and idle/touch timers 1100 ms. With the stock vendor's idle timer (600 ms) a screen that updates once a
# second (Settings > About phone, clock counters, the network traffic indicator) flips 120 -> 60 -> 120 Hz
# every second, visible as flicker: 60 mode switches in 30 s on clean-2.0. The stock vendor build.prop sets
# debug.sf.set_idle_timer_ms=600 too and SurfaceFlinger prefers it over ro.surface_flinger.set_idle_timer_ms,
# so the ro props alone changed nothing (59 switches). Product props are loaded after vendor's and override
# both. Tested on clean-2.0 through an adb overlay (2026-10-16): 0 switches in 30 s, static screen 60 Hz.
PRODUCT_PRODUCT_PROPERTIES += \
    debug.sf.set_idle_timer_ms=1100 \
    ro.surface_flinger.use_content_detection_for_refresh_rate=true \
    ro.surface_flinger.set_idle_timer_ms=1100 \
    ro.surface_flinger.set_touch_timer_ms=1100 \
    ro.surface_flinger.set_display_power_timer_ms=1000

# Max / min refresh rate lists in Settings > Display (SettingsResOverlayEmerald).
PRODUCT_PACKAGES += \
    SettingsResOverlayEmerald

# Window and shade blur, as stock OS3.0.305.0 (its system_ext.prop sets the same prop on this vendor and
# Mali r54p1 driver). On clean-2.7 this prop was the only missing condition (SystemUI BlurUtils:
# isHighEndGfx true, disable_window_blurs 0). The off switch is AOSP's: Settings > Accessibility >
# Color and motion "Reduce blur effects" / Developer options "Allow window-level blurs".
# Not measured yet with our RenderEngine (SkiaGL): shade jank A/B with blur on/off after installing.
PRODUCT_PRODUCT_PROPERTIES += \
    ro.surface_flinger.supports_background_blur=1

# Fingerprint (stock HAL): FOD touch mode + scanner state, and the UDFPS icon size / illumination
# overlays. The /dev/mi_display node for local HBM comes from system/core (clean-2 branch: the
# mi_display rule in rootdir/ueventd.rc; init reads only /system/etc/ueventd.rc). Same as clean-1.2.1,
# tested on clean-2.0 through an adb overlay (2026-10-16): enroll + unlock work.
PRODUCT_PACKAGES += \
    EmeraldUdfps \
    FrameworksResOverlayEmerald \
    SystemUIResOverlayEmerald

# Double tap to wake: the touch gesture switch run by init for EmeraldUdfps (touch/dt2w.cpp).
PRODUCT_PACKAGES += \
    emerald_dt2w

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/keylayout/goodix_ts.kl:$(TARGET_COPY_OUT_PRODUCT)/usr/keylayout/goodix_ts.kl

# Screen-off fingerprint unlock: EmeraldUdfps's key handler in PhoneWindowManager (lineage-sdk
# config_deviceKeyHandler*). A build-time overlay, not an RRO: system_server reads lineage-sdk
# resources from the zygote's system assets, which take no RRO for lineageos.platform (an adb-installed
# RRO was ignored, 2026-10-18).
PRODUCT_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay-lineage

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

# No "Serial console" notification on userdebug: stop the console shell init.rc starts on debuggable
# builds (emerald boots with console=ttynull, the shell is attached to nothing).
PRODUCT_PACKAGES += \
    emerald_noconsole.rc

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
