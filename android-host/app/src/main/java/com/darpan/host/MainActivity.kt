package com.darpan.host

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.RadioButton
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.darpan.host.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var prefs: HostPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        prefs = DarpanHostApp.instance.preferences
        binding.serverUrlInput.setText(prefs.serverUrl)
        binding.displayNameInput.setText(prefs.displayName)
        binding.roomIdInput.setText(prefs.lastRoomId)
        binding.hqWifiSwitch.isChecked = prefs.highQualityOnWifi
        selectRole(prefs.lastRole)

        binding.roleGroup.setOnCheckedChangeListener { _, _ -> updateRoleUi() }
        updateRoleUi()

        binding.permissionsInfoButton.setOnClickListener {
            startActivity(Intent(this, PermissionsInfoActivity::class.java))
        }
        binding.accessibilityButton.setOnClickListener {
            startActivity(Intent(this, AccessibilitySetupActivity::class.java))
        }
        binding.connectButton.setOnClickListener { connect() }
    }

    private fun updateRoleUi() {
        val role = selectedRole()
        binding.hqWifiSwitch.visibility = if (role == "host") View.VISIBLE else View.GONE
        binding.accessibilityButton.visibility = if (role == "host") View.VISIBLE else View.GONE
    }

    private fun selectRole(role: String) {
        when (role) {
            "host" -> binding.roleHost.isChecked = true
            "viewer" -> binding.roleViewer.isChecked = true
            else -> binding.roleController.isChecked = true
        }
    }

    private fun selectedRole(): String {
        val id = binding.roleGroup.checkedRadioButtonId
        return when (id) {
            binding.roleHost.id -> "host"
            binding.roleViewer.id -> "viewer"
            else -> "controller"
        }
    }

    private fun connect() {
        val serverUrl = binding.serverUrlInput.text?.toString()?.trim().orEmpty()
        val roomId = binding.roomIdInput.text?.toString()?.trim().orEmpty()
        val displayName = binding.displayNameInput.text?.toString()?.trim()
            ?: HostPreferences.DEFAULT_DISPLAY_NAME
        val role = selectedRole()

        if (serverUrl.isEmpty() || roomId.isEmpty()) {
            Toast.makeText(this, R.string.fill_required_fields, Toast.LENGTH_SHORT).show()
            return
        }

        if (role == "host" && !RemoteControlAccessibilityService.isEnabled()) {
            Toast.makeText(this, R.string.enable_accessibility_first, Toast.LENGTH_LONG).show()
            startActivity(Intent(this, AccessibilitySetupActivity::class.java))
            return
        }

        prefs.serverUrl = serverUrl
        prefs.displayName = displayName
        prefs.lastRoomId = roomId
        prefs.lastRole = role
        prefs.highQualityOnWifi = binding.hqWifiSwitch.isChecked

        startActivity(
            Intent(this, SessionActivity::class.java).apply {
                putExtra(SessionActivity.EXTRA_SERVER_URL, serverUrl)
                putExtra(SessionActivity.EXTRA_ROOM_ID, roomId)
                putExtra(SessionActivity.EXTRA_DISPLAY_NAME, displayName)
                putExtra(SessionActivity.EXTRA_ROLE, role)
            },
        )
    }
}
