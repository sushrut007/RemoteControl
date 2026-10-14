package com.darpan.remote

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.view.LayoutInflater
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import com.darpan.remote.databinding.ActivityMainBinding
import com.darpan.remote.databinding.ItemActiveTransferBinding
import com.darpan.remote.databinding.ItemMemberBinding
import com.darpan.remote.databinding.ItemRecentRoomBinding
import com.darpan.remote.databinding.ItemTransferHistoryBinding
import com.darpan.remote.files.FileTransferManager
import com.darpan.remote.protocol.ControlProtocol
import com.darpan.remote.session.IncomingOfferEvent
import com.darpan.remote.session.SessionViewModel
import com.darpan.remote.session.TransferRow
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.tabs.TabLayout
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private val viewModel: SessionViewModel by viewModels()

    private var participantsExpanded = false

    private val projectionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode != Activity.RESULT_OK || result.data == null) {
                Toast.makeText(this, R.string.projection_denied, Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            val pin = binding.inputHostPin.text?.toString()?.trim().orEmpty().ifEmpty { null }
            viewModel.createRoom(pin, Pair(result.resultCode, result.data!!))
            showSessionPanel(asAndroidHost = true)
        }

    private val filePickerLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
            uri ?: return@registerForActivityResult
            sendFileFromUri(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupHomeNavigation()
        setupSessionControls()
        observeViewModel()
    }

    private fun setupHomeNavigation() {
        binding.btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        binding.tabLayoutHome.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab?) {
                when (tab?.position) {
                    0 -> {
                        binding.pageHost.visibility = View.VISIBLE
                        binding.pageJoin.visibility = View.GONE
                    }
                    1 -> {
                        binding.pageHost.visibility = View.GONE
                        binding.pageJoin.visibility = View.VISIBLE
                    }
                }
            }

            override fun onTabUnselected(tab: TabLayout.Tab?) {}
            override fun onTabReselected(tab: TabLayout.Tab?) {}
        })

        // Host page
        binding.btnCreateRoom.setOnClickListener {
            val mgr = getSystemService(MediaProjectionManager::class.java)
            projectionLauncher.launch(mgr.createScreenCaptureIntent())
        }

        // Join page
        binding.btnJoinRoom.setOnClickListener {
            val code = binding.inputJoinCode.text?.toString()?.trim().orEmpty()
            if (code.length < 4) {
                Toast.makeText(this, R.string.room_code_required, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val pin = binding.inputJoinPin.text?.toString()?.trim().orEmpty().ifEmpty { null }
            viewModel.joinRoom(code, pin)
            showSessionPanel(asAndroidHost = false)
        }
    }

    private fun setupSessionControls() {
        binding.btnCopyRoomCode.setOnClickListener {
            val code = binding.textSessionRoomCode.text.toString()
            val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(ClipData.newPlainText("Room Code", code))
            Toast.makeText(this, R.string.room_code_copied, Toast.LENGTH_SHORT).show()
        }

        binding.btnLeaveTop.setOnClickListener {
            viewModel.leaveSession()
            showHomePanel()
        }

        binding.btnToggleHostScreen.setOnClickListener {
            viewModel.leaveSession()
            showHomePanel()
        }

        binding.layoutParticipantsToggle.setOnClickListener {
            participantsExpanded = !participantsExpanded
            binding.layoutParticipantsList.visibility = if (participantsExpanded) View.VISIBLE else View.GONE
            binding.textParticipantsChevron.text = if (participantsExpanded) "▲" else "▼"
        }

        binding.tabLayoutSession.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab?) {
                when (tab?.position) {
                    0 -> {
                        binding.containerScreen.visibility = View.VISIBLE
                        binding.containerFileTransfers.visibility = View.GONE
                    }
                    1 -> {
                        binding.containerScreen.visibility = View.GONE
                        binding.containerFileTransfers.visibility = View.VISIBLE
                    }
                }
            }

            override fun onTabUnselected(tab: TabLayout.Tab?) {}
            override fun onTabReselected(tab: TabLayout.Tab?) {}
        })

        binding.btnRequestControl.setOnClickListener { viewModel.requestControl() }
        binding.btnKeyboard.setOnClickListener { showKeyboardDialog() }

        binding.remoteScreen.onControlPayload = { payload -> viewModel.sendControl(payload) }

        binding.btnSendFileTab.setOnClickListener { filePickerLauncher.launch("*/*") }
        binding.btnQuickSendFile.setOnClickListener { filePickerLauncher.launch("*/*") }

        binding.btnReconnectSignal.setOnClickListener { viewModel.reconnectSignaling() }
        binding.btnReconnectVideo.setOnClickListener { viewModel.reconnectPeerVideo() }
    }

    private fun observeViewModel() {
        viewModel.status.observe(this) { status ->
            binding.textHomeStatus.text = status
            binding.textSessionConnection.text = "Status: $status"
        }

        viewModel.roomId.observe(this) { id ->
            if (id.isNullOrEmpty()) {
                if (binding.panelSession.visibility == View.VISIBLE) {
                    showHomePanel()
                }
                binding.privacyBanner.visibility = View.GONE
                return@observe
            }
            binding.textSessionRoomCode.text = id
            binding.privacyBanner.visibility = View.VISIBLE
            showSessionPanel(viewModel.localRole.value == "host")
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
            binding.btnRequestControl.text =
                if (granted) getString(R.string.control_active) else getString(R.string.request_control)
            binding.btnKeyboard.isEnabled = granted
        }

        viewModel.localRole.observe(this) { role ->
            val isHost = role == "host"
            binding.btnRequestControl.visibility = if (isHost) View.GONE else View.VISIBLE
            binding.btnKeyboard.visibility = if (isHost) View.GONE else View.VISIBLE
            binding.btnToggleHostScreen.visibility = if (isHost) View.VISIBLE else View.GONE
            binding.hostShareHint.visibility = if (isHost) View.VISIBLE else View.GONE
            binding.remoteScreen.visibility = if (isHost) View.GONE else View.VISIBLE
        }

        viewModel.members.observe(this) { members ->
            binding.textParticipantsCount.text = getString(R.string.participants) + " (${members.size})"
            binding.layoutParticipantsList.removeAllViews()
            val selfId = viewModel.deviceId()
            for (m in members) {
                val rowBinding = ItemMemberBinding.inflate(layoutInflater, binding.layoutParticipantsList, false)
                val isSelf = m.deviceId == selfId
                val control = when {
                    m.role == "host" -> getString(R.string.host_role)
                    m.controlState == "control_granted" -> getString(R.string.control_granted)
                    else -> getString(R.string.view_only)
                }
                rowBinding.textMemberName.text = "${m.displayName} (${m.role})"
                rowBinding.textMemberStatus.text = control

                if (isSelf) {
                    rowBinding.textMemberName.setTextColor(getColor(R.color.darpan_accent))
                    rowBinding.dotStatus.setBackgroundColor(getColor(R.color.darpan_accent))
                } else {
                    rowBinding.textMemberName.setTextColor(getColor(R.color.darpan_text))
                    rowBinding.dotStatus.setBackgroundColor(getColor(R.color.darpan_muted))
                }
                binding.layoutParticipantsList.addView(rowBinding.root)
            }
        }

        viewModel.recentRooms.observe(this) { recents ->
            binding.layoutRecentRooms.removeAllViews()
            if (recents.isEmpty()) {
                binding.textNoRecentRooms.visibility = View.VISIBLE
            } else {
                binding.textNoRecentRooms.visibility = View.GONE
                val df = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault())
                for (entry in recents) {
                    val row = ItemRecentRoomBinding.inflate(layoutInflater, binding.layoutRecentRooms, false)
                    row.textRoomCode.text = entry.roomId
                    row.textRoomDate.text = df.format(Date(entry.lastUsedEpochMs))
                    row.btnRejoin.text = if (entry.pinRequired) getString(R.string.join_ellipsis) else getString(R.string.rejoin)
                    row.btnRejoin.setOnClickListener {
                        if (entry.pinRequired) {
                            binding.inputJoinCode.setText(entry.roomId)
                            binding.tabLayoutHome.getTabAt(1)?.select()
                        } else {
                            viewModel.joinRoom(entry.roomId, null)
                            showSessionPanel(asAndroidHost = false)
                        }
                    }
                    binding.layoutRecentRooms.addView(row.root)
                }
            }
        }

        viewModel.incomingOffer.observe(this) { offer ->
            if (offer != null) {
                showIncomingFileDialog(offer)
            }
        }

        viewModel.activeTransfers.observe(this) { transfers ->
            binding.layoutActiveTransfers.removeAllViews()
            if (transfers.isEmpty()) {
                binding.textNoActiveTransfers.visibility = View.VISIBLE
            } else {
                binding.textNoActiveTransfers.visibility = View.GONE
                for (t in transfers) {
                    val row = ItemActiveTransferBinding.inflate(layoutInflater, binding.layoutActiveTransfers, false)
                    val dir = if (t.isOutgoing) "Sending · " else "Receiving · "
                    row.textFileName.text = "$dir${t.name}"
                    row.textTransferStatus.text = t.statusText
                    val pct = if (t.totalBytes > 0) ((t.transferredBytes.toDouble() / t.totalBytes.toDouble()) * 1000).toInt() else 0
                    row.progressBar.progress = pct
                    row.btnCancel.setOnClickListener { viewModel.cancelTransfer(t.id) }
                    binding.layoutActiveTransfers.addView(row.root)
                }
            }
        }

        viewModel.transferHistory.observe(this) { history ->
            binding.layoutTransferHistory.removeAllViews()
            if (history.isEmpty()) {
                binding.textNoHistory.visibility = View.VISIBLE
            } else {
                binding.textNoHistory.visibility = View.GONE
                for (h in history) {
                    val row = ItemTransferHistoryBinding.inflate(layoutInflater, binding.layoutTransferHistory, false)
                    val dir = if (h.isOutgoing) "Sent" else "Received"
                    val status = if (h.success) "Done: ${h.message}" else "Failed: ${h.message}"
                    row.textHistoryLine.text = "$dir: ${h.name} — $status"
                    row.textHistoryLine.setTextColor(
                        getColor(if (h.success) R.color.darpan_accent else R.color.darpan_danger)
                    )
                    binding.layoutTransferHistory.addView(row.root)
                }
            }
        }
    }

    private fun showIncomingFileDialog(offer: IncomingOfferEvent) {
        val sizeFormatted = FileTransferManager.formatBytes(offer.size)
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.incoming_file_title)
            .setMessage(getString(R.string.incoming_file_msg, offer.fileName, sizeFormatted))
            .setCancelable(false)
            .setPositiveButton(R.string.accept) { _, _ ->
                val target = createDownloadTarget(this, offer.fileName)
                if (target != null) {
                    val (outStream, displayPath) = target
                    viewModel.acceptIncomingFile(offer.transferId, outStream)
                    binding.tabLayoutSession.getTabAt(1)?.select()
                    Toast.makeText(this, getString(R.string.file_saved_toast, displayPath), Toast.LENGTH_SHORT).show()
                } else {
                    viewModel.rejectIncomingFile(offer.transferId)
                    Toast.makeText(this, "Cannot write file", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.decline) { _, _ ->
                viewModel.rejectIncomingFile(offer.transferId)
            }
            .show()
    }

    private fun sendFileFromUri(uri: Uri) {
        try {
            var fileName = "file"
            var fileSize = 0L
            contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
                if (cursor.moveToFirst()) {
                    if (nameIndex != -1) fileName = cursor.getString(nameIndex)
                    if (sizeIndex != -1) fileSize = cursor.getLong(sizeIndex)
                }
            }
            val input = contentResolver.openInputStream(uri)
            if (input != null) {
                viewModel.sendFile(fileName, fileSize, input)
                binding.tabLayoutSession.getTabAt(1)?.select()
            }
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to send file: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun createDownloadTarget(context: Context, fileName: String): Pair<OutputStream, String>? {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val resolver = context.contentResolver
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
                    put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
                }
                val uri = resolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                if (uri != null) {
                    Pair(resolver.openOutputStream(uri)!!, "Downloads/$fileName")
                } else {
                    val file = File(context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), fileName)
                    Pair(FileOutputStream(file), file.name)
                }
            } else {
                @Suppress("DEPRECATION")
                val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                dir.mkdirs()
                val file = File(dir, fileName)
                Pair(FileOutputStream(file), file.absolutePath)
            }
        } catch (_: Exception) {
            try {
                val file = File(context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), fileName)
                Pair(FileOutputStream(file), file.name)
            } catch (_: Exception) {
                null
            }
        }
    }

    private fun showSessionPanel(asAndroidHost: Boolean) {
        binding.panelHome.visibility = View.GONE
        binding.panelSession.visibility = View.VISIBLE
        binding.hostShareHint.visibility = if (asAndroidHost) View.VISIBLE else View.GONE
        binding.remoteScreen.visibility = if (asAndroidHost) View.GONE else View.VISIBLE
        binding.tabLayoutSession.getTabAt(0)?.select()
    }

    private fun showHomePanel() {
        binding.panelSession.visibility = View.GONE
        binding.panelHome.visibility = View.VISIBLE
        binding.inputJoinCode.text?.clear()
        binding.inputJoinPin.text?.clear()
        binding.inputHostPin.text?.clear()
        viewModel.refreshRecentRooms()
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

    override fun onDestroy() {
        if (isFinishing) {
            viewModel.leaveSession()
        }
        super.onDestroy()
    }
}
