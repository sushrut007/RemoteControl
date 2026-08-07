package com.darpan.host

import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap

enum class SessionState {
    Idle,
    Connecting,
    Connected,
    Sharing,
    Streaming,
    Error,
}

data class SessionUiState(
    val state: SessionState = SessionState.Idle,
    val appRole: String = "host",
    val roomId: String = "",
    val localPeerId: String = "",
    val serverUrl: String = "",
    val displayName: String = "",
    val peers: List<PeerInfo> = emptyList(),
    val statusMessage: String = "",
    val logLines: List<String> = emptyList(),
    val isAccessibilityReady: Boolean = false,
    val isSharing: Boolean = false,
    val controlAllowed: Boolean = true,
    val waitingForStream: Boolean = true,
)

class HostSessionManager(
    private val signaling: SignalingClient,
    private val mediasoup: MediasoupSession,
    private val controlProcessor: ControlEventProcessor,
) : SignalingListener {

    private val peers = ConcurrentHashMap<String, PeerInfo>()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val _uiState = MutableStateFlow(SessionUiState())
    val uiState: StateFlow<SessionUiState> = _uiState.asStateFlow()

    private var localPeerId: String = ""
    private var roomId: String = ""
    private var appRole: String = "host"
    private var deviceControlEnabled = false
    private var controlAllowedByHost = true
    private var pendingVideoPackets = 0
    private var joinCompleter: CompletableDeferred<Result<Unit>>? = null

    var onVideoPacket: ((base64: String, isKeyframe: Boolean) -> Unit)? = null

    suspend fun joinRoom(
        serverUrl: String,
        room: String,
        displayName: String,
        peerId: String,
        role: String,
    ): Result<Unit> {
        if (_uiState.value.state == SessionState.Connecting) {
            return Result.failure(IllegalStateException("Already connecting"))
        }

        appRole = role
        signaling.disconnect()
        controlProcessor.reset()

        updateState(
            state = SessionState.Connecting,
            appRole = role,
            roomId = room,
            serverUrl = serverUrl,
            displayName = displayName,
            status = "Connecting to signaling server…",
            logLines = emptyList(),
            waitingForStream = role != "host",
        )
        localPeerId = peerId
        roomId = room

        val completer = CompletableDeferred<Result<Unit>>()
        joinCompleter = completer

        appendLog("Joining as $role in room $room")
        signaling.connect(serverUrl)

        return try {
            withTimeout(30_000) { completer.await() }
        } catch (ex: Exception) {
            updateState(SessionState.Error, status = "Connection timed out")
            Result.failure(ex)
        }
    }

    suspend fun onSignalingConnectedAndJoin(): Result<Unit> {
        appendLog("Signaling connected — join-room")
        val ack = signaling.emitAck(
            "join-room",
            JoinRoomRequest(
                roomId = roomId,
                peerId = localPeerId,
                displayName = _uiState.value.displayName,
                appType = appRole,
            ).toJson(),
        )
        if (ack is SignalingAck.Err) {
            updateState(SessionState.Error, status = ack.message)
            appendLog("join-room failed: ${ack.message}")
            return Result.failure(IllegalStateException(ack.message))
        }

        val body = (ack as SignalingAck.Ok).body
        localPeerId = body.optString("peerId", localPeerId)
        appendLog("join-room ok peer=${localPeerId.take(8)}")

        val existing = body.optJSONArray("peers")
        if (existing != null) {
            for (i in 0 until existing.length()) {
                val peer = peerFromJson(existing.getJSONObject(i))
                if (peer.peerId.isNotBlank()) addPeer(peer)
            }
        }

        mediasoup.setupAfterJoin(body).onFailure { ex ->
            Log.w(TAG, "mediasoup setup failed: ${ex.message}")
            appendLog("mediasoup: ${ex.message}")
        }

        if (appRole == "host") {
            controlProcessor.setLocalPeerId(localPeerId)
            controlProcessor.setKnownPeers(peers.values.toList())
            controlProcessor.startMouseDrainTimer()
        }

        val status = when (appRole) {
            "host" -> "Connected — tap Start screen sharing"
            "viewer" -> "Waiting for host to start sharing…"
            else -> "Connected — touch pad controls remote host"
        }
        updateState(
            SessionState.Connected,
            localPeerId = localPeerId,
            status = status,
        )
        return Result.success(Unit)
    }

    fun setAccessibilityReady(ready: Boolean) {
        _uiState.value = _uiState.value.copy(isAccessibilityReady = ready)
    }

    fun setControlAllowed(allowed: Boolean) {
        controlAllowedByHost = allowed
        _uiState.value = _uiState.value.copy(controlAllowed = allowed)
        applyControlGate()
    }

    private fun applyControlGate() {
        if (appRole != "host") return
        val enabled = controlAllowedByHost && _uiState.value.isSharing
        deviceControlEnabled = enabled
        controlProcessor.setDeviceControlEnabled(enabled)
    }

    fun onSharingStarted() {
        if (appRole != "host") return
        updateState(SessionState.Sharing, isSharing = true, status = "Sharing screen")
        applyControlGate()
        signaling.emit("stream-ready", JSONObject())
        appendLog("stream-ready emitted")
    }

    fun onSharingStopped() {
        if (appRole != "host") return
        updateState(SessionState.Connected, isSharing = false, status = "Screen sharing stopped")
        applyControlGate()
        signaling.emit("stream-stopped", JSONObject())
        appendLog("stream-stopped emitted")
    }

    fun onSharingError(message: String) {
        updateState(SessionState.Error, isSharing = false, status = message)
        appendLog("share error: $message")
    }

    fun sendVideoPacket(base64Data: String, isKeyframe: Boolean) {
        if (appRole != "host") return
        if (!isKeyframe && pendingVideoPackets >= 2) return
        pendingVideoPackets++
        signaling.emit(
            "video-packet",
            JSONObject()
                .put("data", base64Data)
                .put("isKeyframe", isKeyframe),
        ) { ack ->
            pendingVideoPackets = (pendingVideoPackets - 1).coerceAtLeast(0)
            if (ack is SignalingAck.Err) {
                Log.w(TAG, "video-packet ack error: ${ack.message}")
            }
        }
    }

    fun kickPeer(peerId: String) {
        if (appRole != "host") return
        signaling.emit("kick-peer", JSONObject().put("peerId", peerId))
        appendLog("kick-peer $peerId")
    }

    suspend fun leaveRoom() {
        controlProcessor.stopMouseDrainTimer()
        signaling.emitAck("leave-room", JSONObject())
        mediasoup.reset()
        peers.clear()
        controlProcessor.reset()
        signaling.disconnect()
        joinCompleter = null
        onVideoPacket = null
        updateState(SessionState.Idle, status = "Disconnected")
    }

    override fun onConnected() {
        scope.launch {
            val result = onSignalingConnectedAndJoin()
            joinCompleter?.complete(result)
            joinCompleter = null
        }
    }

    override fun onDisconnected(reason: String) {
        if (_uiState.value.state != SessionState.Idle) {
            updateState(SessionState.Error, status = "Disconnected: $reason")
            appendLog("disconnected: $reason")
        }
        joinCompleter?.complete(Result.failure(IllegalStateException(reason)))
        joinCompleter = null
    }

    override fun onPeerJoined(peer: PeerInfo) {
        addPeer(peer)
        if (appRole == "host") {
            controlProcessor.addPeer(peer)
            signaling.emit("peer-ack", JSONObject().put("peerId", peer.peerId))
        }
        updateState(status = "Peer joined: ${peer.displayName} (${peer.appType})")
        appendLog("peer-joined ${peer.appType}")
    }

    override fun onPeerLeft(peerId: String) {
        peers.remove(peerId)
        controlProcessor.removePeer(peerId)
        updateState(peers = peers.values.toList(), status = "Peer left")
        appendLog("peer-left")
    }

    override fun onStreamReady(peerId: String) {
        if (appRole == "host") return
        updateState(
            state = SessionState.Streaming,
            waitingForStream = false,
            status = "Stream live",
        )
        appendLog("stream-ready from ${peerId.take(8)}")
    }

    override fun onStreamStopped(peerId: String) {
        if (appRole == "host") return
        updateState(
            state = SessionState.Connected,
            waitingForStream = true,
            status = "Host paused sharing",
        )
        appendLog("stream-stopped")
    }

    override fun onKicked() {
        updateState(SessionState.Error, status = "Removed from session")
    }

    override fun onVideoPacket(payload: JSONObject) {
        if (appRole == "host") return
        val data = payload.optString("data")
        if (data.isBlank()) return
        val isKeyframe = payload.optBoolean("isKeyframe", false)
        onVideoPacket?.invoke(data, isKeyframe)
        if (_uiState.value.waitingForStream && isKeyframe) {
            updateState(state = SessionState.Streaming, waitingForStream = false, status = "Receiving video")
        }
    }

    override fun onControlPayload(payload: JSONObject) {
        if (appRole != "host" || !deviceControlEnabled) return
        when (payload.optString("type")) {
            "mouse" -> controlProcessor.handleMouse(payload)
            "keyboard" -> controlProcessor.handleKeyboard(payload)
            else -> controlProcessor.handleControl(payload)
        }
    }

    override fun onMousePayload(payload: JSONObject) {
        if (appRole != "host" || !deviceControlEnabled) return
        controlProcessor.handleMouse(payload)
    }

    override fun onKeyboardPayload(payload: JSONObject) {
        if (appRole != "host" || !deviceControlEnabled) return
        controlProcessor.handleKeyboard(payload)
    }

    override fun onError(message: String) {
        updateState(SessionState.Error, status = message)
        appendLog("error: $message")
        joinCompleter?.complete(Result.failure(IllegalStateException(message)))
        joinCompleter = null
    }

    override fun onLog(message: String) {
        appendLog(message)
    }

    private fun addPeer(peer: PeerInfo) {
        peers[peer.peerId] = peer
        _uiState.value = _uiState.value.copy(peers = peers.values.toList())
    }

    private fun appendLog(line: String) {
        val next = (_uiState.value.logLines + line).takeLast(10)
        _uiState.value = _uiState.value.copy(logLines = next)
    }

    private fun updateState(
        state: SessionState = _uiState.value.state,
        appRole: String = _uiState.value.appRole,
        roomId: String = _uiState.value.roomId,
        localPeerId: String = _uiState.value.localPeerId,
        serverUrl: String = _uiState.value.serverUrl,
        displayName: String = _uiState.value.displayName,
        peers: List<PeerInfo> = _uiState.value.peers,
        status: String = _uiState.value.statusMessage,
        logLines: List<String> = _uiState.value.logLines,
        isSharing: Boolean = _uiState.value.isSharing,
        waitingForStream: Boolean = _uiState.value.waitingForStream,
    ) {
        _uiState.value = SessionUiState(
            state = state,
            appRole = appRole,
            roomId = roomId,
            localPeerId = localPeerId,
            serverUrl = serverUrl,
            displayName = displayName,
            peers = peers,
            statusMessage = status,
            logLines = logLines,
            isAccessibilityReady = _uiState.value.isAccessibilityReady,
            isSharing = isSharing,
            controlAllowed = _uiState.value.controlAllowed,
            waitingForStream = waitingForStream,
        )
    }

    companion object {
        private const val TAG = "HostSessionManager"
    }
}
