package com.darpan.host

import android.os.Handler
import android.os.Looper
import android.util.Log
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap

class ControlEventProcessor {
    private val peers = ConcurrentHashMap<String, String>()
    private var localPeerId: String = ""
    private var deviceControlEnabled = false
    private val queue = ArrayDeque<Runnable>()
    private val queueLock = Any()
    private val lastMouseMoveBySender = ConcurrentHashMap<String, MouseEventPayload>()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val mouseDrainRunnable = object : Runnable {
        override fun run() {
            drainMouseMoves()
            mainHandler.postDelayed(this, MOUSE_DRAIN_MS)
        }
    }

    fun setLocalPeerId(peerId: String) {
        localPeerId = peerId
    }

    fun setDeviceControlEnabled(enabled: Boolean) {
        deviceControlEnabled = enabled
    }

    fun setKnownPeers(peerList: List<PeerInfo>) {
        peers.clear()
        peerList.forEach { addPeer(it) }
    }

    fun addPeer(peer: PeerInfo) {
        peers[peer.peerId] = peer.appType
    }

    fun removePeer(peerId: String) {
        peers.remove(peerId)
        lastMouseMoveBySender.remove(peerId)
    }

    fun reset() {
        stopMouseDrainTimer()
        peers.clear()
        lastMouseMoveBySender.clear()
        synchronized(queueLock) { queue.clear() }
        deviceControlEnabled = false
    }

    fun startMouseDrainTimer() {
        mainHandler.removeCallbacks(mouseDrainRunnable)
        mainHandler.postDelayed(mouseDrainRunnable, MOUSE_DRAIN_MS)
    }

    fun stopMouseDrainTimer() {
        mainHandler.removeCallbacks(mouseDrainRunnable)
    }

    fun handleMouse(payload: JSONObject) {
        if (!validateSender(payload)) return
        val ev = MouseEventPayload(
            senderId = resolveSenderId(payload),
            x = payload.optDouble("x", 0.0).toFloat(),
            y = payload.optDouble("y", 0.0).toFloat(),
            deltaX = payload.optDouble("deltaX", 0.0).toFloat(),
            deltaY = payload.optDouble("deltaY", 0.0).toFloat(),
            eventType = payload.optString("eventType", "mousemove"),
            button = payload.optString("button", "left"),
        )
        if (ev.eventType == "mousemove") {
            lastMouseMoveBySender[ev.senderId] = ev
            return
        }
        enqueue {
            flushMouseMove(ev.senderId)
            accessibility()?.handleMouse(ev)
        }
    }

    fun handleKeyboard(payload: JSONObject) {
        if (!validateSender(payload)) return
        val mods = mutableListOf<String>()
        payload.optJSONArray("modifiers")?.let { arr ->
            for (i in 0 until arr.length()) mods += arr.optString(i)
        }
        val ev = KeyboardEventPayload(
            senderId = resolveSenderId(payload),
            key = payload.optString("key"),
            eventType = payload.optString("eventType", "keydown"),
            modifiers = mods,
        )
        enqueue { accessibility()?.handleKeyboard(ev) }
    }

    fun handleControl(payload: JSONObject) {
        if (!validateSender(payload)) return
        val command = payload.optString("command", payload.optString("type", "unknown"))
        enqueue {
            when (command.lowercase()) {
                "back" -> accessibility()?.performBack()
                "home" -> accessibility()?.performHome()
                "recents" -> accessibility()?.performRecents()
                else -> Log.d(TAG, "control command: $command")
            }
        }
    }

    fun drainMouseMoves() {
        if (!deviceControlEnabled) return
        lastMouseMoveBySender.keys.toList().forEach { senderId ->
            flushMouseMove(senderId)
        }
    }

    private fun flushMouseMove(senderId: String) {
        val ev = lastMouseMoveBySender.remove(senderId) ?: return
        accessibility()?.handleMouse(ev)
    }

    private fun validateSender(payload: JSONObject): Boolean {
        if (!deviceControlEnabled) return false
        val senderId = resolveSenderId(payload)
        if (senderId.isBlank() || senderId == localPeerId) return false
        return peers[senderId] == "controller"
    }

    private fun resolveSenderId(payload: JSONObject): String {
        return payload.optString("senderId")
            .ifBlank { payload.optString("fromPeerId") }
    }

    private fun enqueue(action: Runnable) {
        synchronized(queueLock) {
            queue.addLast(action)
        }
        drainQueue()
    }

    private fun drainQueue() {
        val batch = ArrayList<Runnable>()
        synchronized(queueLock) {
            while (queue.isNotEmpty()) batch.add(queue.removeFirst())
        }
        batch.forEach { run ->
            try {
                run.run()
            } catch (ex: Exception) {
                Log.e(TAG, "control dispatch failed", ex)
            }
        }
    }

    companion object {
        private const val TAG = "ControlEventProcessor"
        private const val MOUSE_DRAIN_MS = 16L
        private fun accessibility() = RemoteControlAccessibilityService.getInstance()
    }
}
