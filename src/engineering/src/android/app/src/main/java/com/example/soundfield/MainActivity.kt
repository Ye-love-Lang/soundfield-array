package com.example.soundfield

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.lifecycle.ViewModelProvider
import com.example.soundfield.ui.SoundFieldApp
import com.example.soundfield.ui.theme.SoundFieldTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
        )
        val viewModel = ViewModelProvider(this)[SoundFieldViewModel::class.java]
        setContent {
            SoundFieldTheme {
                SoundFieldApp(viewModel)
            }
        }
    }
}
