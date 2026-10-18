/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Turns the double-tap-to-wake gesture of the FocalTech touch panel on (argument 1) or off (0).
 *
 * The stock fts_ts driver takes the gesture switch only as an EV_SYN/SYN_CONFIG event written to its
 * input device: value 5 = on, 4 = off ("fts_gesture_switch: gesture enabled:1"). The xiaomi-touch
 * mode 14 that ITouchFeature offers is refused ("game mode:14 don't support"). While the panel
 * sleeps the driver applies the switch at the next suspend. A double tap then reports KEY_WAKEUP.
 * Found by hand on clean-2.3, 2026-10-18 (sendevent /dev/input/event5 0 1 5).
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
static constexpr char kTouchName[] = "fts_ts";
static constexpr int kGestureOn = 5;
static constexpr int kGestureOff = 4;

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
        if (ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) < 0 || strcmp(name, kTouchName) != 0) {
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
        LOG(INFO) << "double tap to wake " << (enable ? "on" : "off") << " (" << path << ")";
        break;
    }
    closedir(dir);
    if (!found) {
        LOG(ERROR) << "no " << kTouchName << " input device";
        return 1;
    }
    return 0;
}
