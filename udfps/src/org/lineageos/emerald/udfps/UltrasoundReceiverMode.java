/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.emerald.udfps;

import android.content.Context;
import android.media.AudioManager;
import android.media.AudioSystem;
import android.os.Handler;
import android.util.Log;

/**
 * Keeps the ultrasound of the proximity sensor (Elliptic, out of call) on the earpiece amplifier.
 *
 * The stock audio HAL plays that ultrasound through the 2in1 top speaker, as a receiver or, once
 * anything has played on the loudspeakers, as a speaker, and it stays a speaker (the mode follows
 * routing changes only). In speaker mode the top speaker hisses audibly in a quiet room while the
 * sensor runs: on the lock screen, in Telegram chats. setParameters "ultra-receiver-force=1" makes
 * every ultrasound start take the receiver (AudioALSAHardware::setParameters, then
 * AudioALSAUltrasoundOutOfCallController); while the loudspeakers play, the HAL still takes the
 * speaker. Stock HyperOS sends the key only from its factory test (HQCit EllipticSensorActivity).
 * Tested by hand on clean-2.16, 2026-10-31: the hiss is barely audible; music, calls and the
 * screen-off at the ear as before. The flag lives in the HAL, so it is sent again whenever
 * audioserver is back up.
 */
class UltrasoundReceiverMode {
    private static final String TAG = "EmeraldUdfps";
    private static final String PARAMETER = "ultra-receiver-force=1";

    private final AudioManager mAudioManager;
    private final Handler mHandler;

    UltrasoundReceiverMode(Context context, Handler handler) {
        mAudioManager = context.getSystemService(AudioManager.class);
        mHandler = handler;
    }

    void start() {
        send();
        mAudioManager.setAudioServerStateCallback(mHandler::post,
                new AudioManager.AudioServerStateCallback() {
                    @Override
                    public void onAudioServerUp() {
                        send();
                    }
                });
    }

    private void send() {
        // AudioSystem rather than AudioManager.setParameters(), which drops the status.
        final int status = AudioSystem.setParameters(PARAMETER);
        if (status == AudioSystem.SUCCESS) {
            Log.i(TAG, "ultrasound proximity on the receiver (" + PARAMETER + ")");
        } else {
            Log.e(TAG, PARAMETER + " failed: " + status);
        }
    }
}
