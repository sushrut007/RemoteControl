package com.darpan.host

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.recyclerview.widget.LinearLayoutManager
import com.darpan.host.databinding.ActivitySessionBinding
import kotlinx.coroutines.launch

class SessionActivity : AppCompatActivity() {

    private lateinit var binding: ActivitySessionBinding
    private lateinit var prefs: HostPreferences
    private lateinit var sessionManager: HostSessionManager
    private lateinit var peerAdapter: PeerAdapter
    private var appRole: String = "host"
    private var videoDecoder: VideoPacketDecoder? = null

    private val accessibilityReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            sessionManager.setAccessibilityReady(RemoteControlAccessibilityService.isEnabled())
            refreshAccessibilityStatus()
        }
    }

    private val projectionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { result ->
        if (result.resultCode != RESULT_OK || result.data == null) {
            Toast.makeText(this, R.string.screen_capture_denied, Toast.LENGTH_SHORT).show()
            return@registerForActivityResult
        }
        MediaProjectionHolder.set(result.resultCode, result.data!!)
        ScreenCaptureService.start(
            context = this,
            settings = StreamSettings(highQualityOnWifi = prefs.highQualityOnWifi),
        )
    }

    private val notificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { _ -> requestScreenCapture() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySessionBinding.inflate(layoutInflater)
        setContentView(binding.root)

        prefs = DarpanHostApp.instance.preferences
        sessionManager = DarpanHostApp.instance.sessionManager
        peerAdapter = PeerAdapter { peer -> confirmKick(peer) }

        appRole = intent.getStringExtra(EXTRA_ROLE) ?: "host"
        val serverUrl = intent.getStringExtra(EXTRA_SERVER_URL).orEmpty()
        val roomId = intent.getStringExtra(EXTRA_ROOM_ID).orEmpty()
        val displayName = intent.getStringExtra(EXTRA_DISPLAY_NAME).orEmpty()

        binding.roomCodeText.text = roomId
        binding.peerList.layoutManager = LinearLayoutManager(this)
        binding.peerList.adapter = peerAdapter

        setupRoleUi()
        wireControls()

        lifecycleScope.launch {
            val result = sessionManager.joinRoom(
                serverUrl = serverUrl,
                room = roomId,
                displayName = displayName,
                peerId = prefs.peerIdForRoom(roomId),
                role = appRole,
            )
            if (result.isFailure) {
                Toast.makeText(
                    this@SessionActivity,
                    result.exceptionOrNull()?.message ?: getString(R.string.join_failed),
                    Toast.LENGTH_LONG,
                ).show()
            }
        }

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                sessionManager.uiState.collect { state -> renderState(state) }
            }
        }
    }

    private fun setupRoleUi() {
        val roleLabel = when (appRole) {
            "host" -> getString(R.string.role_host_title)
            "viewer" -> getString(R.string.role_viewer_title)
            else -> getString(R.string.role_controller_title)
        }
        binding.sessionTitle.text = roleLabel

        binding.hostPanel.isVisible = appRole == "host"
        binding.remotePanel.isVisible = appRole == "viewer" || appRole == "controller"
        binding.touchPad.isVisible = appRole == "controller"

        if (appRole == "viewer" || appRole == "controller") {
            binding.videoSurface.holder.addCallback(object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    videoDecoder = VideoPacketDecoder(holder.surface)
                    sessionManager.onVideoPacket = { b64, kf ->
                        videoDecoder?.decode(b64, kf)
                    }
                }

                override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
                override fun surfaceDestroyed(holder: SurfaceHolder) {
                    videoDecoder?.release()
                    videoDecoder = null
                    sessionManager.onVideoPacket = null
                }
            })
        }

        if (appRole == "controller") {
            binding.touchPad.onTouchEventNormalized = { x, y, type, button ->
                ControlSender.mouse(
                    signaling = DarpanHostApp.signalingClient,
                    localPeerId = sessionManager.uiState.value.localPeerId,
                    x = x,
                    y = y,
                    eventType = type,
                    button = button,
                )
            }
        }
    }

    private fun wireControls() {
        binding.copyRoomButton.setOnClickListener { copyRoomCode() }
        binding.stopShareButton.setOnClickListener { ScreenCaptureService.stop(this) }
        binding.endSessionButton.setOnClickListener { endSession() }
        binding.startShareButton.setOnClickListener { beginShareFlow() }
        binding.controlToggle.setOnCheckedChangeListener { _, checked ->
            sessionManager.setControlAllowed(checked)
        }
    }

    private fun renderState(state: SessionUiState) {
        binding.sessionSubtitle.text = getString(
            R.string.session_subtitle,
            state.displayName,
            state.localPeerId.take(8),
        )
        binding.statusText.text = state.statusMessage
        binding.logText.text = state.logLines.joinToString("\n")

        if (appRole == "host") {
            peerAdapter.submit(state.peers)
            binding.startShareButton.isEnabled = state.state == SessionState.Connected && !state.isSharing
            binding.stopShareButton.isEnabled = state.isSharing
            if (binding.controlToggle.isChecked != state.controlAllowed) {
                binding.controlToggle.isChecked = state.controlAllowed
            }
            refreshAccessibilityStatus()
        }

        if (appRole == "viewer" || appRole == "controller") {
            binding.waitOverlay.isVisible = state.waitingForStream
        }
    }

    override fun onStart() {
        super.onStart()
        if (appRole == "host") {
            LocalBroadcastManager.getInstance(this).registerReceiver(
                accessibilityReceiver,
                IntentFilter(RemoteControlAccessibilityService.ACTION_ACCESSIBILITY_READY),
            )
            sessionManager.setAccessibilityReady(RemoteControlAccessibilityService.isEnabled())
            refreshAccessibilityStatus()
        }
    }

    override fun onStop() {
        if (appRole == "host") {
            LocalBroadcastManager.getInstance(this).unregisterReceiver(accessibilityReceiver)
        }
        super.onStop()
    }

    override fun onDestroy() {
        videoDecoder?.release()
        super.onDestroy()
    }

    private fun beginShareFlow() {
        if (!RemoteControlAccessibilityService.isEnabled()) {
            Toast.makeText(this, R.string.enable_accessibility_first, Toast.LENGTH_LONG).show()
            startActivity(Intent(this, AccessibilitySetupActivity::class.java))
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notificationPermission.launch(android.Manifest.permission.POST_NOTIFICATIONS)
        } else {
            requestScreenCapture()
        }
    }

    private fun requestScreenCapture() {
        val mgr = getSystemService(MediaProjectionManager::class.java)
        projectionLauncher.launch(mgr.createScreenCaptureIntent())
    }

    private fun endSession() {
        if (appRole == "host") ScreenCaptureService.stop(this)
        lifecycleScope.launch {
            sessionManager.leaveRoom()
            finish()
        }
    }

    private fun copyRoomCode() {
        val room = sessionManager.uiState.value.roomId
        getSystemService(ClipboardManager::class.java)
            .setPrimaryClip(ClipData.newPlainText("room", room))
        Toast.makeText(this, R.string.room_code_copied, Toast.LENGTH_SHORT).show()
    }

    private fun confirmKick(peer: PeerInfo) {
        AlertDialog.Builder(this)
            .setTitle(R.string.kick_peer_title)
            .setMessage(getString(R.string.kick_peer_message, peer.displayName))
            .setPositiveButton(R.string.kick) { _, _ -> sessionManager.kickPeer(peer.peerId) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun refreshAccessibilityStatus() {
        val ready = RemoteControlAccessibilityService.isEnabled()
        binding.accessibilityStatus.text = if (ready) {
            getString(R.string.accessibility_ready)
        } else {
            getString(R.string.accessibility_required)
        }
        binding.accessibilityStatus.setTextColor(
            ContextCompat.getColor(
                this,
                if (ready) R.color.darpan_viewer else R.color.darpan_host,
            ),
        )
    }

    companion object {
        const val EXTRA_SERVER_URL = "server_url"
        const val EXTRA_ROOM_ID = "room_id"
        const val EXTRA_DISPLAY_NAME = "display_name"
        const val EXTRA_ROLE = "role"
    }
}
