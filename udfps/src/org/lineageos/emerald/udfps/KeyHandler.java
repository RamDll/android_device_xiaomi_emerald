/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.emerald.udfps;

import android.app.ActivityManager;
import android.content.Context;
import android.hardware.display.AmbientDisplayConfiguration;
import android.hardware.fingerprint.FingerprintManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.PowerManager;
import android.os.SystemClock;
import android.os.UserHandle;
import android.provider.Settings;
import android.util.Log;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

/**
 * "Screen-off fingerprint unlock": a LineageOS device key handler, loaded into system_server by
 * PhoneWindowManager (lineage-sdk config_deviceKeyHandlerClasses, build-time overlay in
 * overlay-lineage/).
 *
 * With the screen off the stock FocalTech driver reports a finger on the sensor as scan code 338
 * (BTN_INFO, not mapped in Generic.kl) and the stock fingerprint HAL sees the finger too, but it
 * can't light the sensor with the panel off and gives up (GF_ERROR_UI_READY_TIMEOUT). So this
 * shows the ambient screen, as ScreenOffPulse does for the pickup sensor; there the HAL lights the
 * sensor and unlocks while the finger is still down. The stock "xiaomi.sensor.fod" sensor is no use
 * here: it fires on pickup, not on a finger. Tested by hand on clean-2.3, 2026-10-18.
 *
 * The ambient screen needs doze, and Android dozes only with an ambient display option on
 * (DreamComponentsResolver.getDozeComponent -> AmbientDisplayConfiguration.enabled); the switch
 * itself doesn't count while config_dozeUdfpsLongPressSensorType is empty. With all of them off
 * nothing takes the pulse (tester, clean-2.5, 2026-10-19), so this wakes the screen instead and the
 * HAL lights the sensor on the lock screen. A keylayout test (338 -> WAKEUP) did the same on
 * clean-2.3: full wake, then unlock.
 */
public class KeyHandler implements DeviceKeyHandler {
    private static final String TAG = "EmeraldKeyHandler";
    private static final int SCANCODE_FINGER_DOWN = 338;

    private final Context mContext;
    private final PowerManager mPowerManager;
    private final Handler mHandler;

    public KeyHandler(Context context) {
        mContext = context;
        mPowerManager = context.getSystemService(PowerManager.class);
        HandlerThread thread = new HandlerThread("EmeraldKeyHandler");
        thread.start();
        mHandler = new Handler(thread.getLooper());
    }

    @Override
    public KeyEvent handleKeyEvent(KeyEvent event) {
        if (event.getScanCode() != SCANCODE_FINGER_DOWN) {
            return event;
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN && !mPowerManager.isInteractive()) {
            // Off the input dispatch thread: settings and fingerprint manager calls.
            // This thread is in system_server: an uncaught exception would take it down.
            mHandler.post(() -> {
                try {
                    if (screenOffUnlockEnabled()) {
                        showSensor();
                    }
                } catch (RuntimeException e) {
                    Log.e(TAG, "screen-off unlock failed", e);
                }
            });
        }
        // An unmapped key (KEYCODE_UNKNOWN) that nothing else wants.
        return null;
    }

    private void showSensor() {
        // Read on every finger-down, not cached: the ambient display settings can change at any time.
        if (new AmbientDisplayConfiguration(mContext).enabled(ActivityManager.getCurrentUser())) {
            ScreenOffPulse.requestPulse(mContext, UserHandle.SYSTEM);
        } else {
            mPowerManager.wakeUp(SystemClock.uptimeMillis(), PowerManager.WAKE_REASON_BIOMETRIC,
                    "org.lineageos.emerald.udfps:FINGER_DOWN");
        }
    }

    private boolean screenOffUnlockEnabled() {
        // Looked up here, not in the constructor: PhoneWindowManager creates the handler while
        // WindowManagerService starts, before FingerprintService is published.
        final FingerprintManager fingerprintManager =
                mContext.getSystemService(FingerprintManager.class);
        final int user = ActivityManager.getCurrentUser();
        final boolean def = mContext.getResources().getBoolean(
                com.android.internal.R.bool.config_screen_off_udfps_default_on);
        return Settings.Secure.getIntForUser(mContext.getContentResolver(),
                        Settings.Secure.SCREEN_OFF_UNLOCK_UDFPS_ENABLED, def ? 1 : 0, user) != 0
                && fingerprintManager != null
                && fingerprintManager.hasEnrolledTemplates(user);
    }
}
