/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.emerald"

#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include <poll.h>
#include <sys/ioctl.h>
#include <fstream>
#include <atomic>
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

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

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
    static char event_data[1024] = {0};
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
                if (fingerDown_) {
                    setFingerDown(false);
                }
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

        // Ensure touchscreen is aware of the press state, ideally this is not needed
        setFingerDown(true);
    }

    void onFingerUp() {
        LOG(DEBUG) << __func__;
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
    std::atomic<bool> fingerDown_{false};

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

    void setFodStatus(int value) {
        set(FOD_STATUS_PATH, value);
        int arg[3] = {Touch_Fod_Enable, value};
        ioctl(touch_fd_, TOUCH_IOC_SETMODE, &arg);
    }

    void setFingerDown(bool pressed) {
        fingerDown_ = pressed;

        // xiaomi-touch
        int arg[3] = {Touch_Fod_Enable, pressed ? 1 : 0};
        ioctl(touch_fd_, TOUCH_IOC_SETMODE, &arg);

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
