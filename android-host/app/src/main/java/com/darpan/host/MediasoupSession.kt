package com.darpan.host

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

/**
 * Sets up mediasoup SFU transports after join-room, matching the Qt RoomManager flow.
 *
 * The desktop Darpan host streams H.264 via Socket.IO [video-packet] events (not WebRTC
 * produce). This class completes create/connect transport handshakes for room parity.
 */
class MediasoupSession(
    private val signaling: SignalingClient,
) {
    private var rtpCapabilities: JSONObject = JSONObject()
    private var sendTransportId: String = ""
    private var recvTransportId: String = ""
    private var sendTransportParams: JSONObject = JSONObject()
    private var recvTransportParams: JSONObject = JSONObject()

    suspend fun setupAfterJoin(joinAck: JSONObject): Result<Unit> = withContext(Dispatchers.IO) {
        rtpCapabilities = joinAck.optJSONObject("rtpCapabilities") ?: JSONObject()
        if (rtpCapabilities.length() == 0) {
            return@withContext Result.failure(IllegalStateException("missing rtpCapabilities"))
        }

        val sendResult = createAndConnectTransport("send")
        if (sendResult.isFailure) return@withContext sendResult

        val recvResult = createAndConnectTransport("recv")
        if (recvResult.isFailure) return@withContext recvResult

        Log.i(TAG, "mediasoup transports ready (send=$sendTransportId recv=$recvTransportId)")
        Result.success(Unit)
    }

    private suspend fun createAndConnectTransport(direction: String): Result<Unit> {
        val createAck = signaling.emitAck(
            "create-transport",
            JSONObject().put("direction", direction),
        )
        if (createAck is SignalingAck.Err) {
            return Result.failure(IllegalStateException(createAck.message))
        }
        val params = (createAck as SignalingAck.Ok).body
        val transportId = params.optString("transportId", params.optString("id"))
        if (transportId.isBlank()) {
            return Result.failure(IllegalStateException("create-transport missing id"))
        }

        val dtls = params.optJSONObject("dtlsParameters") ?: JSONObject()
        val connectAck = signaling.emitAck(
            "connect-transport",
            JSONObject()
                .put("transportId", transportId)
                .put("dtlsParameters", dtls),
        )
        if (connectAck is SignalingAck.Err) {
            return Result.failure(IllegalStateException(connectAck.message))
        }

        if (direction == "send") {
            sendTransportId = transportId
            sendTransportParams = params
        } else {
            recvTransportId = transportId
            recvTransportParams = params
        }
        return Result.success(Unit)
    }

    suspend fun produceVideoPlaceholder(): Result<String> = withContext(Dispatchers.IO) {
        if (sendTransportId.isBlank()) {
            return@withContext Result.failure(IllegalStateException("send transport missing"))
        }
        // Placeholder RTP params – real WebRTC produce can replace this later.
        val rtpParameters = JSONObject()
            .put("codecs", rtpCapabilities.optJSONArray("codecs"))
            .put("headerExtensions", rtpCapabilities.optJSONArray("headerExtensions"))
            .put("encodings", org.json.JSONArray().put(JSONObject().put("ssrc", 11111111)))
            .put("rtcp", JSONObject().put("cname", "darpan-android-host"))

        val ack = signaling.emitAck(
            "produce",
            JSONObject()
                .put("transportId", sendTransportId)
                .put("kind", "video")
                .put("rtpParameters", rtpParameters)
                .put("appData", JSONObject().put("source", "android-host")),
        )
        when (ack) {
            is SignalingAck.Ok -> Result.success(ack.body.optString("producerId"))
            is SignalingAck.Err -> Result.failure(IllegalStateException(ack.message))
        }
    }

    fun reset() {
        rtpCapabilities = JSONObject()
        sendTransportId = ""
        recvTransportId = ""
        sendTransportParams = JSONObject()
        recvTransportParams = JSONObject()
    }

    companion object {
        private const val TAG = "MediasoupSession"
    }
}
