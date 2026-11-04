package com.example.soundfield.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val SoundFieldColors = darkColorScheme(
    primary = Cyan,
    secondary = Amber,
    background = Background,
    surface = Panel,
    onBackground = androidx.compose.ui.graphics.Color(0xFFE9F5F7),
    onSurface = androidx.compose.ui.graphics.Color(0xFFE9F5F7)
)

@Composable
fun SoundFieldTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = SoundFieldColors,
        typography = Typography,
        content = content
    )
}
