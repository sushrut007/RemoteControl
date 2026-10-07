package com.darpan.remote

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import com.darpan.remote.databinding.ActivityMainBinding
import com.darpan.remote.protocol.ControlProtocol
import com.darpan.remote.session.SessionViewModel
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private val viewModel: SessionViewModel by viewModels()

    private enum class PendingHost {
        NONE,
        SHARE_ANDROID,
    }

    private var pendingHost = PendingHost.NONE
    private var joinPcMode = false

    private val projectionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode != Activity.RESULT_OK || result.data == null) {
                pendingHost = PendingHost.NONE
                Toast.makeText(this, R.string.projection_denied, Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            val pin = binding.inputPinHome.text?.toString()?.trim().orEmpty().ifEmpty { null }
            viewModel.createRoom(pin, Pair(result.resultCode, result.data!!))
            showSessionPanel(asAndroidHost = true)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        binding.btnHostPc.setOnClickListener {
            joinPcMode = true
            binding.layoutJoinCode.visibility = View.VISIBLE
            binding.layoutPinHome.visibility = View.VISIBLE
            binding.btnConfirmJoin.visibility = View.VISIBLE
            binding.btnHostPc.visibility = View.GONE
            binding.btnShareAndroid.visibility = View.GONE
        }

        binding.btnShareAndroid.setOnClickListener {
            pendingHost = PendingHost.SHARE_ANDROID
            binding.layoutPinHome.visibility = View.VISIBLE
            val mgr = getSystemService(MediaProjectionManager::class.java)
            projectionLauncher.launch(mgr.createScreenCaptureIntent())
        }

        binding.btnConfirmJoin.setOnClickListener {
            val code = binding.inputJoinCode.text?.toString()?.trim().orEmpty()
            if (code.length < 4) {
                Toast.makeText(this, R.string.room_code_required, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val pin = binding.inputPinHome.text?.toString()?.trim().orEmpty().ifEmpty { null }
            viewModel.joinRoom(code, pin)
            showSessionPanel(asAndroidHost = false)
        }

        binding.btnLeave.setOnClickListener {
            viewModel.leaveSession()
            showHomePanel()
        }

        binding.btnRequestControl.setOnClickListener { viewModel.requestControl() }

        binding.remoteScreen.onControlPayload = { payload -> viewModel.sendControl(payload) }

        binding.btnKeyboard.setOnClickListener { showKeyboardDialog() }

        viewModel.status.observe(this) { binding.textStatus.text = it }

        viewModel.roomId.observe(this) { id ->
            if (id.isNullOrEmpty()) {
                if (binding.panelSession.visibility == View.VISIBLE) {
                    showHomePanel()
                }
                binding.privacyBanner.visibility = View.GONE
                return@observe
            }
            binding.sessionToolbar.title = getString(R.string.room_title, id)
            binding.privacyBanner.visibility = View.VISIBLE
        }

        viewModel.preview.observe(this) { bitmap ->
            if (bitmap != null) {
                binding.remoteScreen.setImageBitmap(bitmap)
                binding.hostShareHint.visibility = View.GONE
            }
        }

        viewModel.streamSize.observe(this) { (w, h) ->
            binding.remoteScreen.streamWidth = w
            binding.remoteScreen.streamHeight = h
        }

        viewModel.controlGranted.observe(this) { granted ->
            binding.remoteScreen.controlEnabled = granted
            binding.btnRequestControl.isEnabled = !granted
            binding.btnKeyboard.isEnabled = granted
        }

        viewModel.localRole.observe(this) { role ->
            val isHost = role == "host"
            binding.btnRequestControl.visibility = if (isHost) View.GONE else View.VISIBLE
            binding.btnKeyboard.visibility = if (isHost) View.GONE else View.VISIBLE
        }
    }

    override fun onDestroy() {
        if (isFinishing) {
            viewModel.leaveSession()
        }
        super.onDestroy()
    }

    private fun showSessionPanel(asAndroidHost: Boolean) {
        binding.panelHome.visibility = View.GONE
        binding.panelSession.visibility = View.VISIBLE
        binding.hostShareHint.visibility = if (asAndroidHost) View.VISIBLE else View.GONE
        binding.remoteScreen.visibility = if (asAndroidHost) View.GONE else View.VISIBLE
    }

    private fun showHomePanel() {
        binding.panelSession.visibility = View.GONE
        binding.panelHome.visibility = View.VISIBLE
        joinPcMode = false
        pendingHost = PendingHost.NONE
        binding.layoutJoinCode.visibility = View.GONE
        binding.layoutPinHome.visibility = View.GONE
        binding.btnConfirmJoin.visibility = View.GONE
        binding.btnHostPc.visibility = View.VISIBLE
        binding.btnShareAndroid.visibility = View.VISIBLE
        binding.inputJoinCode.text?.clear()
    }

    private fun showKeyboardDialog() {
        if (viewModel.controlGranted.value != true) return
        val keys =
            listOf(
                "Enter" to 0x0D,
                "Esc" to 0x1B,
                "Tab" to 0x09,
                "Back" to 0x08,
                "Del" to 0x2E,
                "Ctrl" to 0x11,
                "Alt" to 0x12,
                "Win" to 0x5B,
            )
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.keyboard)
            .setItems(keys.map { it.first }.toTypedArray()) { _, which ->
                val vk = keys[which].second
                viewModel.sendControl(ControlProtocol.packKey(true, vk, false))
                viewModel.sendControl(ControlProtocol.packKey(false, vk, false))
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
