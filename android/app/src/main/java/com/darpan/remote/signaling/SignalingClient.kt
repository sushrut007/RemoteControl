package com.darpan.remote.signaling

import android.util.Log
import com.darpan.remote.settings.SettingsRepository
import okhttp3.CertificatePinner
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject

class SignalingClient(
    private val listener: Listener,
) {
    interface Listener {
        fun onConnected()
        fun onDisconnected()
        fun onMessage(json: String)
        fun onError(message: String)
    }

    private var client: OkHttpClient = OkHttpClient()
    private var socket: WebSocket? = null
    private var pendingUrl: String? = null

    fun applySettings(settings: SettingsRepository) {
        val builder = OkHttpClient.Builder()
        if (settings.signalingPinEnabled) {
            val pin = settings.signalingPinSha256.trim()
            val signalUrl = settings.normalizeSignalingUrl(settings.signalingUrl)
            val host = signalUrl.toHttpUrlOrNull()?.host
            if (host != null && pin.isNotEmpty()) {
                val pinPattern = if (pin.startsWith("sha256/")) pin else "sha256/$pin"
                builder.certificatePinner(
                    CertificatePinner.Builder().add(host, pinPattern).build(),
                )
            }
        }
        client = builder.build()
    }

    fun connect(webSocketUrl: String) {
        disconnect()
        pendingUrl = webSocketUrl
        val request = Request.Builder().url(webSocketUrl).build()
        socket = client.newWebSocket(
            request,
            object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    listener.onConnected()
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    listener.onMessage(text)
                }

                override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                    listener.onDisconnected()
                }

                override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                    listener.onError(t.message ?: "WebSocket error")
                    listener.onDisconnected()
                }
            },
        )
    }

    fun disconnect() {
        socket?.close(1000, "bye")
        socket = null
    }

    fun reconnect() {
        pendingUrl?.let { connect(it) }
    }

    private fun send(obj: JSONObject) {
        socket?.send(obj.toString()) ?: listener.onError("Not connected")
    }

    fun sendHello(deviceName: String) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "hello")
                .put("device_name", deviceName)
                .put("client_platform", "android"),
        )
    }

    fun sendCreateRoom(pin: String?) {
        val o = JSONObject().put("v", 1).put("type", "create_room")
        if (!pin.isNullOrEmpty()) o.put("pin", pin)
        send(o)
    }

    fun sendJoinRoom(roomId: String, pin: String?, roleHint: String = "controller") {
        val o =
            JSONObject()
                .put("v", 1)
                .put("type", "join_room")
                .put("room_id", roomId.trim().uppercase())
                .put("role_hint", roleHint)
        if (!pin.isNullOrEmpty()) o.put("pin", pin)
        send(o)
    }

    fun sendLeaveRoom() {
        send(JSONObject().put("v", 1).put("type", "leave_room"))
    }

    fun sendSignalOffer(targetDeviceId: String, sdp: String) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "signal")
                .put("signal_type", "offer")
                .put("target_device_id", targetDeviceId)
                .put("sdp", sdp),
        )
    }

    fun sendSignalAnswer(targetDeviceId: String, sdp: String) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "signal")
                .put("signal_type", "answer")
                .put("target_device_id", targetDeviceId)
                .put("sdp", sdp),
        )
    }

    fun sendSignalIce(targetDeviceId: String, candidate: JSONObject) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "signal")
                .put("signal_type", "ice")
                .put("target_device_id", targetDeviceId)
                .put("candidate", candidate),
        )
    }

    fun sendRequestControl() {
        send(JSONObject().put("v", 1).put("type", "request_control"))
    }

    fun sendGrantControl(targetDeviceId: String) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "grant_control")
                .put("target_device_id", targetDeviceId),
        )
    }

    fun sendRevokeControl(targetDeviceId: String) {
        send(
            JSONObject()
                .put("v", 1)
                .put("type", "revoke_control")
                .put("target_device_id", targetDeviceId),
        )
    }

    companion object {
        private const val TAG = "DarpanSignaling"
    }
}
