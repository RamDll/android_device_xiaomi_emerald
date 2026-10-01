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
 */

#define LOG_TAG "dispreset"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>

#include <log/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static uint32_t findDpmsProperty(int fd, uint32_t connectorId) {
    uint32_t id = 0;
    drmModeObjectProperties* props =
            drmModeObjectGetProperties(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) return 0;
    for (uint32_t i = 0; i < props->count_props && !id; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
        if (!prop) continue;
        if (strcmp(prop->name, "DPMS") == 0) id = prop->prop_id;
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return id;
}

int main() {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("failed to open /dev/dri/card0: %s", strerror(errno));
        return 0;
    }

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        ALOGE("failed to get DRM resources: %s", strerror(errno));
        close(fd);
        return 0;
    }

    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->connector_type == DRM_MODE_CONNECTOR_DSI) {
            uint32_t dpms = findDpmsProperty(fd, conn->connector_id);
            if (!dpms) {
                ALOGE("connector %u has no DPMS property", conn->connector_id);
            } else {
                int off = drmModeConnectorSetProperty(fd, conn->connector_id, dpms,
                                                      DRM_MODE_DPMS_OFF);
                usleep(50 * 1000);
                int on = drmModeConnectorSetProperty(fd, conn->connector_id, dpms,
                                                     DRM_MODE_DPMS_ON);
                ALOGI("connector %u: DPMS off -> %d, on -> %d", conn->connector_id, off, on);
            }
        }
        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    close(fd);
    return 0;
}
