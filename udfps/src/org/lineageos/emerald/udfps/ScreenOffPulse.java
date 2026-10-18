/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.emerald.udfps;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.display.AmbientDisplayConfiguration;
import android.os.Handler;
import android.os.PowerManager;
import android.os.UserHandle;
import android.util.Log;

/**
 * "Lift to check phone": shows the ambient (doze) screen, as a notification does, when the phone
 * is picked up with the screen off. From there the fingerprint unlocks without the full screen.
 *
 * The stock sensors HAL has a pickup sensor, "xiaomi pick up sensor" (1.0 on pickup), but as a
 * special-trigger sensor; SystemUI's DozeSensors takes one-shot sensors only (requestTriggerSensor).
 * So this listens to it while the screen is off and asks SystemUI for a pulse (DozeTriggers,
 * "com.android.systemui.doze.pulse"; SystemUI checks the proximity sensor first). On the ambient
 * screen the touch panel runs and the stock fingerprint HAL lights the sensor (local HBM) and
 * unlocks. Tested by hand on clean-2.3, 2026-10-18.
 */
class ScreenOffPulse implements SensorEventListener {
    private static final String TAG = "EmeraldUdfps";

    private static final String PICKUP_SENSOR = "xiaomi pick up sensor";
    private static final String PULSE_ACTION = "com.android.systemui.doze.pulse";
    private static final String SYSTEMUI = "com.android.systemui";

    private final Context mContext;
    private final Handler mHandler;
    private final SensorManager mSensorManager;
    private final AmbientDisplayConfiguration mAmbientConfig;
    private final Sensor mPickupSensor;
    private boolean mListening;

    ScreenOffPulse(Context context, Handler handler) {
        mContext = context;
        mHandler = handler;
        mSensorManager = context.getSystemService(SensorManager.class);
        mAmbientConfig = new AmbientDisplayConfiguration(context);
        mPickupSensor = findSensor(PICKUP_SENSOR);
    }

    /** Asks SystemUI for an ambient screen pulse; it drops the request in a pocket or while pulsing. */
    static void requestPulse(Context context, UserHandle user) {
        context.sendBroadcastAsUser(new Intent(PULSE_ACTION).setPackage(SYSTEMUI), user);
    }

    void start() {
        if (mPickupSensor == null) {
            Log.e(TAG, "no " + PICKUP_SENSOR);
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        mContext.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                setListening(Intent.ACTION_SCREEN_OFF.equals(intent.getAction()));
            }
        }, filter, null, mHandler, Context.RECEIVER_NOT_EXPORTED);
        setListening(!mContext.getSystemService(PowerManager.class).isInteractive());
    }

    private Sensor findSensor(String type) {
        for (Sensor sensor : mSensorManager.getSensorList(Sensor.TYPE_ALL)) {
            if (type.equals(sensor.getStringType()) && sensor.isWakeUpSensor()) {
                return sensor;
            }
        }
        return null;
    }

    private void setListening(boolean listen) {
        if (listen == mListening) {
            return;
        }
        mListening = listen;
        // The setting can't change while the screen is off, so check it here: a wakeup sensor
        // left registered would wake the CPU on every pickup for nothing.
        if (listen && pickupEnabled()) {
            mSensorManager.registerListener(
                    this, mPickupSensor, SensorManager.SENSOR_DELAY_NORMAL, mHandler);
        } else if (!listen) {
            mSensorManager.unregisterListener(this);
        }
    }

    private boolean pickupEnabled() {
        // Our own user (0, persistent app): reading another user's settings needs
        // INTERACT_ACROSS_USERS_FULL.
        return mAmbientConfig.pickupGestureEnabled(UserHandle.myUserId());
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.values.length > 0 && event.values[0] != 0) {
            requestPulse(mContext, UserHandle.of(UserHandle.myUserId()));
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {}
}
