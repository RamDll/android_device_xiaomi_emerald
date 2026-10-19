/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// Product identity by hardware SKU, like the stock vendor does for its own partition (vendor
// build.prop: import /vendor/build_${ro.boot.product.hardware.sku}.prop). The values are the stock
// HyperOS 3 OS3.0.305.0.WNFMIXM build_glb*.prop ones. Helpers from LineageOS
// android_device_xiaomi_rosemary / sm8250-common libinit (libinit_utils, set_variant_props).

#include <string>
#include <vector>

#include <unistd.h>

#include <android-base/properties.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "vendor_init.h"

using android::base::GetProperty;

namespace {

struct Variant {
    std::string sku;
    std::string brand;
    std::string name;
    std::string marketname;
    std::string model;
    std::string fingerprint;
};

const std::vector<Variant> kVariants = {
        {"glb", "Redmi", "emerald_global", "Redmi Note 13 Pro", "23117RA68G",
         "Redmi/emerald_global/emerald:16/BP2A.250605.031.A3/OS3.0.305.0.WNFMIXM:user/release-keys"},
        {"glb_p", "POCO", "emerald_p_global", "POCO M6 Pro", "2312FPCA6G",
         "POCO/emerald_p_global/emerald:16/BP2A.250605.031.A3/OS3.0.305.0.WNFMIXM:user/release-keys"},
        {"glb_r", "Redmi", "emerald_r_global", "Redmi Note 14S", "2502FRA65G",
         "Redmi/emerald_r_global/emerald:16/BP2A.250605.031.A3/OS3.0.305.0.WNFMIXM:user/release-keys"},
};

void property_override(const std::string& prop, const std::string& value, bool add = true) {
    auto pi = (prop_info*)__system_property_find(prop.c_str());
    if (pi != nullptr) {
        __system_property_update(pi, value.c_str(), value.length());
    } else if (add) {
        __system_property_add(prop.c_str(), prop.length(), value.c_str(), value.length());
    }
}

void set_ro_build_prop(const std::string& prop, const std::string& value, bool product) {
    for (const std::string source : {"bootimage.", "odm.", "odm_dlkm.", "product.", "system.",
                                     "system_ext.", "vendor.", "vendor_dlkm.", ""}) {
        property_override(product ? "ro.product." + source + prop
                                  : "ro." + source + "build." + prop,
                          value);
    }
}

}  // namespace

void vendor_load_properties() {
    const std::string sku = GetProperty("ro.boot.product.hardware.sku", "");
    for (const auto& v : kVariants) {
        if (v.sku != sku) continue;

        set_ro_build_prop("brand", v.brand, true);
        set_ro_build_prop("name", v.name, true);
        set_ro_build_prop("marketname", v.marketname, true);
        set_ro_build_prop("model", v.model, true);

        if (access("/system/bin/recovery", F_OK) != 0) {
            property_override("bluetooth.device.default_name", v.marketname);
            set_ro_build_prop("fingerprint", v.fingerprint, false);
        }
        break;
    }
}
