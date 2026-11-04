package com.example.soundfield.transport

import android.net.Network
import com.example.soundfield.protocol.Command
import com.example.soundfield.protocol.ControlProtocol
import com.example.soundfield.protocol.ControlResponse
import com.example.soundfield.protocol.MAX_CONTROL_BYTES
import com.example.soundfield.protocol.ProtocolException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/** One NDJSON control channel over TCP. */
class ControlClient private constructor(
    private val input: InputStream,
    private val output: OutputStream,
    private val closeTransport: () -> Unit,
    private val scope: CoroutineScope,
    private val onClosed: (String) -> Unit
) {
    private val writeLock = Mutex()
    private val pending = ConcurrentHashMap<Int, CompletableDeferred<ControlResponse>>()
    private val nextId = AtomicInteger(1)
    private val closed = AtomicBoolean(false)
    private val readerJob: Job = scope.launch(Dispatchers.IO) { readResponses() }

    suspend fun request(command: Command, args: JSONObject = JSONObject(), timeoutMs: Long = 3000): JSONObject {
        if (closed.get()) throw IOException("连接已关闭")
        val id = nextId.getAndIncrement()
        val deferred = CompletableDeferred<ControlResponse>()
        pending[id] = deferred
        try {
            val bytes = ControlProtocol.encode(id, command, args)
            return withTimeout(timeoutMs) {
                writeLock.withLock {
                    if (closed.get()) throw IOException("连接已关闭")
                    withContext(Dispatchers.IO) {
                        output.write(bytes)
                        output.flush()
                    }
                }
                val response = deferred.await()
                response.error?.let { throw it }
                response.data
            }
        } catch (e: TimeoutCancellationException) {
            throw IOException("${command.name} 响应超时", e)
        } finally {
            pending.remove(id)
        }
    }

    fun close(reason: String = "连接已断开") {
        if (!closed.compareAndSet(false, true)) return
        try { closeTransport() } catch (_: IOException) { }
        readerJob.cancel()
        pending.values.forEach { it.completeExceptionally(IOException(reason)) }
        pending.clear()
        onClosed(reason)
    }

    private fun readResponses() {
        val buffer = ByteArray(1024)
        val frame = ByteArrayOutputStream()
        try {
            while (!closed.get()) {
                val count = input.read(buffer)
                if (count < 0) throw IOException("设备已断开连接")
                for (index in 0 until count) {
                    val byte = buffer[index]
                    if (byte == '\n'.code.toByte()) {
                        var payload = frame.toByteArray()
                        frame.reset()
                        if (payload.isNotEmpty() && payload.last() == '\r'.code.toByte()) payload = payload.copyOf(payload.size - 1)
                        val response = ControlProtocol.decode(payload)
                        pending.remove(response.id)?.complete(response)
                    } else {
                        if (frame.size() >= MAX_CONTROL_BYTES) throw ProtocolException("控制消息超过 2048 字节")
                        frame.write(byte.toInt())
                    }
                }
            }
        } catch (e: Exception) {
            if (!closed.get()) close(e.message ?: "控制连接异常")
        }
    }

    companion object {
        suspend fun connect(
            host: String,
            port: Int,
            network: Network?,
            scope: CoroutineScope,
            onClosed: (String) -> Unit
        ): ControlClient = withContext(Dispatchers.IO) {
            val socket = network?.socketFactory?.createSocket() ?: Socket()
            try {
                socket.tcpNoDelay = true
                socket.connect(InetSocketAddress(host, port), 4000)
                ControlClient(socket.getInputStream().buffered(), socket.getOutputStream(), { socket.close() }, scope, onClosed)
            } catch (e: Exception) {
                try { socket.close() } catch (_: IOException) { }
                throw e
            }
        }
    }
}
