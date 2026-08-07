package com.darpan.host

import org.json.JSONArray
import org.json.JSONObject

object ControlSender {

    fun mouse(
        signaling: SignalingClient,
        localPeerId: String,
        x: Float,
        y: Float,
        eventType: String,
        button: String = "left",
        deltaX: Float = 0f,
        deltaY: Float = 0f,
    ) {
        signaling.emit(
            "control",
            JSONObject()
                .put("type", "mouse")
                .put("x", x.toDouble())
                .put("y", y.toDouble())
                .put("deltaX", deltaX.toDouble())
                .put("deltaY", deltaY.toDouble())
                .put("eventType", eventType)
                .put("button", button)
                .put("senderId", localPeerId),
        )
    }

    fun keyboard(
        signaling: SignalingClient,
        localPeerId: String,
        key: String,
        eventType: String,
    ) {
        signaling.emit(
            "control",
            JSONObject()
                .put("type", "keyboard")
                .put("key", key)
                .put("eventType", eventType)
                .put("modifiers", JSONArray())
                .put("senderId", localPeerId),
        )
    }
}
