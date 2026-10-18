/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.emerald.udfps;

import android.content.Context;
import android.database.ContentObserver;
import android.os.Handler;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;

/**
 * Mirrors Settings.Secure.DOUBLE_TAP_TO_WAKE ("Tap to wake" in Settings) into sys.emerald.dt2w;
 * init then runs emerald_dt2w, which switches the touch driver's gesture (touch/dt2w.cpp). The
 * framework's own path, PowerManagerService -> IPower setMode(DOUBLE_TAP_TO_WAKE), ends in the
 * stock MediaTek power HAL, which does not drive the touch panel.
 */
class DoubleTapToWake {
    private static final String TAG = "EmeraldUdfps";
    private static final String PROP = "sys.emerald.dt2w";

    private final Context mContext;
    private final Handler mHandler;

    DoubleTapToWake(Context context, Handler handler) {
        mContext = context;
        mHandler = handler;
    }

    void start() {
        // This app runs as user 0 (persistent), so this is user 0's setting; other users would need
        // INTERACT_ACROSS_USERS_FULL.
        mContext.getContentResolver().registerContentObserver(
                Settings.Secure.getUriFor(Settings.Secure.DOUBLE_TAP_TO_WAKE), false,
                new ContentObserver(mHandler) {
                    @Override
                    public void onChange(boolean selfChange) {
                        update();
                    }
                });
        update();
    }

    private void update() {
        // Same default as Settings' TapToWakePreferenceController.
        final boolean enabled = Settings.Secure.getInt(mContext.getContentResolver(),
                Settings.Secure.DOUBLE_TAP_TO_WAKE, 0) != 0;
        try {
            SystemProperties.set(PROP, enabled ? "1" : "0");
            Log.i(TAG, "double tap to wake " + (enabled ? "on" : "off"));
        } catch (RuntimeException e) {
            Log.e(TAG, "could not set " + PROP, e);
        }
    }
}
