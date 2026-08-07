package com.darpan.host

import org.json.JSONObject

data class JoinRoomRequest(
    val roomId: String,
    val peerId: String,
    val displayName: String,
    val appType: String = "host",
    val metadata: JSONObject = JSONObject(),
) {
    fun toJson(): JSONObject = JSONObject()
        .put("roomId", roomId)
        .put("peerId", peerId)
        .put("displayName", displayName)
        .put("appType", appType)
        .put("metadata", metadata)
}

data class PeerInfo(
    val peerId: String,
    val displayName: String,
    val appType: String,
)

data class MouseEventPayload(
    val senderId: String,
    val x: Float,
    val y: Float,
    val deltaX: Float,
    val deltaY: Float,
    val eventType: String,
    val button: String,
)

data class KeyboardEventPayload(
    val senderId: String,
    val key: String,
    val eventType: String,
    val modifiers: List<String>,
)

data class ControlCommandPayload(
    val senderId: String,
    val command: String,
)

data class VideoPacketPayload(
    val dataBase64: String,
    val isKeyframe: Boolean,
)

data class StreamSettings(
    val width: Int = 1280,
    val height: Int = 720,
    val fps: Int = 24,
    val bitrateKbps: Int = 2500,
    val highQualityOnWifi: Boolean = false,
) {
    fun effectiveWidth(onWifi: Boolean): Int =
        if (onWifi && highQualityOnWifi) 1920 else width

    fun effectiveHeight(onWifi: Boolean): Int =
        if (onWifi && highQualityOnWifi) 1080 else height

    fun effectiveFps(onWifi: Boolean): Int =
        if (onWifi && highQualityOnWifi) 30 else fps
}

sealed class SignalingAck {
    data class Ok(val body: JSONObject) : SignalingAck()
    data class Err(val message: String) : SignalingAck()
}

fun parseAck(args: Array<Any>?): SignalingAck {
    if (args == null || args.isEmpty()) {
        return SignalingAck.Err("empty ack")
    }
    val raw = args[0]
    if (raw !is JSONObject) {
        return SignalingAck.Err("invalid ack type")
    }
    if (raw.optBoolean("ok", true) && !raw.has("error")) {
        return SignalingAck.Ok(raw)
    }
    val err = when {
        raw.has("error") && raw.get("error") is String -> raw.getString("error")
        raw.has("error") && raw.get("error") is JSONObject ->
            raw.getJSONObject("error").optString("message", "unknown error")
        else -> "request failed"
    }
    return SignalingAck.Err(err)
}

fun firstPayload(args: Array<Any>?): JSONObject {
    if (args == null || args.isEmpty()) return JSONObject()
    return when (val raw = args[0]) {
        is JSONObject -> raw
        else -> JSONObject()
    }
}

fun peerFromJson(obj: JSONObject): PeerInfo = PeerInfo(
    peerId = obj.optString("peerId"),
    displayName = obj.optString("displayName"),
    appType = obj.optString("appType", "viewer"),
)
