package com.example.soundfield

import com.example.soundfield.audio.PcmChunker
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class PcmChunkerTest {
    @Test
    fun oddTcpBoundariesDoNotChangeSampleBytes() {
        val original = ByteArray(1920) { it.toByte() }
        val output = mutableListOf<ByteArray>()
        val chunker = PcmChunker()
        chunker.accept(original.copyOfRange(0, 481), 481, output::add)
        assertEquals(0, output.size)
        chunker.accept(original.copyOfRange(481, 960), 479, output::add)
        chunker.accept(original.copyOfRange(960, 1920), 960, output::add)
        assertEquals(2, output.size)
        assertArrayEquals(original, output[0] + output[1])
    }
}
