/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * LiveDisplay for emerald: display modes, picture adjustment (hue, saturation, intensity), color
 * calibration (red, green, blue) and reading mode, applied together as one 3x3 color matrix by the
 * CCORR block of the display pipeline. Nothing is composed on the GPU: a SurfaceFlinger color matrix
 * (what LiveDisplay and Night Light use without a HAL) makes the MTK HWC compose every layer on the
 * GPU (clean-2.12, 2026-10-28).
 *
 * The matrix goes to the stock MediaTek picture quality HAL,
 * vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL/default (version 7), as
 * setColorMatrix3x3(in int[3][3] matrix, int hint) = transaction 51 (its V7 NDK proxy). That
 * interface is not in our sources, so the transaction is written by hand. Found on clean-2.14,
 * 2026-10-29, with `service call` and the user watching the screen:
 * - the coefficients are fixed point with 1024 = 1.0 (a red tint 1024/400/400 looked as expected;
 *   the panel's CCORR is 13 bit, the HAL converts);
 * - rows are output channels, matrix[i][j] = the share of input channel j in output channel i
 *   (with the rows G, B, R the red YouTube icon turned blue and the green battery icon red);
 * - hint 1 applies the matrix at once, hint 0 left the screen unchanged;
 * - the matrix stays after the screen goes off and on again.
 *
 * A 3x3 matrix cannot change the contrast, so the contrast is the global contrast of the DISP_COLOR
 * block instead: setPQIndex(int level, int scenario, int tuningMode, int index, int step) =
 * transaction 87, index 9 = SET_PQ_CONTRAST (the case of PQService::setPQParamlevel that logs that
 * name). The level selects an entry of the panel's CONTRAST table (cust_color_dsi_n6_4*.xml,
 * 101 entries, 50 = 128 = 1.0, 0 = 0.31, 100 = 1.56); the PQ HAL keeps it in
 * persist.vendor.sys.pq.contrastidx. Level 100 made the bright parts visibly brighter (clean-2.14,
 * 2026-10-29).
 *
 * The framework keeps and re-applies the picture adjustment, the calibration and reading mode;
 * the display mode is kept here, in persist.vendor.livedisplay.mode.
 */

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <aidl/vendor/lineage/livedisplay/BnDisplayColorCalibration.h>
#include <aidl/vendor/lineage/livedisplay/BnDisplayModes.h>
#include <aidl/vendor/lineage/livedisplay/BnPictureAdjustment.h>
#include <aidl/vendor/lineage/livedisplay/BnReadingEnhancement.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using aidl::vendor::lineage::livedisplay::BnDisplayColorCalibration;
using aidl::vendor::lineage::livedisplay::BnDisplayModes;
using aidl::vendor::lineage::livedisplay::BnPictureAdjustment;
using aidl::vendor::lineage::livedisplay::BnReadingEnhancement;
using aidl::vendor::lineage::livedisplay::DisplayMode;
using aidl::vendor::lineage::livedisplay::FloatRange;
using aidl::vendor::lineage::livedisplay::HSIC;
using ndk::ScopedAStatus;

namespace {

using Matrix = std::array<std::array<float, 3>, 3>;

constexpr char kPqInstance[] = "vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL/default";
constexpr char kPqDescriptor[] = "vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL";
constexpr transaction_code_t kSetColorMatrix3x3 = 51;
constexpr transaction_code_t kSetPQIndex = 87;
constexpr transaction_code_t kGetInterfaceVersion = FIRST_CALL_TRANSACTION + 16777214;
constexpr int32_t kPqMinVersion = 7;
constexpr int32_t kApplyNow = 1;
constexpr float kFixedOne = 1024.0f;
constexpr long kCoefficientMin = -2048, kCoefficientMax = 2047;
constexpr int32_t kPqIndexContrast = 9;
constexpr int32_t kContrastNeutral = 50;

constexpr char kModeProp[] = "persist.vendor.livedisplay.mode";

// Rec. 709 luma weights.
constexpr float kLr = 0.2126f, kLg = 0.7152f, kLb = 0.0722f;

// The panel as it is, a softer and a stronger profile. The names select the titles LineageParts
// shows (live_display_color_profile_<name>_title).
struct Profile {
    const char* name;
    float saturation;
};
constexpr Profile kProfiles[] = {
        {"standard", 1.0f},
        {"natural", 0.9f},
        {"dynamic", 1.25f},
};
constexpr int32_t kNumProfiles = sizeof(kProfiles) / sizeof(kProfiles[0]);
constexpr int32_t kDefaultProfile = 0;

// The contrast is a level offset from kContrastNeutral. The framework hides the whole picture
// adjustment if any range is 0..0, so the saturation threshold (no slider, unused) gets one too.
constexpr FloatRange kHueRange = {180.0f, -180.0f, 1.0f};
constexpr FloatRange kSaturationRange = {100.0f, -100.0f, 1.0f};
constexpr FloatRange kIntensityRange = {0.0f, -50.0f, 1.0f};
constexpr FloatRange kContrastRange = {50.0f, -50.0f, 1.0f};
constexpr FloatRange kSaturationThresholdRange = {1.0f, 0.0f, 1.0f};

constexpr int32_t kCalibrationMin = 0;
constexpr int32_t kCalibrationMax = 255;

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix m{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) m[i][j] += a[i][k] * b[k][j];
    return m;
}

// Scales the distance of each color from its luma: 0 = gray, 1 = unchanged.
Matrix saturation(float s) {
    const std::array<float, 3> luma = {kLr, kLg, kLb};
    Matrix m{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = (1 - s) * luma[j] + (i == j ? s : 0);
    return m;
}

// Rotates the hue around the luma axis, keeping the luma (the usual image processing matrix).
Matrix hue(float degrees) {
    const float c = std::cos(degrees * static_cast<float>(M_PI) / 180.0f);
    const float s = std::sin(degrees * static_cast<float>(M_PI) / 180.0f);
    return {{{kLr + c * (1 - kLr) - s * kLr, kLg - c * kLg - s * kLg, kLb - c * kLb + s * (1 - kLb)},
             {kLr - c * kLr + s * 0.143f, kLg + c * (1 - kLg) + s * 0.140f, kLb - c * kLb - s * 0.283f},
             {kLr - c * kLr - s * (1 - kLr), kLg - c * kLg + s * kLg, kLb + c * (1 - kLb) + s * kLb}}};
}

Matrix scale(float r, float g, float b) {
    return {{{r, 0, 0}, {0, g, 0}, {0, 0, b}}};
}

class ColorState {
  public:
    ColorState() {
        mProfile = android::base::GetIntProperty(kModeProp, kDefaultProfile, 0, kNumProfiles - 1);
        mDefaultProfile = mProfile;
    }

    // Waits for the PQ HAL in the background and applies the current state once it is there. Right
    // after a restart of the PQ HAL the service manager may still hand out the dead binder: retry.
    void connectAsync() {
        std::thread([this] {
            for (;;) {
                ndk::SpAIBinder binder(AServiceManager_waitForService(kPqInstance));
                {
                    std::lock_guard<std::mutex> lock(mLock);
                    switch (attachLocked(std::move(binder))) {
                        case Attach::kOk:
                            applyLocked();
                            applyContrastLocked();
                            return;
                        case Attach::kUnsupported:
                            return;
                        case Attach::kRetry:
                            break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
    }

    int32_t profile() {
        std::lock_guard<std::mutex> lock(mLock);
        return mProfile;
    }
    int32_t defaultProfile() {
        std::lock_guard<std::mutex> lock(mLock);
        return mDefaultProfile;
    }
    void setProfile(int32_t id, bool makeDefault) {
        std::lock_guard<std::mutex> lock(mLock);
        mProfile = id;
        if (makeDefault) {
            mDefaultProfile = id;
            android::base::SetProperty(kModeProp, std::to_string(id));
        }
        applyLocked();
    }

    HSIC hsic() {
        std::lock_guard<std::mutex> lock(mLock);
        return mHsic;
    }
    void setHsic(const HSIC& hsic) {
        std::lock_guard<std::mutex> lock(mLock);
        mHsic = hsic;
        applyLocked();
        applyContrastLocked();
    }

    std::array<int32_t, 3> calibration() {
        std::lock_guard<std::mutex> lock(mLock);
        return mCalibration;
    }
    void setCalibration(const std::array<int32_t, 3>& rgb) {
        std::lock_guard<std::mutex> lock(mLock);
        mCalibration = rgb;
        applyLocked();
    }

    bool reading() {
        std::lock_guard<std::mutex> lock(mLock);
        return mReading;
    }
    void setReading(bool enabled) {
        std::lock_guard<std::mutex> lock(mLock);
        mReading = enabled;
        applyLocked();
    }

  private:
    // The calibration applies last, so it also tints reading mode.
    Matrix composeLocked() const {
        Matrix m = saturation(kProfiles[mProfile].saturation);
        m = multiply(hue(mHsic.hue), m);
        m = multiply(saturation(1 + mHsic.saturation / 100), m);
        const float intensity = 1 + mHsic.intensity / 100;
        m = multiply(scale(intensity, intensity, intensity), m);
        if (mReading) m = multiply(saturation(0), m);
        m = multiply(scale(mCalibration[0] / float(kCalibrationMax),
                           mCalibration[1] / float(kCalibrationMax),
                           mCalibration[2] / float(kCalibrationMax)),
                     m);
        return m;
    }

    enum class Attach { kOk, kRetry, kUnsupported };

    // Sends the transaction and checks the status header of the reply; the reply data follows.
    static bool transact(AIBinder* binder, transaction_code_t code, AParcel* in,
                         ndk::ScopedAParcel* reply) {
        AParcel* out = nullptr;
        binder_status_t status = AIBinder_transact(binder, code, &in, &out, 0);
        reply->set(out);
        if (status != STATUS_OK) {
            LOG(ERROR) << "PQ transaction " << code << " failed: " << status;
            return false;
        }
        AStatus* replyStatus = nullptr;
        if (AParcel_readStatusHeader(reply->get(), &replyStatus) != STATUS_OK) return false;
        ndk::ScopedAStatus scopedStatus(replyStatus);
        if (!scopedStatus.isOk()) {
            LOG(ERROR) << "PQ transaction " << code << ": " << scopedStatus.getDescription();
            return false;
        }
        return true;
    }

    Attach attachLocked(ndk::SpAIBinder binder) {
        static AIBinder_Class* pqClass = AIBinder_Class_define(
                kPqDescriptor, [](void*) -> void* { return nullptr; }, [](void*) {},
                [](AIBinder*, transaction_code_t, const AParcel*, AParcel*) -> binder_status_t {
                    return STATUS_UNKNOWN_TRANSACTION;
                });
        if (binder.get() == nullptr || !AIBinder_associateClass(binder.get(), pqClass)) {
            LOG(ERROR) << "No " << kPqInstance;
            return Attach::kRetry;
        }
        // Transaction 51 is setColorMatrix3x3 from version 7 on; stable AIDL only appends.
        AParcel* in = nullptr;
        ndk::ScopedAParcel reply;
        int32_t version = 0;
        if (AIBinder_prepareTransaction(binder.get(), &in) != STATUS_OK ||
            !transact(binder.get(), kGetInterfaceVersion, in, &reply) ||
            AParcel_readInt32(reply.get(), &version) != STATUS_OK) {
            return Attach::kRetry;
        }
        if (version < kPqMinVersion) {
            LOG(ERROR) << kPqInstance << " is version " << version << ", need " << kPqMinVersion;
            return Attach::kUnsupported;
        }
        // A restarted PQ HAL has lost our matrix: give it again.
        if (mDeathRecipient.get() == nullptr) {
            mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(AIBinder_DeathRecipient_new(
                    [](void* cookie) {
                        auto* self = static_cast<ColorState*>(cookie);
                        LOG(WARNING) << "PQ HAL died, reconnecting";
                        {
                            std::lock_guard<std::mutex> lock(self->mLock);
                            self->mPq = ndk::SpAIBinder();
                            self->mAppliedContrast = -1;
                        }
                        self->connectAsync();
                    }));
        }
        if (AIBinder_linkToDeath(binder.get(), mDeathRecipient.get(), this) != STATUS_OK) {
            return Attach::kRetry;
        }
        mPq = std::move(binder);
        return Attach::kOk;
    }

    void applyLocked() {
        if (mPq.get() == nullptr) return;  // connectAsync applies it

        const Matrix m = composeLocked();
        AParcel* in = nullptr;
        if (AIBinder_prepareTransaction(mPq.get(), &in) != STATUS_OK) return;
        // int[3][3] = an array of three rows, each written with its length. Coefficients up to 1.5
        // were seen to work; the range of the 13 bit CCORR is not known, so stay within +-2.0 (only
        // extreme combinations, e.g. the dynamic profile with the saturation at its maximum, get there).
        AParcel_writeInt32(in, 3);
        for (const auto& row : m) {
            AParcel_writeInt32(in, 3);
            for (float v : row) {
                AParcel_writeInt32(in, static_cast<int32_t>(std::clamp(std::lround(v * kFixedOne),
                                                                       kCoefficientMin, kCoefficientMax)));
            }
        }
        AParcel_writeInt32(in, kApplyNow);

        ndk::ScopedAParcel reply;
        int32_t result = -1;
        if (!transact(mPq.get(), kSetColorMatrix3x3, in, &reply)) return;
        if (AParcel_readInt32(reply.get(), &result) != STATUS_OK || result != 0) {
            LOG(ERROR) << "setColorMatrix3x3: result " << result;
        }
    }

    // Only on a change: the PQ HAL writes its persist property on every call.
    void applyContrastLocked() {
        if (mPq.get() == nullptr) return;  // connectAsync applies it

        const int32_t level = kContrastNeutral + static_cast<int32_t>(std::lround(mHsic.contrast));
        if (level == mAppliedContrast) return;
        AParcel* in = nullptr;
        if (AIBinder_prepareTransaction(mPq.get(), &in) != STATUS_OK) return;
        AParcel_writeInt32(in, level);
        AParcel_writeInt32(in, 0);  // scenario: image (the panel maps all three to one table)
        AParcel_writeInt32(in, 0);  // tuning mode: normal
        AParcel_writeInt32(in, kPqIndexContrast);
        AParcel_writeInt32(in, 0);  // step: at once

        ndk::ScopedAParcel reply;
        int32_t result = -1;
        if (!transact(mPq.get(), kSetPQIndex, in, &reply)) return;
        if (AParcel_readInt32(reply.get(), &result) != STATUS_OK || result != 0) {
            LOG(ERROR) << "setPQIndex(contrast " << level << "): result " << result;
            return;
        }
        mAppliedContrast = level;
    }

    std::mutex mLock;
    ndk::SpAIBinder mPq;
    ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
    int32_t mProfile;
    int32_t mDefaultProfile;
    HSIC mHsic = {0, 0, 0, 0, 0};
    std::array<int32_t, 3> mCalibration = {kCalibrationMax, kCalibrationMax, kCalibrationMax};
    bool mReading = false;
    int32_t mAppliedContrast = -1;
};

DisplayMode profileMode(int32_t id) {
    return {id, kProfiles[id].name};
}

class DisplayModes : public BnDisplayModes {
  public:
    explicit DisplayModes(ColorState* state) : mState(state) {}

    ScopedAStatus getDisplayModes(std::vector<DisplayMode>* modes) override {
        modes->clear();
        for (int32_t id = 0; id < kNumProfiles; id++) modes->push_back(profileMode(id));
        return ScopedAStatus::ok();
    }
    ScopedAStatus getCurrentDisplayMode(DisplayMode* mode) override {
        *mode = profileMode(mState->profile());
        return ScopedAStatus::ok();
    }
    ScopedAStatus getDefaultDisplayMode(DisplayMode* mode) override {
        *mode = profileMode(mState->defaultProfile());
        return ScopedAStatus::ok();
    }
    ScopedAStatus setDisplayMode(int32_t id, bool makeDefault) override {
        if (id < 0 || id >= kNumProfiles) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        mState->setProfile(id, makeDefault);
        return ScopedAStatus::ok();
    }

  private:
    ColorState* mState;
};

class PictureAdjustment : public BnPictureAdjustment {
  public:
    explicit PictureAdjustment(ColorState* state) : mState(state) {}

    ScopedAStatus getHueRange(FloatRange* range) override {
        *range = kHueRange;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getSaturationRange(FloatRange* range) override {
        *range = kSaturationRange;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getIntensityRange(FloatRange* range) override {
        *range = kIntensityRange;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getContrastRange(FloatRange* range) override {
        *range = kContrastRange;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getSaturationThresholdRange(FloatRange* range) override {
        *range = kSaturationThresholdRange;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getPictureAdjustment(HSIC* hsic) override {
        *hsic = mState->hsic();
        return ScopedAStatus::ok();
    }
    ScopedAStatus getDefaultPictureAdjustment(HSIC* hsic) override {
        *hsic = {0, 0, 0, 0, 0};
        return ScopedAStatus::ok();
    }
    ScopedAStatus setPictureAdjustment(const HSIC& hsic) override {
        auto inRange = [](float v, const FloatRange& r) { return v >= r.min && v <= r.max; };
        if (!inRange(hsic.hue, kHueRange) || !inRange(hsic.saturation, kSaturationRange) ||
            !inRange(hsic.intensity, kIntensityRange) || !inRange(hsic.contrast, kContrastRange)) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        mState->setHsic(hsic);
        return ScopedAStatus::ok();
    }

  private:
    ColorState* mState;
};

class DisplayColorCalibration : public BnDisplayColorCalibration {
  public:
    explicit DisplayColorCalibration(ColorState* state) : mState(state) {}

    ScopedAStatus getMaxValue(int32_t* max) override {
        *max = kCalibrationMax;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getMinValue(int32_t* min) override {
        *min = kCalibrationMin;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getCalibration(std::vector<int32_t>* rgb) override {
        const auto cal = mState->calibration();
        *rgb = {cal[0], cal[1], cal[2]};
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCalibration(const std::vector<int32_t>& rgb) override {
        if (rgb.size() != 3) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        std::array<int32_t, 3> cal;
        for (int i = 0; i < 3; i++) {
            if (rgb[i] < kCalibrationMin || rgb[i] > kCalibrationMax)
                return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            cal[i] = rgb[i];
        }
        mState->setCalibration(cal);
        return ScopedAStatus::ok();
    }

  private:
    ColorState* mState;
};

class ReadingEnhancement : public BnReadingEnhancement {
  public:
    explicit ReadingEnhancement(ColorState* state) : mState(state) {}

    ScopedAStatus getEnabled(bool* enabled) override {
        *enabled = mState->reading();
        return ScopedAStatus::ok();
    }
    ScopedAStatus setEnabled(bool enabled) override {
        mState->setReading(enabled);
        return ScopedAStatus::ok();
    }

  private:
    ColorState* mState;
};

template <typename T>
void addService(const std::shared_ptr<T>& service) {
    const std::string instance = std::string(T::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(service->asBinder().get(), instance.c_str()), STATUS_OK)
            << instance;
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    static ColorState state;
    addService(ndk::SharedRefBase::make<DisplayModes>(&state));
    addService(ndk::SharedRefBase::make<PictureAdjustment>(&state));
    addService(ndk::SharedRefBase::make<DisplayColorCalibration>(&state));
    addService(ndk::SharedRefBase::make<ReadingEnhancement>(&state));
    state.connectAsync();

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
