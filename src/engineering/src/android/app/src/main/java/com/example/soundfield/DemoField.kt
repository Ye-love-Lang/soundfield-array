package com.example.soundfield

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.roundToInt
import kotlin.math.sin

data class SoundSource(val id: Int, val angleDeg: Int, val strength: Int)

data class DemoFrame(
    val spectrum: List<Int>,
    val sources: List<SoundSource>,
    val targetAngleDeg: Int,
    val simulated: Boolean = true
)

/** Local display data only. No values in this class come from the FPGA. */
object DemoField {
    fun frame(tick: Long, targetAngleDeg: Int): DemoFrame {
        val phase = tick * 0.075
        val firstAngle = (48 + 9 * sin(phase)).roundToInt().wrapAngle()
        val secondAngle = (221 + 12 * sin(phase * 0.61 + 1.2)).roundToInt().wrapAngle()
        val sources = listOf(
            SoundSource(0, firstAngle, 850),
            SoundSource(1, secondAngle, 620)
        )
        val spectrum = List(72) { index ->
            val angle = index * 5
            val first = 760.0 * exp(-angularDistance(angle, firstAngle) * angularDistance(angle, firstAngle) / 520.0)
            val second = 510.0 * exp(-angularDistance(angle, secondAngle) * angularDistance(angle, secondAngle) / 780.0)
            val ripple = 35.0 * (1.0 + cos(index * 0.7 + phase))
            (80 + first + second + ripple).roundToInt().coerceIn(0, 1000)
        }
        return DemoFrame(spectrum, sources, targetAngleDeg.wrapAngle())
    }

    private fun angularDistance(a: Int, b: Int): Int {
        val difference = abs(a - b)
        return minOf(difference, 360 - difference)
    }
}

fun Int.wrapAngle(): Int = ((this % 360) + 360) % 360
