package com.example.soundfield.recordings

import android.content.Context
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.util.UUID

/** Writes received 48 kHz mono S16LE PCM to a phone file without buffering the recording in RAM. */
class LocalWavRecorder private constructor(
    val file: File,
    val filename: String,
    private val output: RandomAccessFile
) {
    private var dataBytes = 0L
    private var closed = false

    @Synchronized
    fun append(block: ByteArray) {
        if (closed) return
        if (block.size % 2 != 0) throw IOException("PCM 数据未按 16 位样本对齐")
        if (dataBytes + block.size > MAX_PCM_BYTES) throw IOException("手机 WAV 已达到 2 GB 上限")
        output.write(block)
        dataBytes += block.size
    }

    @Synchronized
    fun finish(): File {
        if (closed) throw IOException("录音文件已经关闭")
        if (dataBytes == 0L) {
            discard()
            throw IOException("未收到 PCM 音频，未生成空 WAV")
        }
        try {
            output.seek(0)
            output.write(header(dataBytes.toInt()))
            output.fd.sync()
            output.close()
            closed = true
            return file
        } catch (e: Exception) {
            discard()
            throw e
        }
    }

    @Synchronized
    fun discard() {
        if (!closed) {
            closed = true
            try { output.close() } catch (_: IOException) { }
        }
        file.delete()
    }

    companion object {
        private const val MAX_PCM_BYTES = 1_999_999_956L // Keeps the complete WAV below 2 GB.

        fun suggestedFilename(): String =
            "SoundField_${System.currentTimeMillis()}_${UUID.randomUUID().toString().take(8)}.wav"

        fun create(context: Context, filename: String = suggestedFilename()): LocalWavRecorder {
            val directory = File(context.filesDir, "local_recordings")
            if (!directory.isDirectory && !directory.mkdirs()) throw IOException("无法创建手机录音目录")
            val file = File(directory, filename)
            val output = RandomAccessFile(file, "rw")
            try {
                output.write(ByteArray(44))
                return LocalWavRecorder(file, filename, output)
            } catch (e: Exception) {
                output.close()
                file.delete()
                throw e
            }
        }

        private fun header(dataBytes: Int): ByteArray = ByteArray(44).apply {
            fun ascii(offset: Int, value: String) = value.toByteArray(Charsets.US_ASCII).copyInto(this, offset)
            fun u16(offset: Int, value: Int) {
                this[offset] = value.toByte()
                this[offset + 1] = (value ushr 8).toByte()
            }
            fun u32(offset: Int, value: Int) {
                u16(offset, value)
                u16(offset + 2, value ushr 16)
            }
            ascii(0, "RIFF")
            u32(4, dataBytes + 36)
            ascii(8, "WAVE")
            ascii(12, "fmt ")
            u32(16, 16)
            u16(20, 1)
            u16(22, 1)
            u32(24, 48000)
            u32(28, 96000)
            u16(32, 2)
            u16(34, 16)
            ascii(36, "data")
            u32(40, dataBytes)
        }
    }
}
