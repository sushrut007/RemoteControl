package com.darpan.host

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.util.Base64
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.lifecycle.LifecycleService

class ScreenCaptureService : LifecycleService() {

    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var encoder: MediaCodec? = null
    private var encodeThread: HandlerThread? = null
    private var encodeHandler: Handler? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    private var running = false

    override fun onBind(intent: Intent): IBinder? = super.onBind(intent)

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        val cmd = intent ?: return START_NOT_STICKY

        if (cmd.action == ACTION_STOP) {
            stopCaptureInternal()
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }

        // Android 14+: foreground + type must be active BEFORE MediaProjection.
        ensureForeground()

        val width = cmd.getIntExtra(EXTRA_WIDTH, 1280).alignEven().alignDown(16)
        val height = cmd.getIntExtra(EXTRA_HEIGHT, 720).alignEven().alignDown(16)
        val fps = cmd.getIntExtra(EXTRA_FPS, 24).coerceIn(10, 30)
        val bitrateKbps = cmd.getIntExtra(EXTRA_BITRATE_KBPS, 2500)

        val holder = MediaProjectionHolder.consume()
        if (holder == null) {
            Log.e(TAG, "Missing MediaProjection result")
            stopSelf()
            return START_NOT_STICKY
        }

        val (resultCode, resultData) = holder
        if (resultCode != Activity.RESULT_OK) {
            stopSelf()
            return START_NOT_STICKY
        }

        try {
            startCapture(resultCode, resultData, width, height, fps, bitrateKbps)
        } catch (ex: Exception) {
            Log.e(TAG, "startCapture failed", ex)
            DarpanHostApp.instance.sessionManager.onSharingError(ex.message ?: "capture failed")
            stopCaptureInternal()
            stopSelf()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopCaptureInternal()
        super.onDestroy()
    }

    private fun ensureForeground() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ServiceCompat.startForeground(
                this,
                NOTIFICATION_ID,
                buildNotification(getString(R.string.notification_sharing)),
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION,
            )
        } else {
            startForeground(NOTIFICATION_ID, buildNotification(getString(R.string.notification_sharing)))
        }
    }

    private fun startCapture(
        resultCode: Int,
        resultData: Intent,
        width: Int,
        height: Int,
        fps: Int,
        bitrateKbps: Int,
    ) {
        if (running) return

        val mgr = getSystemService(MediaProjectionManager::class.java)
            ?: throw IllegalStateException("MediaProjectionManager unavailable")

        val activeProjection = mgr.getMediaProjection(resultCode, resultData)
            ?: throw IllegalStateException("getMediaProjection returned null")

        projection = activeProjection

        encodeThread = HandlerThread("DarpanEncode").also { it.start() }
        encodeHandler = Handler(encodeThread!!.looper)

        activeProjection.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                mainHandler.post { stopCaptureInternal() }
            }
        }, encodeHandler)

        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface,
            )
            setInteger(MediaFormat.KEY_BIT_RATE, bitrateKbps * 1000)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                setInteger(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 1_000_000 / fps)
            }
        }

        val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        val inputSurface = codec.createInputSurface()
        codec.start()
        encoder = codec

        val density = resources.displayMetrics.densityDpi
        virtualDisplay = activeProjection.createVirtualDisplay(
            "DarpanHostCapture",
            width,
            height,
            density,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            inputSurface,
            null,
            encodeHandler,
        ) ?: throw IllegalStateException("createVirtualDisplay failed")

        running = true
        encodeHandler?.post { drainEncoderLoop() }

        mainHandler.post {
            DarpanHostApp.instance.sessionManager.onSharingStarted()
        }
        Log.i(TAG, "Screen capture started ${width}x${height}@${fps}fps")
    }

    private fun drainEncoderLoop() {
        val codec = encoder ?: return
        val bufferInfo = MediaCodec.BufferInfo()
        while (running) {
            val index = codec.dequeueOutputBuffer(bufferInfo, 10_000)
            when {
                index == MediaCodec.INFO_TRY_AGAIN_LATER -> continue
                index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> continue
                index >= 0 -> {
                    val buffer = codec.getOutputBuffer(index)
                    if (buffer != null && bufferInfo.size > 0) {
                        val chunk = ByteArray(bufferInfo.size)
                        buffer.position(bufferInfo.offset)
                        buffer.limit(bufferInfo.offset + bufferInfo.size)
                        buffer.get(chunk)
                        val isKeyframe =
                            (bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                        val base64 = Base64.encodeToString(chunk, Base64.NO_WRAP)
                        mainHandler.post {
                            DarpanHostApp.instance.sessionManager.sendVideoPacket(base64, isKeyframe)
                        }
                    }
                    codec.releaseOutputBuffer(index, false)
                }
            }
        }
    }

    private fun stopCaptureInternal() {
        if (!running && encoder == null && projection == null) return
        running = false

        mainHandler.post {
            if (DarpanHostApp.instance.sessionManager.uiState.value.isSharing) {
                DarpanHostApp.instance.sessionManager.onSharingStopped()
            }
        }

        virtualDisplay?.release()
        virtualDisplay = null

        try {
            encoder?.stop()
        } catch (_: Exception) {
        }
        try {
            encoder?.release()
        } catch (_: Exception) {
        }
        encoder = null

        try {
            projection?.stop()
        } catch (_: Exception) {
        }
        projection = null

        encodeThread?.quitSafely()
        encodeThread = null
        encodeHandler = null
    }

    private fun buildNotification(content: String): Notification {
        val channelId = "darpan_host_capture"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                channelId,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            )
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
        return NotificationCompat.Builder(this, channelId)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(content)
            .setSmallIcon(R.drawable.ic_notification)
            .setOngoing(true)
            .build()
    }

    companion object {
        const val ACTION_STOP = "com.darpan.host.action.STOP_CAPTURE"
        const val EXTRA_WIDTH = "width"
        const val EXTRA_HEIGHT = "height"
        const val EXTRA_FPS = "fps"
        const val EXTRA_BITRATE_KBPS = "bitrate_kbps"

        private const val NOTIFICATION_ID = 1001
        private const val TAG = "ScreenCaptureService"

        fun start(context: Context, settings: StreamSettings) {
            val onWifi = isOnWifi(context)
            val intent = Intent(context, ScreenCaptureService::class.java).apply {
                putExtra(EXTRA_WIDTH, settings.effectiveWidth(onWifi))
                putExtra(EXTRA_HEIGHT, settings.effectiveHeight(onWifi))
                putExtra(EXTRA_FPS, settings.effectiveFps(onWifi))
                putExtra(EXTRA_BITRATE_KBPS, settings.bitrateKbps)
            }
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.startService(
                Intent(context, ScreenCaptureService::class.java).apply {
                    action = ACTION_STOP
                },
            )
        }

        private fun isOnWifi(context: Context): Boolean {
            val cm = context.getSystemService(ConnectivityManager::class.java) ?: return false
            val network = cm.activeNetwork ?: return false
            val caps = cm.getNetworkCapabilities(network) ?: return false
            return caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
        }

        private fun Int.alignEven(): Int = if (this and 1 == 1) this - 1 else this
        private fun Int.alignDown(multiple: Int): Int = (this / multiple) * multiple
    }
}
