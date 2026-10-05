/*
 * SPDX-FileCopyrightText: 2018 The LineageOS Project
 * SPDX-FileCopyrightText: 2025 Paranoid Android
 * SPDX-License-Identifier: Apache-2.0
 */

package com.xiaomi.settings

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.os.SystemProperties
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

        // LK hands the panel over with its own DSI timing (8.54 ms instead of 8.41 ms per frame at
        // 120 Hz, 60 Hz video judders) until the first real screen off/on. Do that once at boot.
        // A DRM modeset before the composer starts fixed the timing but left the boot screen black.
        private const val NO_PANEL_RESET_PROP = "persist.log.tag.emerald_no_dispreset"
        private const val PANEL_OFF_MS = 1000L
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
        // Display: one screen off/on so the kernel reprograms the panel LK left on
        resetPanelTiming(context)

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

        // Battery
        context.startServiceAsUser(Intent(context, ChargingLimitService::class.java), UserHandle.CURRENT)
    }

    private fun resetPanelTiming(context: Context) {
        if (SystemProperties.get(NO_PANEL_RESET_PROP) == "1") return
        val pm = context.getSystemService(PowerManager::class.java) ?: return
        if (!pm.isInteractive) return
        val pending = goAsync()
        pm.goToSleep(SystemClock.uptimeMillis())
        Handler(Looper.getMainLooper()).postDelayed({
            pm.wakeUp(
                SystemClock.uptimeMillis(), PowerManager.WAKE_REASON_APPLICATION,
                "com.xiaomi.settings:panel-timing"
            )
            pending.finish()
        }, PANEL_OFF_MS)
    }
}
