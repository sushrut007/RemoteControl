package com.darpan.remote.webrtc

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.Looper
import android.util.DisplayMetrics
import android.util.Log
import android.view.WindowManager
import org.webrtc.DataChannel
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.json.JSONObject

class WebRtcSession(
    private val context: Context,
    private val stunUrl: String,
    private val turnUrl: String,
    private val turnUsername: String,
    private val turnPassword: String,
    private val callback: Callback,
) {
    interface Callback {
        fun onLocalOffer(sdp: String)
        fun onLocalAnswer(sdp: String)
        fun onIceCandidate(targetDeviceId: String, candidate: JSONObject)
        fun onPreviewFrame(bitmap: Bitmap, width: Int, height: Int)
        fun onConnected()
        fun onDisconnected()
        fun onError(message: String)
    }

    private val handler = Handler(Looper.getMainLooper())
    private var factory: PeerConnectionFactory? = null
    private var pc: PeerConnection? = null
    private var previewChannel: DataChannel? = null
    private var controlChannel: DataChannel? = null
    private var remoteDeviceId: String = ""
    private var isHost = false

    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var captureWidth = 720
    private var captureHeight = 1280
    private var captureRunning = false

    init {
        PeerConnectionFactory.initialize(
            PeerConnectionFactory.InitializationOptions.builder(context).createInitializationOptions(),
        )
        val encoderFactory = DefaultVideoEncoderFactory(null, true, true)
        val decoderFactory = DefaultVideoDecoderFactory(null)
        factory =
            PeerConnectionFactory.builder()
                .setVideoEncoderFactory(encoderFactory)
                .setVideoDecoderFactory(decoderFactory)
                .createPeerConnectionFactory()
    }

    fun setRemoteDeviceId(id: String) {
        remoteDeviceId = id
    }

    fun startAsHostWithProjection(resultCode: Int, data: Intent) {
        stop()
        isHost = true
        val mgr = context.getSystemService(MediaProjectionManager::class.java)
        projection = mgr.getMediaProjection(resultCode, data)
        setupPeerConnection(asHost = true)
        createHostChannels()
        startPreviewCaptureLoop()
    }

    /** Call when a remote peer has joined and `remoteDeviceId` is set. */
    fun createOfferToPeer() {
        if (!isHost || remoteDeviceId.isEmpty()) return
        createOffer()
    }

    fun prepareAsClient() {
        stop()
        isHost = false
        setupPeerConnection(asHost = false)
    }

    fun handleRemoteOffer(sdp: String) {
        if (pc == null) prepareAsClient()
        val desc = SessionDescription(SessionDescription.Type.OFFER, sdp)
        pc?.setRemoteDescription(
            object : SdpObserver by simpleSdpObserver("setRemoteOffer") {
                override fun onSetSuccess() {
                    createAnswer()
                }
            },
            desc,
        )
    }

    fun handleRemoteAnswer(sdp: String) {
        val desc = SessionDescription(SessionDescription.Type.ANSWER, sdp)
        pc?.setRemoteDescription(simpleSdpObserver("setRemoteAnswer"), desc)
    }

    fun handleRemoteIce(candidate: JSONObject) {
        val ice =
            IceCandidate(
                candidate.optString("sdpMid"),
                candidate.optInt("sdpMLineIndex"),
                candidate.getString("candidate"),
            )
        pc?.addIceCandidate(ice)
    }

    fun sendControl(payload: ByteArray) {
        val channel = controlChannel ?: return
        if (channel.state() != DataChannel.State.OPEN) return
        channel.send(DataChannel.Buffer(ByteBuffer.wrap(payload), true))
    }

    fun stop() {
        captureRunning = false
        virtualDisplay?.release()
        imageReader?.close()
        projection?.stop()
        virtualDisplay = null
        imageReader = null
        projection = null
        previewChannel?.close()
        controlChannel?.close()
        previewChannel = null
        controlChannel = null
        pc?.close()
        pc = null
    }

    private fun buildIceServers(): List<PeerConnection.IceServer> {
        val list = mutableListOf(PeerConnection.IceServer.builder(stunUrl).createIceServer())
        val turn = turnUrl.trim()
        if (turn.isNotEmpty()) {
            val builder = PeerConnection.IceServer.builder(turn)
            if (turnUsername.isNotEmpty()) {
                builder.setUsername(turnUsername).setPassword(turnPassword)
            }
            list.add(builder.createIceServer())
        }
        return list
    }

    private fun setupPeerConnection(asHost: Boolean) {
        val servers = buildIceServers()
        val config =
            PeerConnection.RTCConfiguration(servers).apply {
                sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            }
        pc =
            factory?.createPeerConnection(
                config,
                object : PeerConnection.Observer {
                    override fun onSignalingChange(state: PeerConnection.SignalingState?) {}

                    override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {
                        when (state) {
                            PeerConnection.IceConnectionState.CONNECTED,
                            PeerConnection.IceConnectionState.COMPLETED,
                            -> callback.onConnected()
                            PeerConnection.IceConnectionState.DISCONNECTED,
                            PeerConnection.IceConnectionState.FAILED,
                            -> callback.onDisconnected()
                            else -> {}
                        }
                    }

                    override fun onIceConnectionReceivingChange(receiving: Boolean) {}

                    override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {}

                    override fun onIceCandidate(candidate: IceCandidate?) {
                        candidate ?: return
                        if (remoteDeviceId.isEmpty()) return
                        val json =
                            JSONObject()
                                .put("candidate", candidate.sdp)
                                .put("sdpMid", candidate.sdpMid)
                                .put("sdpMLineIndex", candidate.sdpMLineIndex)
                        callback.onIceCandidate(remoteDeviceId, json)
                    }

                    override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}

                    override fun onAddStream(stream: org.webrtc.MediaStream?) {}

                    override fun onRemoveStream(stream: org.webrtc.MediaStream?) {}

                    override fun onDataChannel(channel: DataChannel?) {
                        channel ?: return
                        wireDataChannel(channel)
                    }

                    override fun onRenegotiationNeeded() {}

                    override fun onAddTrack(
                        receiver: org.webrtc.RtpReceiver?,
                        streams: Array<out org.webrtc.MediaStream>?,
                    ) {
                    }
                },
            )
    }

    private fun createHostChannels() {
        val init = DataChannel.Init().apply { ordered = true }
        previewChannel = pc?.createDataChannel("darpan.preview", init)
        wireDataChannel(previewChannel)
        pc?.createDataChannel("darpan.control", init)
        pc?.createDataChannel("darpan.files", init)
    }

    private fun wireDataChannel(channel: DataChannel?) {
        channel ?: return
        when (channel.label()) {
            "darpan.preview" -> {
                previewChannel = channel
                if (!isHost) {
                    channel.registerObserver(
                        object : DataChannel.Observer {
                            override fun onBufferedAmountChange(amount: Long) {}

                            override fun onStateChange() {}

                            override fun onMessage(buffer: DataChannel.Buffer?) {
                                buffer ?: return
                                val bytes = ByteArray(buffer.data.remaining())
                                buffer.data.get(bytes)
                                parsePreview(bytes)
                            }
                        },
                    )
                }
            }
            "darpan.control" -> {
                controlChannel = channel
            }
            else -> {}
        }
    }

    private fun parsePreview(bytes: ByteArray) {
        if (bytes.size < 8 || !bytes.copyOfRange(0, 4).contentEquals("DPJ1".toByteArray())) return
        val w =
            (bytes[4].toInt() and 0xff) or ((bytes[5].toInt() and 0xff) shl 8)
        val h =
            (bytes[6].toInt() and 0xff) or ((bytes[7].toInt() and 0xff) shl 8)
        val jpeg = bytes.copyOfRange(8, bytes.size)
        val bitmap = android.graphics.BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size) ?: return
        handler.post { callback.onPreviewFrame(bitmap, w, h) }
    }

    private fun startPreviewCaptureLoop() {
        val wm = context.getSystemService(WindowManager::class.java)
        val metrics = DisplayMetrics()
        @Suppress("DEPRECATION")
        wm.defaultDisplay.getRealMetrics(metrics)
        captureWidth = (metrics.widthPixels * 0.5f).toInt().coerceAtMost(1280)
        captureHeight = (metrics.heightPixels * captureWidth / metrics.widthPixels)

        imageReader =
            ImageReader.newInstance(captureWidth, captureHeight, PixelFormat.RGBA_8888, 2)
        virtualDisplay =
            projection?.createVirtualDisplay(
                "darpan-capture",
                captureWidth,
                captureHeight,
                metrics.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                imageReader?.surface,
                null,
                handler,
            )
        captureRunning = true
        Thread {
            while (captureRunning) {
                val image = imageReader?.acquireLatestImage()
                if (image != null) {
                    try {
                        sendPreviewFromImage(image)
                    } finally {
                        image.close()
                    }
                }
                Thread.sleep(100)
            }
        }.start()
    }

    private fun sendPreviewFromImage(image: android.media.Image) {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowPadding = rowStride - pixelStride * captureWidth
        val bitmap =
            Bitmap.createBitmap(
                captureWidth + rowPadding / pixelStride,
                captureHeight,
                Bitmap.Config.ARGB_8888,
            )
        bitmap.copyPixelsFromBuffer(buffer)
        val cropped = Bitmap.createBitmap(bitmap, 0, 0, captureWidth, captureHeight)
        bitmap.recycle()
        val jpeg = ByteArrayOutputStream()
        cropped.compress(Bitmap.CompressFormat.JPEG, 72, jpeg)
        cropped.recycle()
        val packet = ByteArrayOutputStream()
        packet.write("DPJ1".toByteArray())
        packet.write(
            byteArrayOf(
                (captureWidth and 0xff).toByte(),
                ((captureWidth shr 8) and 0xff).toByte(),
                (captureHeight and 0xff).toByte(),
                ((captureHeight shr 8) and 0xff).toByte(),
            ),
        )
        packet.write(jpeg.toByteArray())
        val ch = previewChannel
        if (ch != null && ch.state() == DataChannel.State.OPEN) {
            ch.send(DataChannel.Buffer(ByteBuffer.wrap(packet.toByteArray()), true))
        }
    }

    private fun createOffer() {
        val constraints =
            MediaConstraints().apply {
                mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
            }
        pc?.createOffer(
            object : SdpObserver by simpleCreateSdpObserver(SessionDescription.Type.OFFER) {},
            constraints,
        )
    }

    private fun createAnswer() {
        val constraints = MediaConstraints()
        pc?.createAnswer(
            object : SdpObserver by simpleCreateSdpObserver(SessionDescription.Type.ANSWER) {},
            constraints,
        )
    }

    private fun simpleCreateSdpObserver(type: SessionDescription.Type): SdpObserver =
        object : SdpObserver {
            override fun onCreateSuccess(desc: SessionDescription?) {
                desc ?: return
                pc?.setLocalDescription(simpleSdpObserver("setLocal"), desc)
                when (type) {
                    SessionDescription.Type.OFFER -> callback.onLocalOffer(desc.description)
                    SessionDescription.Type.ANSWER -> callback.onLocalAnswer(desc.description)
                    else -> {}
                }
            }

            override fun onSetSuccess() {}

            override fun onCreateFailure(error: String?) {
                callback.onError(error ?: "SDP error")
            }

            override fun onSetFailure(error: String?) {
                callback.onError(error ?: "SDP set error")
            }
        }

    private fun simpleSdpObserver(name: String): SdpObserver =
        object : SdpObserver {
            override fun onCreateSuccess(desc: SessionDescription?) {}

            override fun onSetSuccess() {}

            override fun onCreateFailure(error: String?) {
                Log.e(TAG, "$name create failure: $error")
            }

            override fun onSetFailure(error: String?) {
                Log.e(TAG, "$name set failure: $error")
            }
        }

    companion object {
        private const val TAG = "DarpanWebRtc"
    }
}
