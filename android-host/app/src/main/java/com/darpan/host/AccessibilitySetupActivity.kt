package com.darpan.host

import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.darpan.host.databinding.ActivityAccessibilitySetupBinding

class AccessibilitySetupActivity : AppCompatActivity() {

    private lateinit var binding: ActivityAccessibilitySetupBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityAccessibilitySetupBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.openSettingsButton.setOnClickListener {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            Toast.makeText(this, R.string.accessibility_settings_hint, Toast.LENGTH_LONG).show()
        }
        binding.doneButton.setOnClickListener { finish() }
    }

    override fun onResume() {
        super.onResume()
        binding.setupStatus.text = if (RemoteControlAccessibilityService.isEnabled()) {
            getString(R.string.accessibility_ready)
        } else {
            getString(R.string.accessibility_required)
        }
    }
}
