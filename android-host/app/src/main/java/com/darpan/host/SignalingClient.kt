package com.darpan.host

import android.util.Log
import io.socket.client.Ack
import io.socket.client.IO
import io.socket.client.Socket
import io.socket.engineio.client.transports.WebSocket
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.URI
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.math.min
import kotlin.math.pow

interface SignalingListener {
    fun onConnected() {}
    fun onDisconnected(reason: String) {}
    fun onPeerJoined(peer: PeerInfo) {}
    fun onPeerLeft(peerId: String) {}
    fun onStreamReady(peerId: String) {}
    fun onStreamStopped(peerId: String) {}
    fun onKicked() {}
    fun onControlPayload(payload: JSONObject) {}
    fun onMousePayload(payload: JSONObject) {}
    fun onKeyboardPayload(payload: JSONObject) {}
    fun onVideoPacket(payload: JSONObject) {}
    fun onError(message: String) {}
    fun onLog(message: String) {}
}

class SignalingClient(
    private val listenerProvider: () -> SignalingListener,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var socket: Socket? = null
    private var serverUrl: String = ""
    private var reconnectJob: Job? = null
    private var reconnectAttempt = 0
    private var manualDisconnect = false
    private val extraListeners = CopyOnWriteArrayList<(String, Array<Any>?) -> Unit>()

    private val listener: SignalingListener
        get() = listenerProvider()

    val isConnected: Boolean
        get() = socket?.connected() == true

    fun connect(url: String) {
        serverUrl = normalizeUrl(url)
        manualDisconnect = false
        reconnectAttempt = 0
        openSocket(serverUrl)
    }

    fun disconnect() {
        manualDisconnect = true
        reconnectJob?.cancel()
        socket?.off()
        socket?.disconnect()
        socket = null
    }

    fun on(event: String, handler: (Array<Any>?) -> Unit) {
        extraListeners.add { name, args ->
            if (name == event) handler(args)
        }
    }

    suspend fun emitAck(event: String, payload: JSONObject): SignalingAck =
        withContext(Dispatchers.IO) {
            val deferred = CompletableDeferred<SignalingAck>()
            val s = socket
            if (s == null || !s.connected()) {
                return@withContext SignalingAck.Err("not connected")
            }
            log("TX ack $event")
            s.emit(event, payload, Ack { args ->
                deferred.complete(parseAck(args))
            })
            deferred.await()
        }

    fun emit(event: String, payload: JSONObject = JSONObject(), ack: ((SignalingAck) -> Unit)? = null) {
        scope.launch {
            if (ack != null) {
                ack(emitAck(event, payload))
            } else {
                if (socket?.connected() == true) {
                    socket?.emit(event, payload)
                } else {
                    log("TX skipped (offline): $event")
                }
            }
        }
    }

    private fun openSocket(url: String) {
        scope.launch {
            try {
                socket?.off()
                socket?.disconnect()

                val uri = URI.create(url)
                val headers = mutableMapOf<String, List<String>>()
                headers["Origin"] = listOf(SIGNALING_ORIGIN)

                val options = IO.Options.builder()
                    .setPath("/socket.io/")
                    .setTransports(arrayOf(WebSocket.NAME))
                    .setReconnection(false)
                    .setForceNew(true)
                    .setSecure(uri.scheme.equals("https", ignoreCase = true))
                    .setExtraHeaders(headers)
                    .build()

                log("Connecting to $url")
                val s = IO.socket(uri, options)
                wireEvents(s)
                socket = s
                s.connect()
            } catch (ex: Exception) {
                Log.e(TAG, "connect failed", ex)
                listener.onError(ex.message ?: "connect failed")
                scheduleReconnect()
            }
        }
    }

    private fun wireEvents(s: Socket) {
        s.on(Socket.EVENT_CONNECT) {
            log("Socket.IO connected")
            reconnectAttempt = 0
            listener.onConnected()
        }
        s.on(Socket.EVENT_DISCONNECT) { args ->
            val reason = args.firstOrNull()?.toString() ?: "unknown"
            log("Socket.IO disconnected: $reason")
            listener.onDisconnected(reason)
            if (!manualDisconnect) scheduleReconnect()
        }
        s.on(Socket.EVENT_CONNECT_ERROR) { args ->
            val msg = args.firstOrNull()?.toString() ?: "connect error"
            log("Socket.IO connect error: $msg")
            listener.onError(msg)
            if (!manualDisconnect) scheduleReconnect()
        }

        s.on("peer-joined") { args ->
            val payload = firstPayload(args)
            val peer = peerFromJson(payload)
            if (peer.peerId.isNotBlank()) listener.onPeerJoined(peer)
            dispatch("peer-joined", args)
        }
        s.on("peer-left") { args ->
            val payload = firstPayload(args)
            listener.onPeerLeft(payload.optString("peerId"))
            dispatch("peer-left", args)
        }
        s.on("stream-ready") { args ->
            val payload = firstPayload(args)
            listener.onStreamReady(payload.optString("peerId"))
            dispatch("stream-ready", args)
        }
        s.on("stream-stopped") { args ->
            val payload = firstPayload(args)
            listener.onStreamStopped(payload.optString("peerId"))
            dispatch("stream-stopped", args)
        }
        s.on("kicked") { _ ->
            listener.onKicked()
            dispatch("kicked", args = null)
        }
        s.on("control") { args ->
            val payload = firstPayload(args)
            listener.onControlPayload(payload)
            dispatch("control", args)
        }
        s.on("mouse") { args ->
            val payload = firstPayload(args)
            listener.onMousePayload(payload)
            dispatch("mouse", args)
        }
        s.on("keyboard") { args ->
            val payload = firstPayload(args)
            listener.onKeyboardPayload(payload)
            dispatch("keyboard", args)
        }
        s.on("video-packet") { args ->
            val payload = firstPayload(args)
            listener.onVideoPacket(payload)
            dispatch("video-packet", args)
        }
        s.on("new-producer") { args -> dispatch("new-producer", args) }
    }

    private fun dispatch(event: String, args: Array<Any>?) {
        extraListeners.forEach { it(event, args) }
    }

    private fun scheduleReconnect() {
        if (manualDisconnect || serverUrl.isBlank()) return
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            val delayMs = min(
                30_000L,
                (1_000L * 2.0.pow(reconnectAttempt.toDouble())).toLong(),
            )
            reconnectAttempt++
            log("Reconnecting in ${delayMs}ms (attempt $reconnectAttempt)")
            delay(delayMs)
            if (!manualDisconnect) openSocket(serverUrl)
        }
    }

    private fun log(message: String) {
        Log.d(TAG, message)
        listener.onLog(message)
    }

    companion object {
        private const val TAG = "SignalingClient"
        const val SIGNALING_ORIGIN = "https://remotecontrol.sushrutmakes.qzz.io"
        const val DEFAULT_SERVER_URL = "https://remotecontrol.sushrutmakes.qzz.io"

        fun normalizeUrl(raw: String): String {
            var url = raw.trim()
            if (url.startsWith("ws://")) url = "http://" + url.removePrefix("ws://")
            if (url.startsWith("wss://")) url = "https://" + url.removePrefix("wss://")
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "https://$url"
            }
            return url.trimEnd('/')
        }
    }
}
