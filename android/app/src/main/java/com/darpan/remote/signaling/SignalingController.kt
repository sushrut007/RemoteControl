package com.darpan.remote.signaling

import org.json.JSONObject

data class RoomMember(
    val deviceId: String,
    val displayName: String,
    val role: String,
    val controlState: String?,
)

class SignalingController {
    var deviceId: String = ""
        private set
    var roomId: String = ""
        private set
    var localRole: String = ""
        private set
    var members: List<RoomMember> = emptyList()
        private set

    var listener: Listener? = null

    interface Listener {
        fun onRegistered(deviceId: String)
        fun onRoomCreated(roomId: String, role: String)
        fun onRoomJoined(roomId: String, role: String)
        fun onRoomLeft()
        fun onMembersUpdated(members: List<RoomMember>)
        fun onMemberJoined(member: RoomMember)
        fun onSignalOffer(fromDeviceId: String, sdp: String)
        fun onSignalAnswer(fromDeviceId: String, sdp: String)
        fun onSignalIce(fromDeviceId: String, candidate: JSONObject)
        fun onControlState(deviceId: String, state: String)
        fun onControlRequest(deviceId: String, displayName: String)
        fun onError(code: String, message: String)
    }

    fun handleMessage(json: String) {
        val obj = JSONObject(json)
        when (obj.optString("type")) {
            "hello_ok" -> {
                deviceId = obj.getString("device_id")
                listener?.onRegistered(deviceId)
            }
            "room_created" -> {
                roomId = obj.getString("room_id")
                localRole = obj.getString("role")
                parseMembers(obj)
                listener?.onRoomCreated(roomId, localRole)
            }
            "room_joined" -> {
                roomId = obj.getString("room_id")
                localRole = obj.getString("role")
                parseMembers(obj)
                listener?.onRoomJoined(roomId, localRole)
            }
            "room_left" -> {
                roomId = ""
                localRole = ""
                members = emptyList()
                listener?.onRoomLeft()
            }
            "room_state", "member_joined" -> {
                if (obj.has("members")) {
                    parseMembers(obj)
                } else if (obj.has("member")) {
                    val m = parseMember(obj.getJSONObject("member"))
                    if (members.none { it.deviceId == m.deviceId }) {
                        members = members + m
                    }
                    listener?.onMemberJoined(m)
                }
                listener?.onMembersUpdated(members)
            }
            "member_left" -> {
                val id = obj.getString("device_id")
                members = members.filter { it.deviceId != id }
                listener?.onMembersUpdated(members)
            }
            "signal" -> {
                val from = obj.getString("from_device_id")
                when (obj.getString("signal_type")) {
                    "offer" -> listener?.onSignalOffer(from, obj.getString("sdp"))
                    "answer" -> listener?.onSignalAnswer(from, obj.getString("sdp"))
                    "ice" -> listener?.onSignalIce(from, obj.getJSONObject("candidate"))
                }
            }
            "control_state" -> {
                val id = obj.getString("device_id")
                val state = obj.getString("state")
                members =
                    members.map {
                        if (it.deviceId == id) it.copy(controlState = state) else it
                    }
                listener?.onControlState(id, state)
                listener?.onMembersUpdated(members)
            }
            "control_request" -> {
                listener?.onControlRequest(
                    obj.getString("device_id"),
                    obj.optString("display_name", "Peer"),
                )
            }
            "error" -> listener?.onError(obj.optString("code"), obj.optString("message"))
        }
    }

    private fun parseMembers(obj: JSONObject) {
        val arr = obj.optJSONArray("members") ?: return
        members = buildList {
            for (i in 0 until arr.length()) {
                add(parseMember(arr.getJSONObject(i)))
            }
        }
    }

    private fun parseMember(o: JSONObject): RoomMember =
        RoomMember(
            deviceId = o.getString("device_id"),
            displayName = o.getString("display_name"),
            role = o.getString("role"),
            controlState = o.optString("control_state").ifEmpty { null },
        )
}
