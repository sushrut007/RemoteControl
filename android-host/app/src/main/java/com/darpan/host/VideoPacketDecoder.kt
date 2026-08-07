package com.darpan.host

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface

class VideoPacketDecoder(private val surface: Surface) {

    private var codec: MediaCodec? = null
    private var configured = false
    private var width = 1280
    private var height = 720

    fun decode(base64: String, isKeyframe: Boolean) {
        val data = android.util.Base64.decode(base64, android.util.Base64.NO_WRAP)
        if (data.isEmpty()) return

        if (!configured) {
            if (!isKeyframe) return
            initDecoder()
        }

        val c = codec ?: return
        val index = c.dequeueInputBuffer(10_000)
        if (index >= 0) {
            val buffer = c.getInputBuffer(index) ?: return
            buffer.clear()
            buffer.put(data)
            c.queueInputBuffer(index, 0, data.size, System.nanoTime() / 1000, 0)
        }

        val info = MediaCodec.BufferInfo()
        var outIndex = c.dequeueOutputBuffer(info, 10_000)
        while (outIndex >= 0) {
            c.releaseOutputBuffer(outIndex, true)
            outIndex = c.dequeueOutputBuffer(info, 0)
        }
    }

    private fun initDecoder() {
        try {
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
            val c = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            c.configure(format, surface, null, 0)
            c.start()
            codec = c
            configured = true
        } catch (ex: Exception) {
            Log.e(TAG, "decoder init failed", ex)
        }
    }

    fun release() {
        try {
            codec?.stop()
        } catch (_: Exception) {
        }
        try {
            codec?.release()
        } catch (_: Exception) {
        }
        codec = null
        configured = false
    }

    companion object {
        private const val TAG = "VideoPacketDecoder"
    }
}
