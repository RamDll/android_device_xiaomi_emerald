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

        // Earlier builds seeded min_refresh_rate = 90 at first boot. The 90 Hz mode is the one that
        // visibly steps the brightness on these panels, so go back to a 60 Hz minimum once. This
        // also resets a 90 the user picked by hand; the changelog says so. r13 wrote 0 instead,
        // which works but Settings' minimum list has no 0 entry and shows its first one (120Hz),
        // so 0 is moved to 60 as well.
        private const val MIN_REFRESH_RATE = "min_refresh_rate"
        private val OLD_MIN_REFRESH_RATES = setOf(90f, 0f)
        private const val DEFAULT_MIN_REFRESH_RATE = 60f
        private const val PREFS = "boot_migrations"
        private const val KEY_MIN_REFRESH_60 = "min_refresh_60"

        // Blur makes SurfaceFlinger compose the whole screen on the GPU. It is supported now, but
        // starts disabled; Developer options -> "Allow window-level blurs" turns it on.
        private const val KEY_BLURS_OFF = "blurs_off"
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
        // Display: one-time migration away from the old 90 Hz minimum
        val prefs = context.createDeviceProtectedStorageContext()
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(KEY_MIN_REFRESH_60, false)) {
            val min = Settings.System.getFloat(
                context.contentResolver, MIN_REFRESH_RATE, DEFAULT_MIN_REFRESH_RATE
            )
            if (min in OLD_MIN_REFRESH_RATES) {
                Settings.System.putFloat(
                    context.contentResolver, MIN_REFRESH_RATE, DEFAULT_MIN_REFRESH_RATE
                )
            }
            prefs.edit().putBoolean(KEY_MIN_REFRESH_60, true).apply()
        }

        // Display: blur off by default, once, unless the user already chose
        if (!prefs.getBoolean(KEY_BLURS_OFF, false)) {
            if (Settings.Global.getString(
                    context.contentResolver, Settings.Global.DISABLE_WINDOW_BLURS) == null) {
                Settings.Global.putInt(
                    context.contentResolver, Settings.Global.DISABLE_WINDOW_BLURS, 1
                )
            }
            prefs.edit().putBoolean(KEY_BLURS_OFF, true).apply()
        }

        // Battery
        context.startServiceAsUser(Intent(context, ChargingLimitService::class.java), UserHandle.CURRENT)
    }
}
