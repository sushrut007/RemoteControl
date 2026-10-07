package com.darpan.remote.settings

import android.content.Context

class SettingsRepository(context: Context) {
    private val prefs = context.getSharedPreferences("darpan", Context.MODE_PRIVATE)

    var deviceName: String
        get() = prefs.getString(KEY_DEVICE, "Darpan Android") ?: "Darpan Android"
        set(value) = prefs.edit().putString(KEY_DEVICE, value).apply()

    var signalingUrl: String
        get() = prefs.getString(KEY_SIGNAL, DEFAULT_SIGNAL) ?: DEFAULT_SIGNAL
        set(value) = prefs.edit().putString(KEY_SIGNAL, value).apply()

    var stunServer: String
        get() = prefs.getString(KEY_STUN, DEFAULT_STUN) ?: DEFAULT_STUN
        set(value) = prefs.edit().putString(KEY_STUN, value).apply()

    var turnServer: String
        get() = prefs.getString(KEY_TURN, "") ?: ""
        set(value) = prefs.edit().putString(KEY_TURN, value).apply()

    var turnUsername: String
        get() = prefs.getString(KEY_TURN_USER, "") ?: ""
        set(value) = prefs.edit().putString(KEY_TURN_USER, value).apply()

    var turnPassword: String
        get() = prefs.getString(KEY_TURN_PASS, "") ?: ""
        set(value) = prefs.edit().putString(KEY_TURN_PASS, value).apply()

    var signalingPinSha256: String
        get() = prefs.getString(KEY_PIN_SHA, "") ?: ""
        set(value) = prefs.edit().putString(KEY_PIN_SHA, value.trim()).apply()

    var signalingPinEnabled: Boolean
        get() = prefs.getBoolean(KEY_PIN_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_PIN_ENABLED, value).apply()

    fun normalizeSignalingUrl(raw: String): String {
        var url = raw.trim()
        if (url.isEmpty()) url = DEFAULT_SIGNAL
        if (!url.contains("://")) url = "ws://$url"
        if (!url.endsWith("/ws")) {
            url = if (url.endsWith("/")) "${url}ws" else "$url/ws"
        }
        return url
    }

    companion object {
        private const val KEY_DEVICE = "device_name"
        private const val KEY_SIGNAL = "signaling_url"
        private const val KEY_STUN = "stun_server"
        private const val KEY_TURN = "turn_server"
        private const val KEY_TURN_USER = "turn_username"
        private const val KEY_TURN_PASS = "turn_password"
        private const val KEY_PIN_SHA = "signaling_pin_sha256"
        private const val KEY_PIN_ENABLED = "signaling_pin_enabled"
        const val DEFAULT_STUN = "stun:stun.l.google.com:19302"
        /** Emulator → host machine loopback */
        const val DEFAULT_SIGNAL = "ws://10.0.2.2:8765/ws"
    }
}
