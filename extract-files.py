#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2025 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.fixups_blob import blob_fixups_user_type, blob_fixup
from extract_utils.fixups_lib import (
    lib_fixup_remove_arch_suffix,
    lib_fixup_remove_proto_version_suffix,
    lib_fixup_vendorcompat,
    lib_fixups_user_type,
    libs_clang_rt_ubsan,
    libs_proto_3_9_1,
    libs_proto_21_12,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'device/xiaomi/emerald',
    "hardware/mediatek",
    'hardware/mediatek/libaedv',
    "hardware/mediatek/libmtkperf_client",
    "hardware/xiaomi"
]


lib_fixups: lib_fixups_user_type = {
    libs_clang_rt_ubsan: lib_fixup_remove_arch_suffix,
    libs_proto_3_9_1: lib_fixup_vendorcompat,
    libs_proto_21_12: lib_fixup_remove_proto_version_suffix,
}


def fixup_ndk_platform(libname: str) -> tuple[str, str]:
    """
    Replace -ndk_platform with -ndk
    """
    return (libname, libname.replace("-ndk_platform.so", "-ndk.so"))


patchelf_version = "0_17_2"

def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    ('vendor.mediatek.hardware.videotelephony@1.0',): lib_fixup_vendor_suffix,
}

blob_fixups: blob_fixups_user_type = {
    "vendor/lib64/hw/audio.primary.mediatek.so": blob_fixup()
    .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    "vendor/bin/hw/android.hardware.security.keymint@1.0-service.mitee": blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed(
        "android.hardware.security.keymint-V1-ndk_platform.so",
        "android.hardware.security.keymint-V3-ndk-v34.so",
    )
    .add_needed("android.hardware.security.rkp-V3-ndk.so")
    .replace_needed(
        *fixup_ndk_platform("android.hardware.security.secureclock-V1-ndk_platform.so")
    )
    .replace_needed(
        *fixup_ndk_platform("android.hardware.security.sharedsecret-V1-ndk_platform.so")
    ),
    'vendor/bin/hw/mtkfusionrild': blob_fixup()
        .add_needed('libutils-v32.so'),
    "vendor/etc/init/android.hardware.graphics.allocator@4.0-service-mediatek.rc": blob_fixup().regex_replace(
        "android.hardware.graphics.allocator@4.0-service-mediatek",
        "mt6789/android.hardware.graphics.allocator@4.0-service-mediatek.mt6789",
    ),
    (
        "vendor/lib/libwvhidl.so",
        "vendor/lib/mediadrm/libwvdrmengine.so",
        "vendor/lib64/libwvhidl.so",
        "vendor/lib64/mediadrm/libwvdrmengine.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libprotobuf-cpp-lite-3.9.1.so", "libprotobuf-cpp-full-3.9.1.so"),
    (
        "vendor/bin/mnld",
        "vendor/lib64/hw/android.hardware.sensors@2.X-subhal-mediatek.so",
        "vendor/lib64/mt6789/libaalservice.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libsensorndkbridge.so", "android.hardware.sensors@1.0-convert-shared.so"),
    "vendor/lib64/mt6789/libcam.utils.sensorprovider.so": blob_fixup()
    .add_needed("android.hardware.sensors@1.0-convert-shared.so"),
    # The stock Codec2 HAL is built against the Android 12 ("for Android 31") codec2 libs, e.g. it
    # allocates 0x108 bytes for utils::ComponentStore while the A16 libcodec2_hidl@1.2 constructs
    # RefBase at +0x110 -> heap corruption at every start. Run it on the stock codec2 set (-v31)
    # with a single libstagefright_foundation (-v33) in the whole process, like xiaomi/earth.
    "vendor/bin/hw/android.hardware.media.c2@1.2-mediatek-64b": blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libavservices_minijail_vendor.so", "libavservices_minijail.so")
    .replace_needed("libcodec2_hidl@1.0.so", "libcodec2_hidl@1.0-v31.so")
    .replace_needed("libcodec2_hidl@1.1.so", "libcodec2_hidl@1.1-v31.so")
    .replace_needed("libcodec2_hidl@1.2.so", "libcodec2_hidl@1.2-v31.so")
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so"),
    (
        "vendor/lib64/libcodec2_hidl@1.0-v31.so",
        "vendor/lib64/libcodec2_hidl@1.1-v31.so",
        "vendor/lib64/libcodec2_hidl@1.2-v31.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libstagefright_bufferqueue_helper.so", "libstagefright_bufferqueue_helper-v31.so")
    .replace_needed("libcodec2_hidl@1.0.so", "libcodec2_hidl@1.0-v31.so")
    .replace_needed("libcodec2_hidl@1.1.so", "libcodec2_hidl@1.1-v31.so")
    .replace_needed("libcodec2_hidl_plugin.so", "libcodec2_hidl_plugin-v31.so")
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so")
    .replace_needed("libui.so", "libui-v34.so")
    .add_needed("libbase_shim.so"),
    "vendor/lib64/libcodec2_hidl_plugin-v31.so": blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so"),
    "vendor/lib64/libcodec2_vndk-v31.so": blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libui.so", "libui-v34.so")
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so"),
    (
        "vendor/lib64/libcodec2_soft_common-v31.so",
        "vendor/lib64/libsfplugin_ccodec_utils-v31.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so")
    .replace_needed("libsfplugin_ccodec_utils.so", "libsfplugin_ccodec_utils-v31.so")
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so"),
    "vendor/lib64/libstagefright_bufferqueue_helper-v31.so": blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so"),
    (
        "vendor/lib64/libcodec2_mtk_c2store.so",
        "vendor/lib64/libcodec2_vpp_qt_plugin.so",
        "vendor/lib64/libcodec2_vpp_rs_plugin.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libcodec2_soft_common.so", "libcodec2_soft_common-v31.so")
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so")
    .replace_needed("libsfplugin_ccodec_utils.so", "libsfplugin_ccodec_utils-v31.so")
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so"),
    "vendor/etc/init/android.hardware.media.c2@1.2-mediatek.rc": blob_fixup()
    .regex_replace("@1.2-mediatek", "@1.2-mediatek-64b"),
    # MemoryDevice(bool secure) picks its backend once per process from the first non-secure
    # instance (DMA here, there is no /dev/ion). If the secure AVC encoder is the first one
    # created, e.g. right after the HAL restarted mid codec-list enumeration, the type is still
    # unknown and it abort()s, so the HAL crash-loops. Fall through into the DMA path instead
    # (b.ne -> nop at 0x9e224), which is what the secure encoder uses on every good boot.
    # Same code (and byte pattern) in the decoder library, b.ne -> nop at 0x9a7e0: there the secure
    # decoders crash-loop the HAL the same way.
    (
        "vendor/lib64/libcodec2_mtk_vdec.so",
        "vendor/lib64/libcodec2_mtk_venc.so",
    ): blob_fixup()
    .binary_regex_replace(
        b"\xa8\x02\x40\xb9\x1f\x05\x00\x71\x40\x05\x00\x54\x1f\x0d\x00\x71\xc1\x03\x00\x54",
        b"\xa8\x02\x40\xb9\x1f\x05\x00\x71\x40\x05\x00\x54\x1f\x0d\x00\x71\x1f\x20\x03\xd5",
    )
    .patchelf_version(patchelf_version)
    .replace_needed("libcodec2_soft_common.so", "libcodec2_soft_common-v31.so")
    .replace_needed("libcodec2_vndk.so", "libcodec2_vndk-v31.so")
    .replace_needed("libsfplugin_ccodec_utils.so", "libsfplugin_ccodec_utils-v31.so")
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so")
    .replace_needed("libui.so", "libui-v34.so"),
    "vendor/etc/init/android.hardware.bluetooth@1.1-service-mediatek.rc": blob_fixup().regex_replace(
        "on property:vts(.|\n)*", ""
    ),
    "vendor/etc/init/android.hardware.neuralnetworks-shim-service-mtk.rc": blob_fixup().regex_replace(
        "start", "enable"
    ),
    (
        "vendor/lib64/libteei_daemon_vfs.so",
        "vendor/lib64/mt6789/lib3a.flash.so",
        "vendor/lib64/mt6789/libaaa_ltm.so",
        "vendor/lib64/mt6789/lib3a.ae.stat.so",
        "vendor/lib64/mt6789/lib3a.sensors.color.so",
        "vendor/lib64/mt6789/lib3a.sensors.flicker.so",
        "vendor/lib64/libSQLiteModule_VER_ALL.so",
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .add_needed("liblog.so"),
    (
        "vendor/lib64/mt6789/libmtkcam_stdutils.so",
        "vendor/lib64/hw/mt6789/android.hardware.camera.provider@2.6-impl-mediatek.so"
    ): blob_fixup()
    .patchelf_version(patchelf_version)
    .replace_needed("libutils.so", "libutils-v32.so"),
    "vendor/lib64/libmorpho_video_stabilizer.so": blob_fixup()
    .add_needed("libutils.so"),
    "vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so": blob_fixup()
    .patchelf_version(patchelf_version)
    .add_needed("android.hardware.sensors@1.0-convert-shared.so")
    .replace_needed("libutils.so", "libutils-v32.so")
    .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/lib64/libmorpho_Ldc.so',
        'vendor/lib64/libTrueSight.so',
	    'vendor/lib64/libMiVideoFilter.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_acquire')
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_lockPlanes')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock'),
    ('vendor/lib64/hw/android.hardware.gnss-impl-mediatek.so', 'vendor/bin/hw/android.hardware.gnss-service.mediatek'): blob_fixup()
    .replace_needed('android.hardware.gnss-V1-ndk_platform.so', 'android.hardware.gnss-V1-ndk.so'),
    'vendor/lib64/mt6789/libmnl.so': blob_fixup()
    .add_needed('libcutils.so'),
    'vendor/lib64/libdlbdsservice.so': blob_fixup()
    .replace_needed("libstagefright_foundation.so", "libstagefright_foundation-v33.so"),
    'vendor/lib64/mt6789/libneuralnetworks_sl_driver_mtk_prebuilt.so': blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_createFromHandle')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_getNativeHandle')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock')
        .add_needed('libbase_shim.so'),
    ('vendor/lib64/libnvram.so', 'vendor/lib64/libsysenv.so'): blob_fixup()
        .add_needed('libbase_shim.so'),
    'vendor/lib64/hw/hwcomposer.mtk_common.so': blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    'vendor/lib64/hw/vendor.xiaomi.sensor.citsensorservice@2.0-impl.so': blob_fixup()
        .add_needed('libui_shim.so')
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    'vendor/lib64/librt_extamp_intf.so': blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
}  # fmt: skip

module = ExtractUtilsModule(
    'emerald',
    'xiaomi',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
    check_elf=True,
    add_firmware_proprietary_file=True,
)

if __name__ == "__main__":
    utils = ExtractUtils.device(module)
    utils.run()
