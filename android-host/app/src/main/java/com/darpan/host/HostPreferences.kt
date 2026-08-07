package com.darpan.host

import android.content.Context
import android.content.SharedPreferences
import java.util.UUID

class HostPreferences(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var serverUrl: String
        get() = prefs.getString(KEY_SERVER_URL, DEFAULT_SERVER_URL).orEmpty()
        set(value) = prefs.edit().putString(KEY_SERVER_URL, value.trim()).apply()

    var displayName: String
        get() = prefs.getString(KEY_DISPLAY_NAME, DEFAULT_DISPLAY_NAME).orEmpty()
        set(value) = prefs.edit().putString(KEY_DISPLAY_NAME, value.trim()).apply()

    var lastRoomId: String
        get() = prefs.getString(KEY_LAST_ROOM, "").orEmpty()
        set(value) = prefs.edit().putString(KEY_LAST_ROOM, value.trim()).apply()

    var highQualityOnWifi: Boolean
        get() = prefs.getBoolean(KEY_HQ_WIFI, false)
        set(value) = prefs.edit().putBoolean(KEY_HQ_WIFI, value).apply()

    var lastRole: String
        get() = prefs.getString(KEY_LAST_ROLE, "controller").orEmpty()
        set(value) = prefs.edit().putString(KEY_LAST_ROLE, value).apply()

    fun peerIdForRoom(roomId: String): String {
        val key = "$KEY_PEER_PREFIX$roomId"
        val existing = prefs.getString(key, null)
        if (!existing.isNullOrBlank()) return existing
        val created = UUID.randomUUID().toString().replace("-", "")
        prefs.edit().putString(key, created).apply()
        return created
    }

    companion object {
        private const val PREFS_NAME = "darpan_host"
        private const val KEY_SERVER_URL = "server_url"
        private const val KEY_DISPLAY_NAME = "display_name"
        private const val KEY_LAST_ROOM = "last_room"
        private const val KEY_HQ_WIFI = "hq_wifi"
        private const val KEY_LAST_ROLE = "last_role"
        private const val KEY_PEER_PREFIX = "peer_id_"
        const val DEFAULT_SERVER_URL = SignalingClient.DEFAULT_SERVER_URL
        const val DEFAULT_DISPLAY_NAME = "My Phone"
    }
}
