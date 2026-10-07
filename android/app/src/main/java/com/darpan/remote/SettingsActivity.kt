package com.darpan.remote

import android.os.Bundle
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
        binding.inputSignalingUrl.setText(settings.signalingUrl)
        binding.inputStun.setText(settings.stunServer)
        binding.inputTurn.setText(settings.turnServer)
        binding.inputTurnUser.setText(settings.turnUsername)
        binding.inputTurnPass.setText(settings.turnPassword)
        binding.switchPin.isChecked = settings.signalingPinEnabled
        binding.inputPinSha.setText(settings.signalingPinSha256)

        binding.btnSave.setOnClickListener {
            settings.deviceName = binding.inputDeviceName.text?.toString()?.trim().orEmpty()
            settings.signalingUrl = binding.inputSignalingUrl.text?.toString()?.trim().orEmpty()
            settings.stunServer = binding.inputStun.text?.toString()?.trim().orEmpty()
            settings.turnServer = binding.inputTurn.text?.toString()?.trim().orEmpty()
            settings.turnUsername = binding.inputTurnUser.text?.toString()?.trim().orEmpty()
            settings.turnPassword = binding.inputTurnPass.text?.toString()?.trim().orEmpty()
            settings.signalingPinEnabled = binding.switchPin.isChecked
            settings.signalingPinSha256 = binding.inputPinSha.text?.toString()?.trim().orEmpty()
            finish()
        }
    }
}
