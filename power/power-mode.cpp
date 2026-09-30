/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <aidl/android/hardware/power/BnPower.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/types.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#define SET_CUR_VALUE 0
#define TOUCH_DOUBLETAP_MODE 14
#define TOUCH_FOD_ENABLE 10
#define TOUCH_MAGIC 't'
#define TOUCH_IOC_SETMODE _IO(TOUCH_MAGIC, SET_CUR_VALUE)
#define TOUCH_DEV_PATH "/dev/xiaomi-touch"

// The FTS touch driver on emerald toggles its gesture (double tap) mode through a
// EV_SYN/SYN_CONFIG event written to its input device: 5 = on, 4 = off.
#define TOUCH_GESTURE_ON 5
#define TOUCH_GESTURE_OFF 4

// Xiaomi display feature events (see udfps/mi_disp.h).
#define DISP_FEATURE_PATH "/dev/mi_display/disp_feature"
#define BACKLIGHT_PATH "/sys/class/leds/lcd-backlight/brightness"
#define MI_DISP_PRIMARY 0
#define MI_DISP_EVENT_POWER 0
#define MI_DISP_DPMS_ON 0
#define MI_DISP_DPMS_POWERDOWN 5

struct disp_base {
    __u32 flag;
    __u32 disp_id;
};

struct disp_event_req {
    struct disp_base base;
    __u32 type;
};

struct disp_event {
    __s32 disp_id;
    __u32 type;
    __u32 length;
};

#define MI_DISP_IOCTL_REGISTER_EVENT _IOW('D', 0x07, struct disp_event_req)

namespace aidl {
namespace google {
namespace hardware {
namespace power {
namespace impl {
namespace pixel {

using ::aidl::android::hardware::power::Mode;
using ::android::base::unique_fd;

namespace {

// This kernel's xiaomi-touch driver takes {mode, value}, not {touch_id, mode, value}
// ("xiaomi_touch_dev_ioctl: cmd:0, mode:0, value:14" when a touch id is prepended).
void setTouchMode(int mode, int value) {
    unique_fd fd(open(TOUCH_DEV_PATH, O_RDWR | O_CLOEXEC));
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open " << TOUCH_DEV_PATH;
        return;
    }
    int arg[3] = {mode, value, 0};
    if (ioctl(fd.get(), TOUCH_IOC_SETMODE, &arg) < 0) {
        PLOG(ERROR) << "Failed to set touch mode " << mode << " to " << value;
    }
}

// emerald ships with either a Focaltech (fts_ts) or a Goodix (goodix_ts) touchscreen.
constexpr const char* kTouchNames[] = {"fts_ts", "goodix_ts"};

// Returns the touchscreen's input device (by name: "fts_ts,pen" also reports multitouch
// positions) and optionally its name.
unique_fd openTouchInputDevice(std::string* touchName = nullptr) {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/dev/input"), closedir);
    if (!dir) {
        PLOG(ERROR) << "Failed to open /dev/input";
        return {};
    }
    while (struct dirent* entry = readdir(dir.get())) {
        std::string name(entry->d_name);
        if (name.rfind("event", 0) != 0) continue;
        std::string path = "/dev/input/" + name;
        unique_fd fd(open(path.c_str(), O_RDWR | O_CLOEXEC));
        if (fd < 0) continue;
        char devName[64] = {};
        if (ioctl(fd.get(), EVIOCGNAME(sizeof(devName) - 1), devName) < 0) continue;
        for (const char* touch : kTouchNames) {
            if (strcmp(devName, touch) == 0) {
                if (touchName) *touchName = devName;
                return fd;
            }
        }
    }
    LOG(ERROR) << "No touchscreen input device found";
    return {};
}

bool isGoodixTouch() {
    static const bool goodix = [] {
        std::string name;
        openTouchInputDevice(&name);
        return name == "goodix_ts";
    }();
    return goodix;
}

void setDoubleTapToWake(bool enabled) {
    setTouchMode(TOUCH_DOUBLETAP_MODE, enabled ? 1 : 0);

    unique_fd fd = openTouchInputDevice();
    if (fd < 0) return;
    struct input_event ev = {};
    ev.type = EV_SYN;
    ev.code = SYN_CONFIG;
    ev.value = enabled ? TOUCH_GESTURE_ON : TOUCH_GESTURE_OFF;
    if (write(fd.get(), &ev, sizeof(ev)) != sizeof(ev)) {
        PLOG(ERROR) << "Failed to write gesture mode to the touch input device";
    }
}

// Panel ESD recovery. When the panel raises its error flag the MTK ESD check resets it
// (mtk_drm_esd_recover: DSI disable + enable) and the panel driver's lcm_esd_restore_backlight()
// then writes the maximum level (0x51 0x03FF), not the last one. Nothing tells the framework, so
// the screen stays at full brightness until the next brightness change. On stock Xiaomi's display
// service handles this. The reset shows up as a DSI power-down followed by a power-on within
// ~0.5 s, which a real screen off/on doesn't do; then write the current level back.
constexpr auto kEsdRecoveryWindow = std::chrono::milliseconds(1500);
// Let lcm_enable (and its backlight restore) finish before writing the level back.
constexpr auto kRestoreDelay = std::chrono::milliseconds(300);

void restoreBacklight() {
    std::string value;
    if (!::android::base::ReadFileToString(BACKLIGHT_PATH, &value)) {
        PLOG(ERROR) << "Failed to read " << BACKLIGHT_PATH;
        return;
    }
    int level = atoi(value.c_str());
    if (level <= 1) return;
    LOG(INFO) << "Panel reset by ESD recovery, restoring backlight level " << level;
    // The backlight driver skips a write of the level it already has, so step through level - 1.
    ::android::base::WriteStringToFile(std::to_string(level - 1), BACKLIGHT_PATH);
    ::android::base::WriteStringToFile(std::to_string(level), BACKLIGHT_PATH);
}

void watchPanelPower() {
    unique_fd fd(open(DISP_FEATURE_PATH, O_RDWR | O_CLOEXEC));
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open " << DISP_FEATURE_PATH;
        return;
    }
    disp_event_req req = {};
    req.base.disp_id = MI_DISP_PRIMARY;
    req.type = MI_DISP_EVENT_POWER;
    if (ioctl(fd.get(), MI_DISP_IOCTL_REGISTER_EVENT, &req) < 0) {
        PLOG(ERROR) << "Failed to register for display power events";
        return;
    }

    struct pollfd pfd = {.fd = fd.get(), .events = POLLIN, .revents = 0};
    auto powerDownAt = std::chrono::steady_clock::time_point::min();
    while (true) {
        if (poll(&pfd, 1, -1) < 0) continue;
        char buf[64];
        ssize_t size = read(fd.get(), buf, sizeof(buf));
        if (size < static_cast<ssize_t>(sizeof(disp_event) + 1)) continue;
        const auto* event = reinterpret_cast<const disp_event*>(buf);
        if (event->type != MI_DISP_EVENT_POWER) continue;
        const __u8 state = static_cast<__u8>(buf[sizeof(disp_event)]);
        const auto now = std::chrono::steady_clock::now();
        if (state == MI_DISP_DPMS_POWERDOWN) {
            powerDownAt = now;
        } else if (state == MI_DISP_DPMS_ON && now - powerDownAt < kEsdRecoveryWindow) {
            powerDownAt = std::chrono::steady_clock::time_point::min();
            std::this_thread::sleep_for(kRestoreDelay);
            restoreBacklight();
        }
    }
}

// Started with the power HAL process.
[[maybe_unused]] const bool kPanelWatcherStarted = [] {
    std::thread(watchPanelPower).detach();
    return true;
}();

}  // namespace

bool isDeviceSpecificModeSupported(Mode type, bool* _aidl_return) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
        case Mode::DISPLAY_INACTIVE:
            *_aidl_return = true;
            return true;
        default:
            return false;
    }
}

bool setDeviceSpecificMode(Mode type, bool enabled) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            setDoubleTapToWake(enabled);
            return true;
        case Mode::DISPLAY_INACTIVE:
            // Re-arm finger detection on the UDFPS area whenever the screen goes off; on the FTS
            // touchscreen the UDFPS handler turns it off after each touch and nothing else
            // re-enables it. Leave it alone when the screen comes back, SystemUI and the handler
            // drive it from there.
            // Not on Goodix: this arrives after the touch has entered gesture mode, where the
            // driver answers a mode change with a chip reset that loses its charger mode, and with
            // a charger plugged in every later touch/gesture event fails its checksum (no double
            // tap, no screen-off fingerprint). The UDFPS handler keeps FOD enabled there instead.
            if (enabled && !isGoodixTouch()) setTouchMode(TOUCH_FOD_ENABLE, 1);
            return true;
        default:
            return false;
    }
}

}  // namespace pixel
}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace google
}  // namespace aidl
