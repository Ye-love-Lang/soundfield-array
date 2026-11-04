package com.example.soundfield.recordings

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Network
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.example.soundfield.protocol.RecordingEntry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

data class SavedRecording(val filename: String, val uri: String, val sizeBytes: Long)

class RecordingRepository(private val context: Context) {
    private val resolver = context.contentResolver
    private val preferences = context.getSharedPreferences("saved_recordings", Context.MODE_PRIVATE)

    fun suggestedFilename(entry: RecordingEntry): String {
        val original = entry.filename.substringAfterLast('/').substringAfterLast('\\')
        val base = original.substringBeforeLast('.').replace(Regex("[^\\p{L}\\p{N}._-]"), "_")
            .trim('.', '_').take(60).ifBlank { "recording" }
        return "${base}_${System.currentTimeMillis()}_${UUID.randomUUID().toString().take(8)}.wav"
    }

    suspend fun savedRecordings(): List<SavedRecording> = withContext(Dispatchers.IO) {
        val items = readIndex()
        val existing = items.filter { item ->
            try {
                val uri = Uri.parse(item.uri)
                if (uri.scheme == "file") uri.path?.let { File(it).isFile } == true
                else resolver.openFileDescriptor(uri, "r")?.use { true } == true
            }
            catch (_: Exception) { false }
        }
        if (existing.size != items.size) writeIndex(existing)
        existing
    }

    /** Recovers completed private WAV files after an Activity/process stop. */
    suspend fun recoverLocalRecordings() = withContext(Dispatchers.IO) {
        val directory = File(context.filesDir, "local_recordings")
        val indexed = readIndex()
        val known = indexed.map { it.uri }.toSet()
        val recovered = directory.listFiles().orEmpty().filter { it.isFile && it.extension == "wav" }
            .mapNotNull { file ->
                val uri = Uri.fromFile(file).toString()
                if (uri in known) return@mapNotNull null
                try {
                    FileInputStream(file).use { WavValidator.validate(it, file.length()) }
                    SavedRecording(file.name, uri, file.length())
                } catch (_: Exception) {
                    file.delete()
                    null
                }
            }
        if (recovered.isNotEmpty()) writeIndex(recovered + indexed)
    }

    /** Publishes a completed phone recording. The private WAV remains playable if export fails. */
    suspend fun publishLocalWav(file: File, filename: String, documentUri: Uri?): SavedRecording =
        withContext(Dispatchers.IO) {
            val size = file.length()
            FileInputStream(file).use { WavValidator.validate(it, size) }
            var destination: Uri? = documentUri
            var published = false
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && destination == null) {
                    val values = ContentValues().apply {
                        put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                        put(MediaStore.MediaColumns.MIME_TYPE, "audio/wav")
                        put(MediaStore.MediaColumns.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/SoundField/")
                        put(MediaStore.MediaColumns.IS_PENDING, 1)
                    }
                    destination = resolver.insert(MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
                        ?: throw IOException("无法创建手机 WAV 文件")
                }
                val uri = destination ?: throw IOException("没有选定 WAV 保存位置")
                FileInputStream(file).use { input ->
                    (resolver.openOutputStream(uri, "wt") ?: throw IOException("无法写入手机 WAV 文件")).use { output ->
                        input.copyTo(output, 64 * 1024)
                        output.flush()
                    }
                }
                (resolver.openInputStream(uri) ?: throw IOException("无法校验手机 WAV 文件")).use {
                    WavValidator.validate(it, size)
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && documentUri == null) {
                    if (resolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null) != 1)
                        throw IOException("无法发布手机 WAV 文件")
                } else {
                    try {
                        resolver.takePersistableUriPermission(uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                    } catch (_: SecurityException) { }
                }
                val saved = SavedRecording(filename, uri.toString(), size)
                writeIndex(listOf(saved) + readIndex())
                published = true
                file.delete()
                saved
            } catch (_: Exception) {
                val saved = SavedRecording(filename, Uri.fromFile(file).toString(), size)
                writeIndex(listOf(saved) + readIndex())
                saved
            } finally {
                if (!published) destination?.let { try { resolver.delete(it, null, null) } catch (_: Exception) { } }
            }
        }

    suspend fun download(
        entry: RecordingEntry,
        host: String,
        port: Int,
        network: Network?,
        documentUri: Uri?,
        filename: String,
        onProgress: (Long, Long) -> Unit
    ): SavedRecording = withContext(Dispatchers.IO) {
        if (entry.id.isBlank()) throw IOException("录音 ID 为空")
        val url = URL("http://$host:$port/api/recordings/${Uri.encode(entry.id)}")
        val connection = (network?.openConnection(url) ?: url.openConnection()) as HttpURLConnection
        var destination: Uri? = documentUri
        var published = false
        try {
            connection.requestMethod = "GET"
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 4000
            connection.readTimeout = 10000
            connection.setRequestProperty("Accept", "audio/wav, audio/x-wav, application/octet-stream")
            connection.setRequestProperty("Accept-Encoding", "identity")
            if (connection.responseCode != HttpURLConnection.HTTP_OK)
                throw IOException("HTTP ${connection.responseCode}：下载失败")
            val announced = connection.contentLengthLong
            if (announced > MAX_WAV_BYTES || entry.sizeBytes > MAX_WAV_BYTES)
                throw IOException("WAV 文件超过 2 GB 上限")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && destination == null) {
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                    put(MediaStore.MediaColumns.MIME_TYPE, "audio/wav")
                    put(MediaStore.MediaColumns.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/SoundField/")
                    put(MediaStore.MediaColumns.IS_PENDING, 1)
                }
                destination = resolver.insert(MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
                    ?: throw IOException("无法创建下载文件")
            }
            val uri = destination ?: throw IOException("请选择保存 WAV 的位置")
            var received = 0L
            var lastUpdate = 0L
            connection.inputStream.use { input ->
                (resolver.openOutputStream(uri, "wt") ?: throw IOException("无法写入下载文件")).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val count = input.read(buffer)
                        if (count < 0) break
                        received += count
                        if (received > MAX_WAV_BYTES) throw IOException("WAV 文件超过 2 GB 上限")
                        output.write(buffer, 0, count)
                        if (received - lastUpdate >= 64 * 1024) {
                            onProgress(received, if (entry.sizeBytes > 0) entry.sizeBytes else announced)
                            lastUpdate = received
                        }
                    }
                    output.flush()
                }
            }
            if (announced >= 0 && received != announced) throw IOException("HTTP 文件长度不匹配：收到 $received / $announced 字节")
            if (entry.sizeBytes > 0 && received != entry.sizeBytes)
                throw IOException("录音列表与下载文件长度不匹配：收到 $received / ${entry.sizeBytes} 字节")
            (resolver.openInputStream(uri) ?: throw IOException("无法检查 WAV 文件")).use {
                WavValidator.validate(it, received)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && documentUri == null) {
                val values = ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }
                if (resolver.update(uri, values, null, null) != 1) throw IOException("无法发布下载文件")
            } else {
                try {
                    resolver.takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                } catch (_: SecurityException) {
                    // Some document providers do not offer a persistent grant; the current URI remains usable.
                }
            }
            val saved = SavedRecording(filename, uri.toString(), received)
            writeIndex(listOf(saved) + readIndex())
            published = true
            saved
        } finally {
            if (!published) destination?.let { try { resolver.delete(it, null, null) } catch (_: Exception) { } }
            connection.disconnect()
        }
    }

    private fun readIndex(): List<SavedRecording> = try {
        val array = JSONArray(preferences.getString("items", "[]"))
        List(array.length()) { index ->
            val item = array.getJSONObject(index)
            SavedRecording(item.getString("filename"), item.getString("uri"), item.getLong("size_bytes"))
        }
    } catch (_: Exception) { emptyList() }

    private fun writeIndex(items: List<SavedRecording>) {
        val array = JSONArray()
        items.forEach { item ->
            array.put(JSONObject().put("filename", item.filename).put("uri", item.uri).put("size_bytes", item.sizeBytes))
        }
        preferences.edit().putString("items", array.toString()).apply()
    }

    companion object { private const val MAX_WAV_BYTES = 2_000_000_000L }
}
