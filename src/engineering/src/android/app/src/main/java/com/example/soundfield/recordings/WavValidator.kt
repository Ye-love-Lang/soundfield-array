package com.example.soundfield.recordings

import java.io.IOException
import java.io.InputStream

/** Checks the RIFF length and the PCM format before a download is published. */
object WavValidator {
    fun validate(input: InputStream, length: Long) {
        if (length < 44) throw IOException("WAV 文件过短")
        val header = ByteArray(12)
        readFully(input, header)
        if (ascii(header, 0) != "RIFF" || ascii(header, 8) != "WAVE")
            throw IOException("下载内容不是 RIFF/WAVE 文件")
        if (u32(header, 4) + 8 != length) throw IOException("WAV 文件长度与头部不一致")

        var consumed = 12L
        var validFormat = false
        var hasAudio = false
        while (consumed + 8 <= length) {
            val chunk = ByteArray(8)
            readFully(input, chunk)
            consumed += 8
            val size = u32(chunk, 4)
            val paddedSize = size + (size and 1L)
            if (paddedSize > length - consumed) throw IOException("WAV 数据块不完整")
            when (ascii(chunk, 0)) {
                "fmt " -> {
                    if (size < 16) throw IOException("WAV fmt 数据块过短")
                    val format = ByteArray(16)
                    readFully(input, format)
                    if (u16(format, 0) != 1 || u16(format, 2) != 1 ||
                        u32(format, 4) != 48000L || u16(format, 14) != 16)
                        throw IOException("WAV 需要 48 kHz、单声道、16-bit PCM")
                    validFormat = true
                    skipFully(input, paddedSize - 16)
                }
                "data" -> {
                    if (size == 0L || size % 2L != 0L) throw IOException("WAV 音频数据长度无效")
                    hasAudio = true
                    skipFully(input, paddedSize)
                }
                else -> skipFully(input, paddedSize)
            }
            consumed += paddedSize
        }
        if (consumed != length || !validFormat || !hasAudio) throw IOException("WAV 缺少有效的格式或音频数据")
    }

    private fun ascii(bytes: ByteArray, offset: Int): String =
        String(bytes, offset, 4, Charsets.US_ASCII)

    private fun u16(bytes: ByteArray, offset: Int): Int =
        (bytes[offset].toInt() and 255) or ((bytes[offset + 1].toInt() and 255) shl 8)

    private fun u32(bytes: ByteArray, offset: Int): Long =
        (u16(bytes, offset).toLong() and 65535) or (u16(bytes, offset + 2).toLong() shl 16)

    private fun readFully(input: InputStream, bytes: ByteArray) {
        var offset = 0
        while (offset < bytes.size) {
            val count = input.read(bytes, offset, bytes.size - offset)
            if (count < 0) throw IOException("WAV 文件提前结束")
            offset += count
        }
    }

    private fun skipFully(input: InputStream, amount: Long) {
        var remaining = amount
        val buffer = ByteArray(8192)
        while (remaining > 0) {
            val count = input.read(buffer, 0, minOf(remaining, buffer.size.toLong()).toInt())
            if (count < 0) throw IOException("WAV 文件提前结束")
            remaining -= count
        }
    }
}
