#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# clean-2: LineageOS 24.0 system on the unmodified stock HyperOS 3 Global OS3.0.305.0.WNFMIXM
# vendor, vendor_dlkm, odm_dlkm and system_dlkm images. Values marked "stock" were read from
# the 305 images (boot/vendor_boot headers, super metadata, vendor props).
#

DEVICE_PATH := device/xiaomi/emerald
KERNEL_PATH := device/xiaomi/emerald-kernel
IMAGES_PATH := vendor/xiaomi/emerald-images

# Architecture (mt6789: 2x Cortex-A76 + 6x Cortex-A55; as LineageOS daylight/jagar, xelex/Q25)
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-2a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := cortex-a76

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-2a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := cortex-a55

# A/B
AB_OTA_UPDATER := true

AB_OTA_PARTITIONS += \
    boot \
    dtbo \
    odm_dlkm \
    product \
    system \
    system_dlkm \
    system_ext \
    vbmeta \
    vbmeta_system \
    vbmeta_vendor \
    vendor \
    vendor_boot \
    vendor_dlkm

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := emerald
TARGET_NO_BOOTLOADER := true

# Boot image (stock: boot header v4, kernel at 0x40000000, ramdisk at 0x66f00000,
# tags/dtb at 0x47c80000, page size 4096)
BOARD_BOOT_HEADER_VERSION := 4
BOARD_KERNEL_BASE := 0x3fff8000
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_RAMDISK_OFFSET := 0x26f08000
BOARD_KERNEL_TAGS_OFFSET := 0x07c88000
BOARD_DTB_OFFSET := 0x07c88000

BOARD_MKBOOTIMG_ARGS := --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --dtb_offset $(BOARD_DTB_OFFSET)
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)

# Stock vendor_boot command line and bootconfig
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2
BOARD_BOOTCONFIG := \
    kernel.rcu_nocbs=all \
    kernel.rcutree.enable_rcu_lazy=1 \
    kernel.rcupdate.rcu_cpu_stall_cputime=1

BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := true
BOARD_RAMDISK_USE_LZ4 := true

# Display (stock ro.sf.lcd_density)
TARGET_SCREEN_DENSITY := 480

# Health: LineageOS charging control switches the charger input off and on (no bypass). The
# service ships prebuilt in vendor.img (prebuilts/lineage-health); these are its build flags.
$(call soong_config_set,lineage_health,charging_control_charging_path,/sys/class/power_supply/battery/input_suspend)
$(call soong_config_set,lineage_health,charging_control_charging_enabled,0)
$(call soong_config_set,lineage_health,charging_control_charging_disabled,1)
$(call soong_config_set_bool,lineage_health,charging_control_supports_bypass,false)

# Init: product identity (brand / model / marketname / fingerprint) by hardware SKU, as the stock
# vendor picks build_${ro.boot.product.hardware.sku}.prop for its own partition (libinit/)
$(call soong_config_set,libinit,vendor_init_lib,//$(DEVICE_PATH):init_emerald)

# Kernel: stock GKI, see device/xiaomi/emerald-kernel/README.md
BOARD_USES_GENERIC_KERNEL_IMAGE := true
BOARD_KERNEL_IMAGE_NAME := Image.lz4
TARGET_PREBUILT_KERNEL := $(KERNEL_PATH)/Image.lz4
TARGET_PREBUILT_KERNEL_HEADERS := $(KERNEL_PATH)/kernel-uapi-headers.tar.gz

BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_PREBUILT_DTBIMAGE_DIR := $(KERNEL_PATH)/dtb
BOARD_KERNEL_SEPARATED_DTBO := true
BOARD_PREBUILT_DTBOIMAGE := $(KERNEL_PATH)/dtbo.img

# Kernel modules: only the vendor ramdisk set is packed by the build; vendor_dlkm and
# system_dlkm ship as stock images (below).
EMERALD_RAMDISK_MODULES_DIR := $(KERNEL_PATH)/vendor_ramdisk
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(EMERALD_RAMDISK_MODULES_DIR)/modules.load))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(EMERALD_RAMDISK_MODULES_DIR)/modules.load.recovery))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(wildcard $(EMERALD_RAMDISK_MODULES_DIR)/*.ko)

# Partitions (stock sizes)
BOARD_FLASH_BLOCK_SIZE := 262144 # (BOARD_KERNEL_PAGESIZE * 64)
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_DTBOIMG_PARTITION_SIZE := 8388608

# Stock super: 7516192768 bytes, groups main_a/main_b of 7505707008 bytes each
BOARD_SUPER_PARTITION_SIZE := 7516192768
BOARD_SUPER_PARTITION_GROUPS := main
BOARD_MAIN_SIZE := 7505707008
BOARD_MAIN_PARTITION_LIST := \
    odm_dlkm \
    product \
    system \
    system_dlkm \
    system_ext \
    vendor \
    vendor_dlkm

BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := erofs

TARGET_COPY_OUT_ODM_DLKM := odm_dlkm
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_SYSTEM_DLKM := system_dlkm
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_VENDOR_DLKM := vendor_dlkm

BOARD_USES_METADATA_PARTITION := true

-include vendor/lineage/config/BoardConfigReservedSize.mk

# Stock vendor-side images (copied by extract-images.sh), used as they are except
# vendor.img and vendor_dlkm.img, which get the fixes in patch-images.py
BOARD_PREBUILT_VENDORIMAGE := $(IMAGES_PATH)/vendor.patched.img
BOARD_PREBUILT_VENDOR_DLKMIMAGE := $(IMAGES_PATH)/vendor_dlkm.patched.img
BOARD_PREBUILT_ODM_DLKMIMAGE := $(IMAGES_PATH)/odm_dlkm.img
BOARD_PREBUILT_SYSTEM_DLKMIMAGE := $(IMAGES_PATH)/system_dlkm.img

# Recovery (in vendor_boot; stock fstab and pixel format)
BOARD_INCLUDE_RECOVERY_RAMDISK_IN_VENDOR_BOOT := true
BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.mt6789
TARGET_RECOVERY_PIXEL_FORMAT := BGRA_8888
TARGET_USERIMAGES_USE_F2FS := true

# SELinux: LineageOS' MediaTek policy, included whole as daylight/jagar and xelex/Q25 do.
# On the phone the vendor policy is the stock one inside vendor.img; the vendor part built here
# only goes into recovery and the build's own checks (system_ext file_contexts label
# /data/log_wifi_temp with logtemp_data_file, which only base/vendor defines; stock vendor defines it too).
include device/mediatek/sepolicy_vndr/SEPolicy.mk

# Compat map of the system_ext public types for the stock vendor's 202504 policy (see the file)
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/private

# Security patch level (stock vendor)
VENDOR_SECURITY_PATCH := 2026-06-05

# Verified Boot (as LineageOS daylight/jagar; verification off, the bootloader is unlocked)
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3

BOARD_AVB_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa2048.pem
BOARD_AVB_BOOT_ALGORITHM := SHA256_RSA2048
BOARD_AVB_BOOT_ROLLBACK_INDEX := 1
BOARD_AVB_BOOT_ROLLBACK_INDEX_LOCATION := 1

BOARD_AVB_VBMETA_SYSTEM := product system system_ext
BOARD_AVB_VBMETA_SYSTEM_KEY_PATH := external/avb/test/data/testkey_rsa2048.pem
BOARD_AVB_VBMETA_SYSTEM_ALGORITHM := SHA256_RSA2048
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX := 1
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX_LOCATION := 2

BOARD_AVB_VBMETA_VENDOR := vendor
BOARD_AVB_VBMETA_VENDOR_KEY_PATH := external/avb/test/data/testkey_rsa2048.pem
BOARD_AVB_VBMETA_VENDOR_ALGORITHM := SHA256_RSA2048
BOARD_AVB_VBMETA_VENDOR_ROLLBACK_INDEX := 1
BOARD_AVB_VBMETA_VENDOR_ROLLBACK_INDEX_LOCATION := 3

# VINTF: the device manifest and device matrix are the stock ones inside vendor.img.
# Framework matrices: stock system and product fragments (Xiaomi/MediaTek vendor HALs).
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE := \
    $(DEVICE_PATH)/vintf/framework_compatibility_matrix_system.xml \
    $(DEVICE_PATH)/vintf/framework_compatibility_matrix_product.xml

# Inherit the proprietary files
include vendor/xiaomi/emerald/BoardConfigVendor.mk
