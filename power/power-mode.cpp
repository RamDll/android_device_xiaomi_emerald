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
#include <sys/ioctl.h>
#include <unistd.h>

#include <string>

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

bool testBit(int bit, const unsigned long* array) {
    constexpr int kBitsPerLong = sizeof(unsigned long) * 8;
    return (array[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1;
}

// Returns the touchscreen's input device, i.e. the one reporting multitouch positions.
unique_fd openTouchInputDevice() {
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
        unsigned long absBits[(ABS_MAX + 1) / (sizeof(unsigned long) * 8) + 1] = {};
        if (ioctl(fd.get(), EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) continue;
        if (testBit(ABS_MT_POSITION_X, absBits)) {
            return fd;
        }
    }
    LOG(ERROR) << "No touchscreen input device found";
    return {};
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
            // Re-arm finger detection on the UDFPS area whenever the screen goes off; the UDFPS
            // handler turns it off after each touch and nothing else re-enables it. Leave it alone
            // when the screen comes back, SystemUI and the handler drive it from there.
            if (enabled) setTouchMode(TOUCH_FOD_ENABLE, 1);
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
