/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * LK brings the panel up with its own DSI clock and the kernel display driver takes it over
 * as is; only the first real power-on reprograms the clock. Until then the panel runs a few
 * percent off its nominal rate (8.54 ms instead of 8.41 ms per frame at 120 Hz), which makes
 * 60 Hz video judder.
 *
 * The kernel doesn't even record LK's display in its DRM state: no CRTC is bound to the DSI
 * connector and none has a mode, so toggling DPMS or ACTIVE does nothing. Do a real modeset
 * (connector -> CRTC, the panel's preferred mode, ACTIVE=1), then switch everything off again.
 * That first real power-off drops LK's setup, and the composer's power-on programs the panel
 * the kernel's way. Runs once, before surfaceflinger starts.
 *
 * Disable with: adb shell setprop persist.log.tag.emerald_no_dispreset 1
 * The outcome is left in vendor.dispreset.result (mirrored to debug.dispreset.result).
 */

#define LOG_TAG "dispreset"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <log/log.h>
#include <drm_fourcc.h>
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

// The CRTC the connector can drive: its encoder's first possible CRTC.
static int findCrtcIndex(int fd, drmModeRes* res, drmModeConnector* conn) {
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoders[i]);
        if (!enc) continue;
        uint32_t possible = enc->possible_crtcs;
        drmModeFreeEncoder(enc);
        for (int c = 0; c < res->count_crtcs; c++) {
            if (possible & (1u << c)) return c;
        }
    }
    return -1;
}

static drmModeModeInfo* preferredMode(drmModeConnector* conn) {
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) return &conn->modes[i];
    }
    return conn->count_modes > 0 ? &conn->modes[0] : nullptr;
}

static uint32_t findPrimaryPlane(int fd, int crtcIndex) {
    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
    if (!planes) return 0;
    uint32_t found = 0;
    for (uint32_t i = 0; i < planes->count_planes && !found; i++) {
        drmModePlane* plane = drmModeGetPlane(fd, planes->planes[i]);
        if (!plane) continue;
        uint64_t type = 0;
        if ((plane->possible_crtcs & (1u << crtcIndex)) &&
            findProperty(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type) &&
            type == DRM_PLANE_TYPE_PRIMARY) {
            found = plane->plane_id;
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(planes);
    return found;
}

// A black framebuffer the size of the mode, for drivers that refuse a CRTC without a plane.
static uint32_t createBlackFb(int fd, uint32_t width, uint32_t height) {
    drm_mode_create_dumb create = {.height = height, .width = width, .bpp = 32};
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return 0;
    drm_mode_map_dumb map = {.handle = create.handle};
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0) {
        void* p = mmap(nullptr, create.size, PROT_WRITE, MAP_SHARED, fd, map.offset);
        if (p != MAP_FAILED) {
            memset(p, 0, create.size);
            munmap(p, create.size);
        }
    }
    uint32_t handles[4] = {create.handle}, pitches[4] = {create.pitch}, offsets[4] = {0};
    uint32_t fb = 0;
    if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb,
                      0) != 0) {
        fb = 0;
    }
    return fb;
}

struct Ids {
    uint32_t conn, connCrtcId;
    uint32_t crtc, crtcActive, crtcModeId;
    uint32_t plane, planeFbId, planeCrtcId, planeSrcX, planeSrcY, planeSrcW, planeSrcH,
            planeCrtcX, planeCrtcY, planeCrtcW, planeCrtcH;
};

static int commit(int fd, drmModeAtomicReq* req) {
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    return ret < 0 ? -errno : ret;
}

static int enable(int fd, const Ids& ids, uint32_t modeBlob, const drmModeModeInfo* mode,
                  uint32_t fb) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;
    drmModeAtomicAddProperty(req, ids.conn, ids.connCrtcId, ids.crtc);
    drmModeAtomicAddProperty(req, ids.crtc, ids.crtcModeId, modeBlob);
    drmModeAtomicAddProperty(req, ids.crtc, ids.crtcActive, 1);
    if (fb) {
        drmModeAtomicAddProperty(req, ids.plane, ids.planeFbId, fb);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcId, ids.crtc);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeSrcX, 0);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeSrcY, 0);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeSrcW, (uint64_t)mode->hdisplay << 16);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeSrcH, (uint64_t)mode->vdisplay << 16);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcX, 0);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcY, 0);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcW, mode->hdisplay);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcH, mode->vdisplay);
    }
    int ret = commit(fd, req);
    drmModeAtomicFree(req);
    return ret;
}

static int disable(int fd, const Ids& ids, bool withPlane) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;
    if (withPlane) {
        drmModeAtomicAddProperty(req, ids.plane, ids.planeFbId, 0);
        drmModeAtomicAddProperty(req, ids.plane, ids.planeCrtcId, 0);
    }
    drmModeAtomicAddProperty(req, ids.conn, ids.connCrtcId, 0);
    drmModeAtomicAddProperty(req, ids.crtc, ids.crtcActive, 0);
    drmModeAtomicAddProperty(req, ids.crtc, ids.crtcModeId, 0);
    int ret = commit(fd, req);
    drmModeAtomicFree(req);
    return ret;
}

static void powerCycle(int fd, drmModeRes* res, drmModeConnector* conn) {
    Ids ids = {};
    ids.conn = conn->connector_id;
    StringAppendF(&gResult, " conn=%u", ids.conn);

    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        StringAppendF(&gResult, " no-atomic(%d)", errno);
        return;
    }

    int crtcIndex = findCrtcIndex(fd, res, conn);
    drmModeModeInfo* mode = preferredMode(conn);
    if (crtcIndex < 0 || !mode) {
        StringAppendF(&gResult, " crtc-idx=%d modes=%d", crtcIndex, conn->count_modes);
        return;
    }
    ids.crtc = res->crtcs[crtcIndex];
    ids.connCrtcId = findProperty(fd, ids.conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    ids.crtcActive = findProperty(fd, ids.crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    ids.crtcModeId = findProperty(fd, ids.crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    StringAppendF(&gResult, " crtc=%u %ux%u@%u", ids.crtc, mode->hdisplay, mode->vdisplay,
                  mode->vrefresh);
    if (!ids.connCrtcId || !ids.crtcActive || !ids.crtcModeId) {
        StringAppendF(&gResult, " no-props");
        return;
    }

    uint32_t modeBlob = 0;
    if (drmModeCreatePropertyBlob(fd, mode, sizeof(*mode), &modeBlob) != 0) {
        StringAppendF(&gResult, " blob=%d", errno);
        return;
    }

    // First without a plane; some drivers want one, then retry with a black framebuffer.
    bool withPlane = false;
    int on = enable(fd, ids, modeBlob, mode, 0);
    StringAppendF(&gResult, " on=%d", on);
    if (on < 0) {
        ids.plane = findPrimaryPlane(fd, crtcIndex);
        uint32_t fb = ids.plane ? createBlackFb(fd, mode->hdisplay, mode->vdisplay) : 0;
        if (fb) {
            uint32_t p = ids.plane, t = DRM_MODE_OBJECT_PLANE;
            ids.planeFbId = findProperty(fd, p, t, "FB_ID");
            ids.planeCrtcId = findProperty(fd, p, t, "CRTC_ID");
            ids.planeSrcX = findProperty(fd, p, t, "SRC_X");
            ids.planeSrcY = findProperty(fd, p, t, "SRC_Y");
            ids.planeSrcW = findProperty(fd, p, t, "SRC_W");
            ids.planeSrcH = findProperty(fd, p, t, "SRC_H");
            ids.planeCrtcX = findProperty(fd, p, t, "CRTC_X");
            ids.planeCrtcY = findProperty(fd, p, t, "CRTC_Y");
            ids.planeCrtcW = findProperty(fd, p, t, "CRTC_W");
            ids.planeCrtcH = findProperty(fd, p, t, "CRTC_H");
            withPlane = true;
            on = enable(fd, ids, modeBlob, mode, fb);
            StringAppendF(&gResult, " plane=%u on=%d", ids.plane, on);
        } else {
            StringAppendF(&gResult, " no-fb(plane=%u)", ids.plane);
        }
    }

    if (on >= 0) {
        usleep(100 * 1000);
        int off = disable(fd, ids, withPlane);
        StringAppendF(&gResult, " off=%d", off);
    }
    drmModeDestroyPropertyBlob(fd, modeBlob);
}

int main() {
    if (android::base::GetProperty("persist.log.tag.emerald_no_dispreset", "") == "1") {
        gResult = "disabled";
        finish();
        return 0;
    }

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
