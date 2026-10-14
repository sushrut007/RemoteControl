package com.darpan.remote.settings

import android.content.Context
import com.darpan.remote.Secrets
import org.json.JSONArray
import org.json.JSONObject

data class RecentRoomEntry(
    val roomId: String,
    val pinRequired: Boolean,
    val lastUsedEpochMs: Long,
)

class SettingsRepository(context: Context) {
    private val prefs = context.getSharedPreferences("darpan", Context.MODE_PRIVATE)

    var deviceName: String
        get() = prefs.getString(KEY_DEVICE, "Darpan Android") ?: "Darpan Android"
        set(value) = prefs.edit().putString(KEY_DEVICE, value).apply()

    var signalingUrl: String
        get() = prefs.getString(KEY_SIGNAL, Secrets.DEFAULT_SIGNALING_URL) ?: Secrets.DEFAULT_SIGNALING_URL
        set(value) = prefs.edit().putString(KEY_SIGNAL, value).apply()

    var stunServer: String
        get() = prefs.getString(KEY_STUN, Secrets.DEFAULT_STUN_SERVER) ?: Secrets.DEFAULT_STUN_SERVER
        set(value) = prefs.edit().putString(KEY_STUN, value).apply()

    var turnServer: String
        get() = prefs.getString(KEY_TURN, Secrets.DEFAULT_TURN_SERVER) ?: Secrets.DEFAULT_TURN_SERVER
        set(value) = prefs.edit().putString(KEY_TURN, value).apply()

    var turnUsername: String
        get() = prefs.getString(KEY_TURN_USER, Secrets.DEFAULT_TURN_USERNAME) ?: Secrets.DEFAULT_TURN_USERNAME
        set(value) = prefs.edit().putString(KEY_TURN_USER, value).apply()

    var turnPassword: String
        get() = prefs.getString(KEY_TURN_PASS, Secrets.DEFAULT_TURN_PASSWORD) ?: Secrets.DEFAULT_TURN_PASSWORD
        set(value) = prefs.edit().putString(KEY_TURN_PASS, value).apply()

    var signalingPinSha256: String
        get() = prefs.getString(KEY_PIN_SHA, "") ?: ""
        set(value) = prefs.edit().putString(KEY_PIN_SHA, value.trim()).apply()

    var signalingPinEnabled: Boolean
        get() = prefs.getBoolean(KEY_PIN_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_PIN_ENABLED, value).apply()

    fun normalizeSignalingUrl(raw: String): String {
        var url = raw.trim()
        if (url.isEmpty()) url = Secrets.DEFAULT_SIGNALING_URL
        if (!url.contains("://")) url = "ws://$url"
        if (!url.endsWith("/ws")) {
            url = if (url.endsWith("/")) "${url}ws" else "$url/ws"
        }
        return url
    }

    fun getRecentRooms(): List<RecentRoomEntry> {
        val json = prefs.getString(KEY_RECENTS, null) ?: return emptyList()
        return try {
            val array = JSONArray(json)
            val list = mutableListOf<RecentRoomEntry>()
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                list.add(
                    RecentRoomEntry(
                        roomId = obj.getString("room_id"),
                        pinRequired = obj.optBoolean("pin_required", false),
                        lastUsedEpochMs = obj.optLong("last_used_ms", 0L),
                    )
                )
            }
            list
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun addRecentRoom(roomId: String, pinRequired: Boolean) {
        val cleanId = roomId.trim().uppercase()
        if (cleanId.isEmpty()) return
        val current = getRecentRooms().filter { !it.roomId.equals(cleanId, ignoreCase = true) }.toMutableList()
        current.add(0, RecentRoomEntry(cleanId, pinRequired, System.currentTimeMillis()))
        val trimmed = if (current.size > 8) current.take(8) else current
        val array = JSONArray()
        for (item in trimmed) {
            val obj = JSONObject()
                .put("room_id", item.roomId)
                .put("pin_required", item.pinRequired)
                .put("last_used_ms", item.lastUsedEpochMs)
            array.put(obj)
        }
        prefs.edit().putString(KEY_RECENTS, array.toString()).apply()
    }

    fun removeRecentRoom(roomId: String) {
        val current = getRecentRooms().filter { !it.roomId.equals(roomId, ignoreCase = true) }
        val array = JSONArray()
        for (item in current) {
            val obj = JSONObject()
                .put("room_id", item.roomId)
                .put("pin_required", item.pinRequired)
                .put("last_used_ms", item.lastUsedEpochMs)
            array.put(obj)
        }
        prefs.edit().putString(KEY_RECENTS, array.toString()).apply()
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
        private const val KEY_RECENTS = "recent_rooms_json"
    }
}
