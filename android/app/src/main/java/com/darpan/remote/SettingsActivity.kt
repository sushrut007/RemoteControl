package com.darpan.remote

import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.darpan.remote.databinding.ActivitySettingsBinding
import com.darpan.remote.settings.SettingsRepository

class SettingsActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySettingsBinding
    private lateinit var settings: SettingsRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)
        settings = SettingsRepository(this)

        binding.inputDeviceName.setText(settings.deviceName)
        binding.switchPin.isChecked = settings.signalingPinEnabled
        binding.inputPinSha.setText(settings.signalingPinSha256)

        binding.inputSignalingUrl.setText(settings.signalingUrl)
        binding.inputStun.setText(settings.stunServer)
        binding.inputTurn.setText(settings.turnServer)
        binding.inputTurnUser.setText(settings.turnUsername)
        binding.inputTurnPass.setText(settings.turnPassword)

        binding.textAdvancedToggle.setOnClickListener {
            val visible = binding.layoutAdvanced.visibility == View.VISIBLE
            binding.layoutAdvanced.visibility = if (visible) View.GONE else View.VISIBLE
            binding.textAdvancedToggle.text = if (visible) {
                "+ Advanced Network Overrides"
            } else {
                "- Hide Advanced Overrides"
            }
        }

        binding.btnSave.setOnClickListener {
            settings.deviceName = binding.inputDeviceName.text?.toString()?.trim().orEmpty()
            settings.signalingPinEnabled = binding.switchPin.isChecked
            settings.signalingPinSha256 = binding.inputPinSha.text?.toString()?.trim().orEmpty()

            val signalOverride = binding.inputSignalingUrl.text?.toString()?.trim().orEmpty()
            if (signalOverride.isNotEmpty()) {
                settings.signalingUrl = signalOverride
            }
            val stunOverride = binding.inputStun.text?.toString()?.trim().orEmpty()
            if (stunOverride.isNotEmpty()) {
                settings.stunServer = stunOverride
            }
            val turnOverride = binding.inputTurn.text?.toString()?.trim().orEmpty()
            if (turnOverride.isNotEmpty()) {
                settings.turnServer = turnOverride
            }
            settings.turnUsername = binding.inputTurnUser.text?.toString()?.trim().orEmpty()
            settings.turnPassword = binding.inputTurnPass.text?.toString()?.trim().orEmpty()

            Toast.makeText(this, R.string.settings_saved, Toast.LENGTH_SHORT).show()
            finish()
        }
    }
}
