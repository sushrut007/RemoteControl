package com.darpan.host

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.darpan.host.databinding.ActivityPermissionsInfoBinding

class PermissionsInfoActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val binding = ActivityPermissionsInfoBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.closeButton.setOnClickListener { finish() }
    }
}
