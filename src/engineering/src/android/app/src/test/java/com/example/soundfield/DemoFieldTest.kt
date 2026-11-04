package com.example.soundfield

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DemoFieldTest {
    @Test
    fun demoFrameRespectsDisplayDataContract() {
        for (tick in listOf(0L, 1L, 100L, 10_000L)) {
            val frame = DemoField.frame(tick, 359)
            assertTrue(frame.simulated)
            assertEquals(72, frame.spectrum.size)
            assertTrue(frame.spectrum.all { it in 0..1000 })
            assertEquals(2, frame.sources.size)
            assertTrue(frame.sources.all { it.angleDeg in 0..359 && it.strength in 0..1000 })
            assertEquals(359, frame.targetAngleDeg)
        }
    }
}
