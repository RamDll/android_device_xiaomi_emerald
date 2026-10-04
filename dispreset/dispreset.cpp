/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * LK brings the panel up with its own DSI clock and the kernel display driver takes it over
 * as is; only the first real power-on reprograms the clock. Until then the panel runs a few
 * percent off its nominal rate (seen: 8.54 / 8.14 / 8.77 ms instead of 8.41 ms at 120 Hz),
 * which makes 60 Hz video judder. Switch the display off and on once, while LK's splash is
 * still up and before the composer starts.
 *
 * logd may not be up yet at late-fs, so the outcome is also left in vendor.dispreset.result.
 */

#define LOG_TAG "dispreset"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <log/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

using android::base::StringAppendF;

static std::string gResult;

static void finish() {
    ALOGI("%s", gResult.c_str());
    android::base::SetProperty("vendor.dispreset.result", gResult.substr(0, 91));
}

static uint32_t findProperty(int fd, uint32_t objectId, uint32_t objectType, const char* name,
                             uint64_t* value = nullptr) {
    uint32_t id = 0;
    drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
    if (!props) return 0;
    for (uint32_t i = 0; i < props->count_props && !id; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
        if (!prop) continue;
        if (strcmp(prop->name, name) == 0) {
            id = prop->prop_id;
            if (value) *value = props->prop_values[i];
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return id;
}

static uint32_t findCrtc(int fd, drmModeRes* res, drmModeConnector* conn) {
    if (conn->encoder_id) {
        drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc) {
            uint32_t crtc = enc->crtc_id;
            drmModeFreeEncoder(enc);
            if (crtc) return crtc;
        }
    }
    // Not linked yet: take the first CRTC that has a mode (the one LK lit up).
    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc* crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;
        bool valid = crtc->mode_valid;
        uint32_t id = crtc->crtc_id;
        drmModeFreeCrtc(crtc);
        if (valid) return id;
    }
    return 0;
}

static int setCrtcActive(int fd, uint32_t crtcId, uint32_t activeProp, uint64_t active) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;
    int ret = drmModeAtomicAddProperty(req, crtcId, activeProp, active);
    if (ret >= 0) {
        ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
        if (ret < 0) ret = -errno;
    }
    drmModeAtomicFree(req);
    return ret;
}

static void powerCycleLegacy(int fd, uint32_t connectorId) {
    uint32_t dpms = findProperty(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "DPMS");
    if (!dpms) {
        StringAppendF(&gResult, " no-dpms");
        return;
    }
    int off = drmModeConnectorSetProperty(fd, connectorId, dpms, DRM_MODE_DPMS_OFF);
    usleep(50 * 1000);
    int on = drmModeConnectorSetProperty(fd, connectorId, dpms, DRM_MODE_DPMS_ON);
    StringAppendF(&gResult, " dpms off=%d on=%d", off, on);
}

static void powerCycle(int fd, drmModeRes* res, drmModeConnector* conn) {
    StringAppendF(&gResult, " conn=%u", conn->connector_id);

    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        StringAppendF(&gResult, " no-atomic(%d)", errno);
        powerCycleLegacy(fd, conn->connector_id);
        return;
    }

    uint32_t crtcId = findCrtc(fd, res, conn);
    if (!crtcId) {
        StringAppendF(&gResult, " no-crtc");
        powerCycleLegacy(fd, conn->connector_id);
        return;
    }

    uint64_t active = 0, modeId = 0;
    uint32_t activeProp = findProperty(fd, crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE", &active);
    findProperty(fd, crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID", &modeId);
    StringAppendF(&gResult, " crtc=%u active=%u mode=%u", crtcId, (unsigned)active,
                  (unsigned)modeId);
    if (!activeProp) {
        StringAppendF(&gResult, " no-active-prop");
        powerCycleLegacy(fd, conn->connector_id);
        return;
    }

    int off = setCrtcActive(fd, crtcId, activeProp, 0);
    usleep(50 * 1000);
    int on = setCrtcActive(fd, crtcId, activeProp, 1);
    StringAppendF(&gResult, " off=%d on=%d", off, on);
}

int main() {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        StringAppendF(&gResult, "open=%d", errno);
        finish();
        return 0;
    }
    StringAppendF(&gResult, "master=%d", drmIsMaster(fd));
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        StringAppendF(&gResult, " res=%d", errno);
        finish();
        close(fd);
        return 0;
    }

    bool found = false;
    for (int i = 0; i < res->count_connectors && !found; i++) {
        drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) continue;
        if (conn->connection == DRM_MODE_CONNECTED &&
            conn->connector_type == DRM_MODE_CONNECTOR_DSI) {
            found = true;
            powerCycle(fd, res, conn);
        }
        drmModeFreeConnector(conn);
    }
    if (!found) StringAppendF(&gResult, " no-dsi");

    finish();
    drmModeFreeResources(res);
    close(fd);
    return 0;
}
