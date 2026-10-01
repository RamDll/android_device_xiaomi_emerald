/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.emerald"

#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include <dirent.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <cstdint>
#include <chrono>
#include <thread>

#include "UdfpsHandler.h"
#include "mi_disp.h"

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

#define COMMAND_FOD_PRESS_STATUS 1
#define COMMAND_FOD_PRESS_X 2
#define COMMAND_FOD_PRESS_Y 3
#define PARAM_FOD_PRESSED 1
#define PARAM_FOD_RELEASED 0

#define FOD_STATUS_PATH "/sys/class/touch/touch_dev/fod_press_status"
#define FOD_STATUS_OFF 0
#define FOD_STATUS_ON 1

#define SET_CUR_VALUE 0
#define Touch_Fod_Enable 10
#define TOUCH_MAGIC 't'
#define TOUCH_IOC_SETMODE _IO(TOUCH_MAGIC, SET_CUR_VALUE)

#define TOUCH_DEV_PATH "/dev/xiaomi-touch"
#define DISP_FEATURE_PATH "/dev/mi_display/disp_feature"

#define FINGERPRINT_ACQUIRED_VENDOR 7

using ::aidl::android::hardware::biometrics::fingerprint::AcquiredInfo;

namespace {

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

static disp_event_resp* parseDispEvent(int fd) {
    // Per thread: the FOD and the power event threads both parse events.
    thread_local char event_data[1024] = {0};
    ssize_t size = read(fd, event_data, sizeof(event_data));

    if (size < 0) {
        LOG(ERROR) << "read fod event failed";
        return nullptr;
    }
    if (size < sizeof(struct disp_event)) {
        LOG(ERROR) << "Invalid event size " << size << ", expect at least "
                   << sizeof(struct disp_event);
        return nullptr;
    }

    return (struct disp_event_resp*)&event_data[0];
}

}  // anonymous namespace

class XiaomiEmeraldUdfpsHandler : public UdfpsHandler {
  public:
    void init(fingerprint_device_t* device) {
        mDevice = device;
        disp_fd_ = android::base::unique_fd(open(DISP_FEATURE_PATH, O_RDWR));
        touch_fd_ = android::base::unique_fd(open(TOUCH_DEV_PATH, O_RDWR));

        // Thread to listen for fod ui changes
        std::thread([this]() {
            int fd = open(DISP_FEATURE_PATH, O_RDWR);
            if (fd < 0) {
                LOG(ERROR) << "failed to open " << DISP_FEATURE_PATH << " , err: " << fd;
                return;
            }

            // Register for FOD events
            disp_event_req req;
            req.base.flag = 0;
            req.base.disp_id = MI_DISP_PRIMARY;
            req.type = MI_DISP_EVENT_FOD;
            ioctl(fd, MI_DISP_IOCTL_REGISTER_EVENT, &req);

            struct pollfd dispEventPoll = {
                    .fd = fd,
                    .events = POLLIN,
                    .revents = 0,
            };

            while (true) {
                int rc = poll(&dispEventPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll " << DISP_FEATURE_PATH << ", err: " << rc;
                    continue;
                }

                struct disp_event_resp* response = parseDispEvent(fd);
                if (response == nullptr) {
                    continue;
                }

                if (response->base.type != MI_DISP_EVENT_FOD) {
                    LOG(ERROR) << "unexpected display event: " << response->base.type;
                    continue;
                }

                int value = response->data[0];
                LOG(DEBUG) << "received data: " << std::bitset<8>(value);

                bool localHbmUiReady = value & LOCAL_HBM_UI_READY;

                mDevice->extCmd(mDevice, COMMAND_NIT,
                                localHbmUiReady ? PARAM_NIT_FOD : PARAM_NIT_NONE);
            }
        }).detach();

        // Thread to listen for the finger leaving the sensor, as seen by the touch driver. The
        // framework drops the pointer-up when no client is running (after a successful unlock or
        // on the last enrollment step), so this is the only reliable finger-up.
        std::thread([this]() {
            int fd = open(FOD_STATUS_PATH, O_RDONLY);
            if (fd < 0) {
                LOG(ERROR) << "failed to open " << FOD_STATUS_PATH << " , err: " << fd;
                return;
            }

            struct pollfd fodPressPoll = {
                    .fd = fd,
                    .events = POLLERR | POLLPRI,
                    .revents = 0,
            };

            // Arm sysfs_notify
            readBool(fd);

            while (true) {
                int rc = poll(&fodPressPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll " << FOD_STATUS_PATH << ", err: " << rc;
                    continue;
                }

                if (readBool(fd)) {
                    continue;
                }

                LOG(DEBUG) << "finger up reported by the touch driver";
                authDoneAt_ = kNoAuth;
                ++pressGeneration_;
                if (fingerDown_) {
                    setFingerDown(false);
                }
            }
        }).detach();

        // Track the panel power state (MI_DISP_EVENT_POWER carries MI_DISP_DPMS_ON = 0 when the
        // screen is on). The HAL starts during boot with the screen on.
        std::thread([this]() {
            int fd = open(DISP_FEATURE_PATH, O_RDWR);
            if (fd < 0) {
                LOG(ERROR) << "failed to open " << DISP_FEATURE_PATH << " , err: " << fd;
                return;
            }

            disp_event_req req;
            req.base.flag = 0;
            req.base.disp_id = MI_DISP_PRIMARY;
            req.type = MI_DISP_EVENT_POWER;
            ioctl(fd, MI_DISP_IOCTL_REGISTER_EVENT, &req);

            struct pollfd powerPoll = {
                    .fd = fd,
                    .events = POLLIN,
                    .revents = 0,
            };

            while (true) {
                int rc = poll(&powerPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll " << DISP_FEATURE_PATH << ", err: " << rc;
                    continue;
                }

                struct disp_event_resp* response = parseDispEvent(fd);
                if (response == nullptr || response->base.type != MI_DISP_EVENT_POWER) {
                    continue;
                }
                screenOn_ = response->data[0] == 0;
            }
        }).detach();

        // Goodix: FOD is never turned off (see setTouchFod), but something has to turn it on.
        // With a Goodix fingerprint HAL the vendorCode 21/23 acquired events do that; an FPC HAL
        // doesn't send them, so after a reboot screen-off unlock stayed dead until the first
        // screen-on touch of the sensor. The driver keeps the flag across its own resets, so
        // enabling it once is enough, but it silently drops the write until its second init
        // stage is done, which may be after this HAL starts. So repeat it every 2 s for the
        // first 30 s, only while the screen is on: then the write just updates the flag, while
        // in gesture mode it would cost a chip reset that loses the charger mode.
        std::thread([this]() {
            for (int i = 0; i < 60 && touchKind() == TouchKind::UNKNOWN; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            if (touchKind() != TouchKind::GOODIX) return;
            LOG(INFO) << "Goodix touch: enabling FOD";
            for (int i = 0; i < 15; i++) {
                if (screenOn_) setTouchFod(1);
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }).detach();
    }

    void onFingerDown(uint32_t x, uint32_t y, float /*minor*/, float /*major*/) {
        LOG(DEBUG) << __func__ << "x: " << x << ", y: " << y;
        // Track x and y coordinates
        lastPressX = x;
        lastPressY = y;

        // After a successful capture SystemUI re-creates the UDFPS overlay and sends another
        // pointer-down while the same finger is still on the sensor. Don't light the sensor up
        // again for it; the flag is cleared on the real finger-up, on cancel or after a timeout.
        if (now() - authDoneAt_ < kSkipFingerDownAfterAuth) {
            LOG(DEBUG) << __func__ << ": finger still down after authentication, ignoring";
            return;
        }

        uint32_t generation = ++pressGeneration_;
        if (fodPressed()) {
            // Ensure touchscreen is aware of the press state, ideally this is not needed
            setFingerDown(true);
            return;
        }

        // On a quick tap the pointer-down reaches us after the finger has already left the
        // sensor, and lighting the sensor then only leaves a glow over an empty sensor. But the
        // touch firmware also raises its FOD flag a few scan frames after the first contact
        // (~13 ms seen on Goodix), so a pointer-down often arrives before it, and on a partial
        // touch (normal while enrolling) it may never raise it at all. So wait off the binder
        // thread: light the sensor once the flag goes up, or after the wait if the finger is
        // still down; skip only when the finger went up in the meantime.
        std::thread([this, generation]() {
            FodWait result = waitForFodPress();
            if (pressGeneration_ != generation) {
                LOG(DEBUG) << "onFingerDown: finger up while waiting, not lighting the sensor";
                return;
            }
            if (result == FodWait::RELEASED) {
                LOG(DEBUG) << "onFingerDown: quick tap, not lighting the sensor";
                return;
            }
            setFingerDown(true);
        }).detach();
    }

    void onFingerUp() {
        LOG(DEBUG) << __func__;
        ++pressGeneration_;
        // Ensure touchscreen is aware of the press state, ideally this is not needed
        setFingerDown(false);
    }

    void onAcquired(int32_t result, int32_t vendorCode) {
        LOG(DEBUG) << __func__ << " result: " << result << " vendorCode: " << vendorCode;
        if (result != FINGERPRINT_ACQUIRED_VENDOR) {
            if (static_cast<AcquiredInfo>(result) == AcquiredInfo::GOOD) {
                // Request to disable HBM already, even if the finger is still pressed
                authDoneAt_ = now();
                setLocalHbm(false);
                setFodStatus(FOD_STATUS_OFF);
            }
        } else if (vendorCode == 21 || vendorCode == 23) {
            /*
             * vendorCode = 21 waiting for a finger (new authentication or enrollment step)
             * vendorCode = 23 finger up acknowledged by the HAL
             * Both (re-)arm finger detection in the touch controller, which the screen-off
             * unlock relies on.
             */
            if (vendorCode == 21) {
                authDoneAt_ = kNoAuth;
            }
            setFodStatus(FOD_STATUS_ON);
        } else if (vendorCode == 44) {
            /*
             * vendorCode = 44 fingerprint scan failed
             */
            setFingerDown(false);
        }
    }

    void onAuthenticationSucceeded() {
        LOG(DEBUG) << __func__;
        setLocalHbm(false);
    }

    void onAuthenticationFailed() {
        LOG(DEBUG) << __func__;
        setLocalHbm(false);
    }

    void cancel() {
        LOG(DEBUG) << __func__;
        authDoneAt_ = kNoAuth;
        ++pressGeneration_;
        setLocalHbm(false);
        setFodStatus(FOD_STATUS_OFF);
    }

  private:
    // Upper bound for ignoring pointer-downs after a successful capture, in case the finger-up
    // from the touch driver is lost too.
    static constexpr int64_t kSkipFingerDownAfterAuth = 2000;  // ms
    static constexpr int64_t kNoAuth = INT64_MIN / 2;

    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
    }

    std::atomic<int64_t> authDoneAt_{kNoAuth};
    std::atomic<bool> screenOn_{true};
    std::atomic<bool> fingerDown_{false};
    // Bumped on every pointer-down, finger-up and cancel, so a pending FOD wait knows whether its
    // touch is still the current one.
    std::atomic<uint32_t> pressGeneration_{0};

    // How long a pointer-down waits for the touch firmware's FOD flag: well over the 2-3 scan
    // frames (~5 ms each) it usually takes.
    static constexpr int kFodPressWaitMs = 80;

    // The finger-up can get lost, e.g. on the last enrollment step the HAL reports the enrollment
    // done without ACQUIRED_GOOD and SystemUI's pointer-up then arrives with no client. Never
    // leave the 1000-nit white circle on for longer than this; a capture takes ~0.1-0.3 s.
    static constexpr auto kLocalHbmTimeout = std::chrono::milliseconds(1500);

    fingerprint_device_t* mDevice;
    std::atomic<uint32_t> lhbmGeneration_{0};

    void setLocalHbm(bool on) {
        uint32_t generation = ++lhbmGeneration_;

        disp_local_hbm_req req;
        req.base.flag = 0;
        req.base.disp_id = MI_DISP_PRIMARY;
        req.local_hbm_value =
                on ? LHBM_TARGET_BRIGHTNESS_WHITE_1000NIT : LHBM_TARGET_BRIGHTNESS_OFF_FINGER_UP;
        ioctl(disp_fd_.get(), MI_DISP_IOCTL_SET_LOCAL_HBM, &req);

        if (on) {
            std::thread([this, generation]() {
                std::this_thread::sleep_for(kLocalHbmTimeout);
                if (lhbmGeneration_ == generation) {
                    LOG(WARNING) << "No finger up after local HBM, switching it off";
                    setLocalHbm(false);
                }
            }).detach();
        }
    }
    android::base::unique_fd disp_fd_;
    android::base::unique_fd touch_fd_;
    uint32_t lastPressX, lastPressY;

    // emerald ships with a Focaltech (fts_ts) or a Goodix (goodix_ts) touchscreen. The answer is
    // only cached once one of them is found, the HAL may start before the touch driver.
    enum class TouchKind { UNKNOWN, FTS, GOODIX };

    static TouchKind touchKind() {
        static std::atomic<TouchKind> kind{TouchKind::UNKNOWN};
        if (kind != TouchKind::UNKNOWN) return kind;
        std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/dev/input"), closedir);
        if (!dir) return TouchKind::UNKNOWN;
        while (struct dirent* entry = readdir(dir.get())) {
            if (strncmp(entry->d_name, "event", 5) != 0) continue;
            std::string path = std::string("/dev/input/") + entry->d_name;
            android::base::unique_fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
            if (fd < 0) continue;
            char name[64] = {};
            if (ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) < 0) continue;
            if (strcmp(name, "goodix_ts") == 0) return kind = TouchKind::GOODIX;
            if (strcmp(name, "fts_ts") == 0) return kind = TouchKind::FTS;
        }
        return TouchKind::UNKNOWN;
    }

    static bool isGoodixTouch() { return touchKind() == TouchKind::GOODIX; }

    // Touch_Fod_Enable. The Goodix driver turns off finger detection when it enters gesture
    // mode with it off, and re-enabling it while suspended costs a chip reset that loses the
    // charger mode (with a charger plugged in the touch then fails every event). So on Goodix
    // keep it on all the time instead of toggling it per touch and re-arming at screen off.
    void setTouchFod(int value) {
        if (value == 0 && isGoodixTouch()) return;
        int arg[3] = {Touch_Fod_Enable, value};
        ioctl(touch_fd_, TOUCH_IOC_SETMODE, &arg);
    }

    // Whether the touch driver sees a finger on the sensor. Assume it does if the node can't be
    // read, so a missing node never keeps the sensor dark.
    static bool fodPressed() {
        android::base::unique_fd fd(open(FOD_STATUS_PATH, O_RDONLY | O_CLOEXEC));
        if (fd < 0) return true;
        char c;
        if (read(fd.get(), &c, 1) != 1) return true;
        return c != '0';
    }

    enum class FodWait { PRESSED, RELEASED, TIMEOUT };

    // Waits for fod_press_status to go to 1. xiaomi_touch sysfs_notify()s every change, and the
    // read before poll() arms it, so a change in between is not lost. Waking up to a 0 means it
    // went up and down again: a quick tap.
    static FodWait waitForFodPress() {
        android::base::unique_fd fd(open(FOD_STATUS_PATH, O_RDONLY | O_CLOEXEC));
        if (fd < 0) return FodWait::TIMEOUT;
        if (readBool(fd.get())) return FodWait::PRESSED;

        struct pollfd fodPressPoll = {
                .fd = fd.get(),
                .events = POLLERR | POLLPRI,
                .revents = 0,
        };
        int64_t deadline = now() + kFodPressWaitMs;
        for (int64_t left = kFodPressWaitMs; left > 0; left = deadline - now()) {
            int rc = poll(&fodPressPoll, 1, left);
            if (rc == 0) break;
            if (rc < 0) {
                if (errno == EINTR) continue;
                break;
            }
            return readBool(fd.get()) ? FodWait::PRESSED : FodWait::RELEASED;
        }
        return FodWait::TIMEOUT;
    }

    // fod_press_status is read-only (no store in xiaomi_touch), only the ioctl matters.
    void setFodStatus(int value) { setTouchFod(value); }

    void setFingerDown(bool pressed) {
        fingerDown_ = pressed;

        // xiaomi-touch
        setTouchFod(pressed ? 1 : 0);

        // Request HBM
        setLocalHbm(pressed);

        // Notify HAL of both press and release events
        mDevice->extCmd(mDevice, COMMAND_FOD_PRESS_STATUS,
                        pressed ? PARAM_FOD_PRESSED : PARAM_FOD_RELEASED);
    }
};

static UdfpsHandler* create() {
    return new XiaomiEmeraldUdfpsHandler();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
        .create = create,
        .destroy = destroy,
};
