package com.example.soundfield.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.net.Network
import com.example.soundfield.recordings.LocalWavRecorder
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

data class AudioBufferStats(val queuedMs: Int, val droppedBlocks: Int, val underruns: Int)

/** Receives raw PCM on one socket and writes it to AudioTrack on a separate IO coroutine. */
class PcmStreamPlayer private constructor(
    private val socket: Socket,
    private val track: AudioTrack,
    parentScope: CoroutineScope,
    initialPlaybackEnabled: Boolean,
    initialRecorder: LocalWavRecorder?,
    private val onRecorderError: (Throwable) -> Unit,
    private val onStats: (AudioBufferStats) -> Unit
) {
    private val closed = AtomicBoolean(false)
    private val queue = ArrayBlockingQueue<ByteArray>(50) // 500 ms maximum at 10 ms per block.
    private val droppedBlocks = AtomicInteger(0)
    private val playerScope = CoroutineScope(SupervisorJob(parentScope.coroutineContext[Job]) + Dispatchers.IO)
    @Volatile private var playbackEnabled = initialPlaybackEnabled
    @Volatile private var recorder = initialRecorder
    val failure = CompletableDeferred<Throwable>()

    init {
        playerScope.launch { receive() }
        playerScope.launch { play() }
    }

    private fun receive() {
        val chunker = PcmChunker()
        val buffer = ByteArray(4096)
        var blocks = 0
        try {
            val input = socket.getInputStream()
            while (!closed.get()) {
                val count = input.read(buffer)
                if (count < 0) throw IOException("PCM 音频连接已断开")
                chunker.accept(buffer, count) { block ->
                    recorder?.let { sink ->
                        try { sink.append(block) }
                        catch (e: Exception) {
                            if (recorder === sink) recorder = null
                            onRecorderError(e)
                        }
                    }
                    if (playbackEnabled) {
                        if (!queue.offer(block)) {
                            queue.poll()
                            droppedBlocks.incrementAndGet()
                            queue.offer(block)
                        }
                    }
                    blocks++
                    if (blocks % 25 == 0) reportStats()
                }
            }
        } catch (e: Exception) {
            if (!closed.get()) failure.complete(e)
        }
    }

    private fun play() {
        var started = false
        try {
            while (!closed.get() && playerScope.isActive) {
                if (!playbackEnabled) {
                    if (started) {
                        track.pause()
                        track.flush()
                        queue.clear()
                        started = false
                    }
                    Thread.sleep(20)
                    continue
                }
                // Receive roughly 100 ms before starting playback to absorb network jitter.
                if (!started) {
                    if (queue.size < 10) { Thread.sleep(5); continue }
                    track.play()
                    started = true
                }
                val block = queue.poll(100, TimeUnit.MILLISECONDS) ?: continue
                var offset = 0
                while (offset < block.size && !closed.get()) {
                    val written = track.write(block, offset, block.size - offset, AudioTrack.WRITE_BLOCKING)
                    if (written < 0) throw IOException("AudioTrack 写入失败：$written")
                    if (written == 0) Thread.sleep(1) else offset += written
                }
            }
        } catch (e: Exception) {
            if (!closed.get()) failure.complete(e)
        }
    }

    private fun reportStats() {
        if (!closed.get()) onStats(AudioBufferStats(queue.size * 10, droppedBlocks.get(), track.underrunCount))
    }

    fun setPlaybackEnabled(enabled: Boolean) {
        playbackEnabled = enabled
        if (!enabled) queue.clear()
    }

    fun setRecorder(sink: LocalWavRecorder?) {
        recorder = sink
    }

    fun close() {
        if (!closed.compareAndSet(false, true)) return
        try { socket.close() } catch (_: IOException) { }
        playerScope.cancel()
        try { track.pause() } catch (_: IllegalStateException) { }
        try { track.flush() } catch (_: IllegalStateException) { }
        track.release()
        queue.clear()
    }

    companion object {
        suspend fun connect(
            host: String,
            port: Int,
            network: Network?,
            scope: CoroutineScope,
            playbackEnabled: Boolean,
            recorder: LocalWavRecorder?,
            onRecorderError: (Throwable) -> Unit,
            onStats: (AudioBufferStats) -> Unit
        ): PcmStreamPlayer = withContext(Dispatchers.IO) {
            val socket = network?.socketFactory?.createSocket() ?: Socket()
            var track: AudioTrack? = null
            try {
                socket.tcpNoDelay = true
                socket.soTimeout = 5000
                socket.connect(InetSocketAddress(host, port), 4000)
                val minimum = AudioTrack.getMinBufferSize(
                    48000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT
                )
                if (minimum <= 0) throw IOException("设备不支持 48 kHz 单声道 PCM 播放")
                track = AudioTrack.Builder()
                    .setAudioAttributes(
                        AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()
                    )
                    .setAudioFormat(
                        AudioFormat.Builder().setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build()
                    )
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(maxOf(minimum * 2, 19200))
                    .build()
                if (track.state != AudioTrack.STATE_INITIALIZED) throw IOException("AudioTrack 初始化失败")
                PcmStreamPlayer(socket, track, scope, playbackEnabled, recorder, onRecorderError, onStats)
            } catch (e: Exception) {
                try { socket.close() } catch (_: IOException) { }
                track?.release()
                throw e
            }
        }
    }
}
