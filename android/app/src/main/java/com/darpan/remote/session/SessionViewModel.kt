package com.darpan.remote.session

import android.app.Application
import android.content.Intent
import android.graphics.Bitmap
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.darpan.remote.files.FileTransferManager
import com.darpan.remote.settings.RecentRoomEntry
import com.darpan.remote.settings.SettingsRepository
import com.darpan.remote.signaling.RoomMember
import com.darpan.remote.signaling.SignalingClient
import com.darpan.remote.signaling.SignalingController
import com.darpan.remote.webrtc.WebRtcSession
import java.io.InputStream
import java.io.OutputStream
import org.json.JSONObject

data class IncomingOfferEvent(
    val transferId: Long,
    val fileName: String,
    val size: Long,
    val sha256: ByteArray,
)

data class TransferRow(
    val id: Long,
    val name: String,
    val totalBytes: Long,
    val isOutgoing: Boolean,
    var transferredBytes: Long = 0,
    var speedBps: Double = 0.0,
    var elapsedSec: Long = 0,
    var statusText: String = "Starting…",
)

data class TransferHistoryItem(
    val id: Long,
    val name: String,
    val isOutgoing: Boolean,
    val success: Boolean,
    val message: String,
    val timestampMs: Long = System.currentTimeMillis(),
)

class SessionViewModel(app: Application) : AndroidViewModel(app), SignalingClient.Listener {
    private val settings = SettingsRepository(app)
    private val controller = SignalingController()
    private val signaling = SignalingClient(this)

    private var webRtc: WebRtcSession? = null
    private var lastRemotePeerId: String = ""
    private var pendingHostProjection: Pair<Int, Intent>? = null
    private var leaveInProgress = false

    private val _status = MutableLiveData("Disconnected")
    val status: LiveData<String> = _status

    private val _roomId = MutableLiveData<String?>()
    val roomId: LiveData<String?> = _roomId

    private val _members = MutableLiveData<List<RoomMember>>(emptyList())
    val members: LiveData<List<RoomMember>> = _members

    private val _preview = MutableLiveData<Bitmap?>()
    val preview: LiveData<Bitmap?> = _preview

    private val _streamSize = MutableLiveData(Pair(0, 0))
    val streamSize: LiveData<Pair<Int, Int>> = _streamSize

    private val _controlGranted = MutableLiveData(false)
    val controlGranted: LiveData<Boolean> = _controlGranted

    private val _localRole = MutableLiveData("")
    val localRole: LiveData<String> = _localRole

    // File transfer state
    private val _incomingOffer = MutableLiveData<IncomingOfferEvent?>()
    val incomingOffer: LiveData<IncomingOfferEvent?> = _incomingOffer

    private val _activeTransfers = MutableLiveData<List<TransferRow>>(emptyList())
    val activeTransfers: LiveData<List<TransferRow>> = _activeTransfers

    private val _transferHistory = MutableLiveData<List<TransferHistoryItem>>(emptyList())
    val transferHistory: LiveData<List<TransferHistoryItem>> = _transferHistory

    private val _recentRooms = MutableLiveData<List<RecentRoomEntry>>(emptyList())
    val recentRooms: LiveData<List<RecentRoomEntry>> = _recentRooms

    init {
        refreshRecentRooms()
        signaling.applySettings(settings)
        controller.listener =
            object : SignalingController.Listener {
                override fun onRegistered(deviceId: String) {
                    _status.value = "Registered"
                }

                override fun onRoomCreated(roomId: String, role: String) {
                    _roomId.value = roomId
                    _localRole.value = role
                    _status.value = "Room created — share screen or wait for PC"
                    settings.addRecentRoom(roomId, false)
                    refreshRecentRooms()
                    pendingHostProjection?.let { (code, data) ->
                        startHostWebRtc(code, data)
                        pendingHostProjection = null
                    }
                }

                override fun onRoomJoined(roomId: String, role: String) {
                    _roomId.value = roomId
                    _localRole.value = role
                    _status.value = "Joined — waiting for WebRTC"
                    refreshRecentRooms()
                }

                override fun onRoomLeft() {
                    tearDownSession(notifyServer = false)
                }

                override fun onMembersUpdated(members: List<RoomMember>) {
                    _members.value = members
                    updateControlFlag()
                }

                override fun onMemberJoined(member: RoomMember) {
                    if (member.deviceId == controller.deviceId) return
                    lastRemotePeerId = member.deviceId
                    webRtc?.setRemoteDeviceId(member.deviceId)
                    if (controller.localRole == "host") {
                        webRtc?.createOfferToPeer()
                    }
                }

                override fun onSignalOffer(fromDeviceId: String, sdp: String) {
                    lastRemotePeerId = fromDeviceId
                    ensureWebRtc().setRemoteDeviceId(fromDeviceId)
                    ensureWebRtc().handleRemoteOffer(sdp)
                }

                override fun onSignalAnswer(fromDeviceId: String, sdp: String) {
                    ensureWebRtc().handleRemoteAnswer(sdp)
                }

                override fun onSignalIce(fromDeviceId: String, candidate: JSONObject) {
                    ensureWebRtc().handleRemoteIce(candidate)
                }

                override fun onControlState(deviceId: String, state: String) {
                    updateControlFlag()
                }

                override fun onControlRequest(deviceId: String, displayName: String) {
                    _status.value = "Control request from $displayName (PC view-only for Android host)"
                }

                override fun onError(code: String, message: String) {
                    _status.value = "$code: $message"
                }
            }
        connectSignaling()
    }

    fun refreshRecentRooms() {
        _recentRooms.value = settings.getRecentRooms()
    }

    fun connectSignaling() {
        signaling.applySettings(settings)
        signaling.connect(settings.normalizeSignalingUrl(settings.signalingUrl))
    }

    fun reconnectSignaling() {
        _status.value = "Reconnecting signaling…"
        connectSignaling()
    }

    fun reconnectPeerVideo() {
        _status.value = "Reconnecting video…"
        if (controller.localRole == "host") {
            webRtc?.createOfferToPeer()
        } else {
            ensureWebRtc().prepareAsClient()
        }
    }

    override fun onConnected() {
        signaling.sendHello(settings.deviceName)
    }

    override fun onDisconnected() {
        _status.value = "Signaling disconnected"
    }

    override fun onMessage(json: String) {
        controller.handleMessage(json)
    }

    override fun onError(message: String) {
        _status.value = message
    }

    fun createRoom(pin: String?, projectionResult: Pair<Int, Intent>?) {
        pendingHostProjection = projectionResult
        signaling.sendCreateRoom(pin)
    }

    fun joinRoom(code: String, pin: String?) {
        val cleanCode = code.trim().uppercase()
        settings.addRecentRoom(cleanCode, !pin.isNullOrEmpty())
        refreshRecentRooms()
        ensureWebRtc().prepareAsClient()
        signaling.sendJoinRoom(cleanCode, pin, "controller")
    }

    fun leaveSession() {
        tearDownSession(notifyServer = true)
    }

    private fun tearDownSession(notifyServer: Boolean) {
        if (leaveInProgress) return
        leaveInProgress = true
        webRtc?.stop()
        webRtc = null
        if (notifyServer) {
            signaling.sendLeaveRoom()
        }
        _roomId.value = null
        _preview.value = null
        _streamSize.value = Pair(0, 0)
        _controlGranted.value = false
        lastRemotePeerId = ""
        _members.value = emptyList()
        _activeTransfers.value = emptyList()
        leaveInProgress = false
    }

    fun requestControl() {
        signaling.sendRequestControl()
        _status.value = "Control requested — approve on PC host"
    }

    fun revokeControl(targetDeviceId: String) {
        signaling.sendRevokeControl(targetDeviceId)
    }

    fun sendControl(payload: ByteArray) {
        webRtc?.sendControl(payload)
    }

    // File transfer methods
    fun sendFile(fileName: String, size: Long, inputStream: InputStream) {
        ensureWebRtc().fileTransfer.sendFile(fileName, size, inputStream)
    }

    fun acceptIncomingFile(transferId: Long, outputStream: OutputStream) {
        _incomingOffer.value = null
        ensureWebRtc().fileTransfer.acceptIncoming(transferId, outputStream)
    }

    fun rejectIncomingFile(transferId: Long) {
        _incomingOffer.value = null
        ensureWebRtc().fileTransfer.rejectIncoming(transferId)
    }

    fun clearIncomingOffer() {
        _incomingOffer.value = null
    }

    fun cancelTransfer(transferId: Long) {
        ensureWebRtc().fileTransfer.cancelTransfer(transferId)
    }

    private fun setupFileTransferListener(session: WebRtcSession) {
        session.fileTransfer.listener = object : FileTransferManager.Listener {
            override fun onIncomingOffer(transferId: Long, fileName: String, size: Long, sha256: ByteArray) {
                _incomingOffer.postValue(IncomingOfferEvent(transferId, fileName, size, sha256))
            }

            override fun onTransferStarted(transferId: Long, fileName: String, size: Long, isOutgoing: Boolean) {
                val current = _activeTransfers.value.orEmpty().toMutableList()
                current.removeAll { it.id == transferId }
                current.add(
                    TransferRow(
                        id = transferId,
                        name = fileName,
                        totalBytes = size,
                        isOutgoing = isOutgoing,
                    )
                )
                _activeTransfers.postValue(current)
            }

            override fun onTransferProgress(
                transferId: Long,
                transferred: Long,
                total: Long,
                speedBps: Double,
                elapsedSec: Long,
            ) {
                val current = _activeTransfers.value.orEmpty().toMutableList()
                val idx = current.indexOfFirst { it.id == transferId }
                if (idx != -1) {
                    val row = current[idx]
                    row.transferredBytes = transferred
                    row.speedBps = speedBps
                    row.elapsedSec = elapsedSec
                    val pct = if (total > 0) (transferred.toDouble() / total.toDouble() * 100.0).toInt() else 0
                    val speedStr = if (speedBps > 0) " | ${FileTransferManager.formatBytes(speedBps.toLong())}/s" else ""
                    val timeStr = if (elapsedSec > 0) " | ${elapsedSec}s" else ""
                    row.statusText = "${FileTransferManager.formatBytes(transferred)} / ${FileTransferManager.formatBytes(total)} ($pct%)$speedStr$timeStr"
                    _activeTransfers.postValue(current)
                }
            }

            override fun onTransferFinished(transferId: Long, success: Boolean, message: String) {
                val current = _activeTransfers.value.orEmpty().toMutableList()
                val idx = current.indexOfFirst { it.id == transferId }
                val row = if (idx != -1) current.removeAt(idx) else null
                _activeTransfers.postValue(current)

                val history = _transferHistory.value.orEmpty().toMutableList()
                val name = row?.name ?: "Transfer #$transferId"
                val dir = row?.isOutgoing ?: false
                history.add(0, TransferHistoryItem(transferId, name, dir, success, message))
                _transferHistory.postValue(history)
            }
        }
    }

    private fun newWebRtcSession(): WebRtcSession {
        val s = WebRtcSession(
            getApplication(),
            settings.stunServer,
            settings.turnServer,
            settings.turnUsername,
            settings.turnPassword,
            webRtcCallback(),
        )
        setupFileTransferListener(s)
        return s
    }

    private fun startHostWebRtc(resultCode: Int, data: Intent) {
        webRtc =
            newWebRtcSession().also {
                it.setRemoteDeviceId(lastRemotePeerId)
                it.startAsHostWithProjection(resultCode, data)
            }
    }

    private fun ensureWebRtc(): WebRtcSession {
        if (webRtc == null) {
            webRtc = newWebRtcSession()
        }
        return webRtc!!
    }

    private fun webRtcCallback() =
        object : WebRtcSession.Callback {
            override fun onLocalOffer(sdp: String) {
                signaling.sendSignalOffer(lastRemotePeerId, sdp)
            }

            override fun onLocalAnswer(sdp: String) {
                signaling.sendSignalAnswer(lastRemotePeerId, sdp)
            }

            override fun onIceCandidate(targetDeviceId: String, candidate: JSONObject) {
                signaling.sendSignalIce(targetDeviceId, candidate)
            }

            override fun onPreviewFrame(bitmap: Bitmap, width: Int, height: Int) {
                _streamSize.postValue(Pair(width, height))
                _preview.postValue(bitmap)
            }

            override fun onConnected() {
                _status.postValue("Peer connected")
            }

            override fun onDisconnected() {
                _status.postValue("Peer disconnected")
            }

            override fun onError(message: String) {
                _status.postValue(message)
            }
        }

    private fun updateControlFlag() {
        val self = controller.deviceId
        val granted =
            controller.members.any {
                it.deviceId == self && it.controlState == "control_granted"
            }
        _controlGranted.postValue(granted)
    }

    fun settingsRepo(): SettingsRepository = settings

    fun deviceId(): String = controller.deviceId
}
