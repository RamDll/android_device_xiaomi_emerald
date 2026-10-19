/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Turns the double-tap-to-wake gesture of the touch panel on (argument 1) or off (0).
 *
 * emerald ships with a FocalTech (fts_ts) or a Goodix (goodix_ts) panel. Both stock drivers take the
 * gesture switch only as an EV_SYN/SYN_CONFIG event written to their input device: value 5 = on,
 * 4 = off ("fts_gesture_switch: gesture enabled:1"; Goodix: gsx_gesture_switch sets the double tap
 * bit 0x80 of its gesture type, the same bit as its gesture/double_en sysfs node). The xiaomi-touch
 * mode 14 that ITouchFeature offers is refused by fts ("game mode:14 don't support") and only stored
 * by Goodix. While the panel sleeps the drivers apply the switch at the next suspend. A double tap
 * then reports KEY_WAKEUP (fts) or KEY_POWER (Goodix).
 * Found by hand on clean-2.3, 2026-10-18 (sendevent /dev/input/event5 0 1 5, fts_ts); Goodix from
 * the stock gt9916.ko (input_dev->event = gsx_gesture_switch) and a tester's log on clean-2.4.
 */

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include <android-base/logging.h>
#include <android-base/unique_fd.h>

static constexpr char kInputDir[] = "/dev/input";
static constexpr const char* kTouchNames[] = {"fts_ts", "goodix_ts"};
static constexpr int kGestureOn = 5;
static constexpr int kGestureOff = 4;

static bool IsTouchPanel(const char* name) {
    for (const char* touch_name : kTouchNames) {
        if (strcmp(name, touch_name) == 0) {
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));

    if (argc != 2 || (strcmp(argv[1], "0") != 0 && strcmp(argv[1], "1") != 0)) {
        LOG(ERROR) << "usage: " << argv[0] << " 0|1";
        return 2;
    }
    const bool enable = argv[1][0] == '1';

    DIR* dir = opendir(kInputDir);
    if (dir == nullptr) {
        PLOG(ERROR) << "opendir " << kInputDir;
        return 1;
    }
    bool found = false;
    while (dirent* entry = readdir(dir)) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        const std::string path = std::string(kInputDir) + "/" + entry->d_name;
        android::base::unique_fd fd(open(path.c_str(), O_WRONLY | O_CLOEXEC));
        if (fd < 0) {
            PLOG(WARNING) << "open " << path;
            continue;
        }
        char name[64] = {};
        if (ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) < 0 || !IsTouchPanel(name)) {
            continue;
        }
        found = true;
        input_event event = {};
        event.type = EV_SYN;
        event.code = SYN_CONFIG;
        event.value = enable ? kGestureOn : kGestureOff;
        if (TEMP_FAILURE_RETRY(write(fd.get(), &event, sizeof(event))) != sizeof(event)) {
            PLOG(ERROR) << "write " << path;
            closedir(dir);
            return 1;
        }
        LOG(INFO) << "double tap to wake " << (enable ? "on" : "off") << " (" << path << ", " << name
                  << ")";
        break;
    }
    closedir(dir);
    if (!found) {
        LOG(ERROR) << "no fts_ts or goodix_ts input device";
        return 1;
    }
    return 0;
}
