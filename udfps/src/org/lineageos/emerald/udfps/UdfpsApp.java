/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.emerald.udfps;

import android.app.Application;
import android.hardware.biometrics.BiometricStateListener;
import android.hardware.fingerprint.FingerprintManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ServiceManager;
import android.util.Log;

/**
 * Gives the stock HyperOS 3 fingerprint HAL the two inputs the HyperOS framework used to give it.
 *
 * The stock HAL (vendor fingerprint-service.odm, Xiaomi FodEngineCore) watches the touch
 * panel's FOD flag itself and lights the sensor (local HBM) on a finger-down, but only when
 *  - the touch firmware reports finger-on-sensor: touch mode 10 (FOD) must be on, set through
 *    vendor.xiaomi.hw.touchfeature.ITouchFeature setModeValue(touchId 0, mode 10, value 1), and
 *  - the HAL knows a capture is wanted: IXiaomiFingerprint extCmd(4, state) with 1 = enrolling,
 *    3 = authenticating, 0 = idle. While idle it logs "skip enable lhbm as fingerprint is idle"
 *    and the capture never starts.
 * Found on 2026-10-14 by hand on clean-1.1 (emerald-buildnotes notes.md, "Fingerprint, manual test").
 * The stock vendor SELinux policy lets platform_app find and call both services.
 */
public class UdfpsApp extends Application {
    private static final String TAG = "EmeraldUdfps";

    private static final String TOUCH_SERVICE = "vendor.xiaomi.hw.touchfeature.ITouchFeature/default";
    private static final String TOUCH_DESCRIPTOR = "vendor.xiaomi.hw.touchfeature.ITouchFeature";
    // FIRST_CALL_TRANSACTION + 8, read from the stock vendor.xiaomi.hw.touchfeature-V1-ndk.so proxy
    private static final int TOUCH_SET_MODE_VALUE = 9;
    private static final int TOUCH_ID = 0;
    private static final int TOUCH_MODE_FOD = 10;

    private static final String FP_SERVICE =
            "vendor.xiaomi.hardware.fingerprintextension.IXiaomiFingerprint/default";
    private static final String FP_DESCRIPTOR =
            "vendor.xiaomi.hardware.fingerprintextension.IXiaomiFingerprint";
    // FIRST_CALL_TRANSACTION + 0, the interface's only method
    private static final int FP_EXT_CMD = 1;
    private static final int CMD_FINGERPRINT_STATE = 4;
    private static final int FP_STATE_IDLE = 0;
    private static final int FP_STATE_ENROLL = 1;
    private static final int FP_STATE_AUTH = 3;

    private Handler mHandler;

    @Override
    public void onCreate() {
        super.onCreate();
        HandlerThread thread = new HandlerThread(TAG);
        thread.start();
        mHandler = new Handler(thread.getLooper());
        mHandler.post(() -> {
            // First, as setup() below waits for the fingerprint and touch HALs.
            try {
                new UltrasoundReceiverMode(this, mHandler).start();
            } catch (RuntimeException e) {
                Log.e(TAG, "ultrasound receiver mode disabled", e);
            }
            // A persistent app that crashes on start is restarted at once, and the crash loop ends in
            // a RescueParty reboot (clean-1.2 bootloop, 2026-10-15). Without this bridge only the
            // fingerprint stops working, so log and stay idle instead.
            try {
                setup();
            } catch (RuntimeException e) {
                Log.e(TAG, "setup failed, fingerprint bridge disabled", e);
            }
            try {
                new DoubleTapToWake(this, mHandler).start();
            } catch (RuntimeException e) {
                Log.e(TAG, "double tap to wake disabled", e);
            }
            try {
                new ScreenOffPulse(this, mHandler).start();
            } catch (RuntimeException e) {
                Log.e(TAG, "screen-off pulse disabled", e);
            }
        });
    }

    private void setup() {
        // Both HALs start before the framework; wait for them anyway, it costs nothing.
        ServiceManager.waitForService(TOUCH_SERVICE);
        ServiceManager.waitForService(FP_SERVICE);
        setFodMode();

        FingerprintManager fpm = getSystemService(FingerprintManager.class);
        if (fpm == null) {
            Log.e(TAG, "no FingerprintManager");
            return;
        }
        fpm.registerBiometricStateListener(new BiometricStateListener() {
            @Override
            public void onStateChanged(int newState) {
                mHandler.post(() -> onFingerprintState(newState));
            }
        });
        Log.i(TAG, "listening for fingerprint state changes");
    }

    private void onFingerprintState(int state) {
        final int fpState;
        switch (state) {
            case BiometricStateListener.STATE_ENROLLING:
                fpState = FP_STATE_ENROLL;
                break;
            case BiometricStateListener.STATE_KEYGUARD_AUTH:
            case BiometricStateListener.STATE_BP_AUTH:
            case BiometricStateListener.STATE_AUTH_OTHER:
                fpState = FP_STATE_AUTH;
                break;
            default:
                fpState = FP_STATE_IDLE;
                break;
        }
        Log.d(TAG, "framework state " + state + " -> HAL state " + fpState);
        if (fpState != FP_STATE_IDLE) {
            // Cheap and keeps working if the touch HAL restarted since boot.
            setFodMode();
        }
        transact(FP_SERVICE, FP_DESCRIPTOR, FP_EXT_CMD, CMD_FINGERPRINT_STATE, fpState);
    }

    private void setFodMode() {
        transact(TOUCH_SERVICE, TOUCH_DESCRIPTOR, TOUCH_SET_MODE_VALUE, TOUCH_ID, TOUCH_MODE_FOD, 1);
    }

    private static void transact(String service, String descriptor, int code, int... args) {
        IBinder binder = ServiceManager.getService(service);
        if (binder == null) {
            Log.e(TAG, service + " not found");
            return;
        }
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(descriptor);
            for (int arg : args) {
                data.writeInt(arg);
            }
            binder.transact(code, data, reply, 0);
            reply.readException();
        } catch (Exception e) {
            Log.e(TAG, "call " + code + " on " + service + " failed", e);
        } finally {
            data.recycle();
            reply.recycle();
        }
    }
}
