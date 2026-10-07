package com.darpan.remote.session

import android.app.Application
import android.content.Intent
import android.graphics.Bitmap
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.darpan.remote.settings.SettingsRepository
import com.darpan.remote.signaling.RoomMember
import com.darpan.remote.signaling.SignalingClient
import com.darpan.remote.signaling.SignalingController
import com.darpan.remote.webrtc.WebRtcSession
import org.json.JSONObject

class SessionViewModel(app: Application) : AndroidViewModel(app), SignalingClient.Listener {
    private val settings = SettingsRepository(app)
    private val controller = SignalingController()
    private val signaling =
        SignalingClient(this).also {
            // connected below
        }

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

    init {
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
                    pendingHostProjection?.let { (code, data) ->
                        startHostWebRtc(code, data)
                        pendingHostProjection = null
                    }
                }

                override fun onRoomJoined(roomId: String, role: String) {
                    _roomId.value = roomId
                    _localRole.value = role
                    _status.value = "Joined — waiting for WebRTC"
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
                    // Android as host of phone screen: PC control not supported in v1
                    _status.value = "Control request from $displayName (PC view-only for Android host)"
                }

                override fun onError(code: String, message: String) {
                    _status.value = "$code: $message"
                }
            }
        connectSignaling()
    }

    fun connectSignaling() {
        signaling.applySettings(settings)
        signaling.connect(settings.normalizeSignalingUrl(settings.signalingUrl))
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
        ensureWebRtc().prepareAsClient()
        signaling.sendJoinRoom(code, pin, "controller")
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
        leaveInProgress = false
    }

    fun requestControl() {
        signaling.sendRequestControl()
        _status.value = "Control requested — approve on PC host"
    }

    fun sendControl(payload: ByteArray) {
        webRtc?.sendControl(payload)
    }

    private fun newWebRtcSession(): WebRtcSession =
        WebRtcSession(
            getApplication(),
            settings.stunServer,
            settings.turnServer,
            settings.turnUsername,
            settings.turnPassword,
            webRtcCallback(),
        )

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
