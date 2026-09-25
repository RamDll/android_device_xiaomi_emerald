/*
 * SPDX-FileCopyrightText: 2018 The LineageOS Project
 * SPDX-FileCopyrightText: 2025 Paranoid Android
 * SPDX-License-Identifier: Apache-2.0
 */

package com.xiaomi.settings

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.UserHandle
import android.provider.Settings
import android.util.Log
import com.xiaomi.settings.battery.ChargingLimitService

/** Everything begins at boot. */
class BootCompletedReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "BootReceiver"
        private val DEBUG = Log.isLoggable(TAG, Log.DEBUG)

        // The panel flickers when it leaves 60 Hz, so keep it at 90 Hz or above by default.
        private const val MIN_REFRESH_RATE = "min_refresh_rate"
        private const val DEFAULT_MIN_REFRESH_RATE = 90f
    }

    override fun onReceive(context: Context, intent: Intent) {
        if (DEBUG) Log.d(TAG, "Received boot completed intent: ${intent.action}")
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED -> onBootCompleted(context)
            Intent.ACTION_LOCKED_BOOT_COMPLETED -> onLockedBootCompleted(context)
        }
    }

    private fun onBootCompleted(context: Context) {
    }

    private fun onLockedBootCompleted(context: Context) {
        // Display: only seed the default, never override a value the user picked
        if (Settings.System.getString(context.contentResolver, MIN_REFRESH_RATE) == null) {
            Settings.System.putFloat(context.contentResolver, MIN_REFRESH_RATE, DEFAULT_MIN_REFRESH_RATE)
        }

        // Battery
        context.startServiceAsUser(Intent(context, ChargingLimitService::class.java), UserHandle.CURRENT)
    }
}
