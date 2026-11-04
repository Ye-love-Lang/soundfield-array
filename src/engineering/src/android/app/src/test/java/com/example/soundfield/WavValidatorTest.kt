package com.example.soundfield

import com.example.soundfield.recordings.WavValidator
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

class WavValidatorTest {
    private fun wav(sampleRate: Int = 48000): ByteArray = ByteBuffer.allocate(48)
        .order(ByteOrder.LITTLE_ENDIAN)
        .put("RIFF".toByteArray()).putInt(40).put("WAVE".toByteArray())
        .put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(1)
        .putInt(sampleRate).putInt(sampleRate * 2).putShort(2).putShort(16)
        .put("data".toByteArray()).putInt(4).putShort(0).putShort(0).array()

    @Test fun acceptsComplete48kMonoPcmWav() {
        val bytes = wav()
        WavValidator.validate(ByteArrayInputStream(bytes), bytes.size.toLong())
    }

    @Test fun rejectsTruncatedDownload() {
        val bytes = wav().copyOf(44)
        assertThrows(IOException::class.java) {
            WavValidator.validate(ByteArrayInputStream(bytes), bytes.size.toLong())
        }
    }

    @Test fun rejectsUnsupportedSampleRate() {
        val bytes = wav(44100)
        assertThrows(IOException::class.java) {
            WavValidator.validate(ByteArrayInputStream(bytes), bytes.size.toLong())
        }
    }
}
