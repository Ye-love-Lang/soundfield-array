package com.example.soundfield.protocol

import com.example.soundfield.SoundSource
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.nio.charset.StandardCharsets

const val MAX_CONTROL_BYTES = 2048

enum class Command {
    PING, GET_INFO, GET_STATUS, GET_SPECTRUM, GET_SOURCES, SET_ANGLE,
    START_RECORD, STOP_RECORD, LIST_RECORDINGS, START_STREAM, STOP_STREAM,
    GET_FILTER_STATUS, SET_FILTER_GAIN
}

class ProtocolException(message: String) : IOException(message)
class DeviceCommandException(val code: String, message: String) : IOException("$code: $message")

data class ControlResponse(val id: Int, val data: JSONObject, val error: DeviceCommandException? = null)
data class DeviceStatus(val targetAngleDeg: Int, val recording: Boolean, val streaming: Boolean, val simulated: Boolean)
data class SpectrumData(val values: List<Int>, val simulated: Boolean)
data class SourceData(val sources: List<SoundSource>, val simulated: Boolean)
data class DeviceInfo(
    val name: String,
    val firmwareVersion: String,
    val sampleRateHz: Int,
    val simulated: Boolean,
    val supportsAudio: Boolean,
    val supportsFileDownload: Boolean,
    val supportsGainControl: Boolean
)
data class RecordingEntry(val id: String, val filename: String, val sizeBytes: Long)
data class StreamFormat(val port: Int)

object ControlProtocol {
    fun encode(id: Int, command: Command, args: JSONObject = JSONObject()): ByteArray {
        require(id > 0)
        val json = JSONObject().put("v", 1).put("id", id).put("cmd", command.name).put("args", args)
        val bytes = (json.toString() + "\n").toByteArray(StandardCharsets.UTF_8)
        require(bytes.size <= MAX_CONTROL_BYTES) { "控制消息超过 2048 字节" }
        return bytes
    }

    fun decode(bytes: ByteArray): ControlResponse {
        if (bytes.isEmpty() || bytes.size > MAX_CONTROL_BYTES) throw ProtocolException("控制消息长度无效")
        val json = try { JSONObject(String(bytes, StandardCharsets.UTF_8)) }
        catch (e: Exception) { throw ProtocolException("JSON 格式错误: ${e.message}") }
        if (requiredInt(json, "v", 1..1) != 1) throw ProtocolException("不支持的协议版本")
        val id = requiredInt(json, "id", 1..Int.MAX_VALUE)
        if (!json.has("ok") || json.opt("ok") !is Boolean) throw ProtocolException("响应缺少 ok")
        if (!json.getBoolean("ok")) {
            val error = json.optJSONObject("error") ?: throw ProtocolException("失败响应缺少 error")
            return ControlResponse(id, JSONObject(), DeviceCommandException(error.optString("code", "UNKNOWN"), error.optString("message", "设备拒绝命令")))
        }
        return ControlResponse(id, json.optJSONObject("data") ?: throw ProtocolException("成功响应缺少 data"))
    }

    fun status(data: JSONObject): DeviceStatus {
        val angle = requiredInt(data, "target_angle_deg", 0..359)
        return DeviceStatus(angle, requiredBoolean(data, "recording"), requiredBoolean(data, "streaming"), requiredBoolean(data, "simulated"))
    }

    fun spectrum(data: JSONObject): SpectrumData {
        if (requiredInt(data, "step_deg", 5..5) != 5) throw ProtocolException("声场角度间隔错误")
        val array = data.optJSONArray("values") ?: throw ProtocolException("缺少 values")
        if (array.length() != 72) throw ProtocolException("声场数据必须有 72 点")
        return SpectrumData(List(72) { requiredArrayInt(array, it, 0..1000) }, requiredBoolean(data, "simulated"))
    }

    fun sources(data: JSONObject): SourceData {
        val array = data.optJSONArray("sources") ?: throw ProtocolException("缺少 sources")
        val sources = List(array.length()) { index ->
            val source = array.optJSONObject(index) ?: throw ProtocolException("声源格式错误")
            SoundSource(
                requiredInt(source, "source_id", 0..Int.MAX_VALUE),
                requiredInt(source, "angle_deg", 0..359),
                requiredInt(source, "strength", 0..1000)
            )
        }
        return SourceData(sources, requiredBoolean(data, "simulated"))
    }

    fun info(data: JSONObject): DeviceInfo {
        requiredInt(data, "protocol_version", 1..1)
        val sampleRate = requiredInt(data, "sample_rate_hz", 48000..48000)
        return DeviceInfo(
            name = data.optString("device_name", "Zynq 设备"),
            firmwareVersion = data.optString("firmware_version", "未知"),
            sampleRateHz = sampleRate,
            simulated = requiredBoolean(data, "simulated"),
            supportsAudio = data.optJSONObject("capabilities")?.optBoolean("stream_audio", false) == true,
            supportsFileDownload = data.optJSONObject("capabilities")?.optBoolean("file_download", false) == true,
            supportsGainControl = data.optJSONObject("capabilities")?.opt("gain_control") == true
        )
    }

    fun filterGain(data: JSONObject): Int {
        val value = data.opt("gain_db")
        if (value !is Int && value !is Long) throw ProtocolException("gain_db 必须是整数")
        return requiredInt(data, "gain_db", 0..96)
    }

    fun streamFormat(data: JSONObject): StreamFormat {
        requiredInt(data, "sample_rate_hz", 48000..48000)
        requiredInt(data, "channels", 1..1)
        requiredInt(data, "bits_per_sample", 16..16)
        if (data.optString("encoding") != "S16LE") throw ProtocolException("设备返回的 PCM 编码不是 S16LE")
        return StreamFormat(requiredInt(data, "port", 1..65535))
    }

    fun recordings(data: JSONObject): List<RecordingEntry> {
        val array = data.optJSONArray("recordings") ?: throw ProtocolException("缺少 recordings")
        return List(array.length()) { index ->
            val item = array.optJSONObject(index) ?: throw ProtocolException("录音条目格式错误")
            val id = item.optString("id")
            val filename = item.optString("filename")
            val size = item.opt("size_bytes")
            if (id.isBlank() || filename.isBlank() || size !is Number || size.toDouble() % 1.0 != 0.0 || size.toLong() < 0)
                throw ProtocolException("录音条目缺少有效的 id、filename 或 size_bytes")
            RecordingEntry(id, filename, size.toLong())
        }
    }

    private fun requiredBoolean(data: JSONObject, key: String): Boolean {
        val value = data.opt(key)
        if (value !is Boolean) throw ProtocolException("$key 必须是布尔值")
        return value
    }

    private fun requiredInt(data: JSONObject, key: String, range: IntRange): Int {
        val value = data.opt(key)
        if (value !is Number || value.toDouble() % 1.0 != 0.0) throw ProtocolException("$key 必须是整数")
        val number = value.toDouble()
        if (number < range.first || number > range.last) throw ProtocolException("$key 超出范围")
        return number.toInt()
    }

    private fun requiredArrayInt(array: JSONArray, index: Int, range: IntRange): Int {
        val value = array.opt(index)
        if (value !is Number || value.toDouble() % 1.0 != 0.0) throw ProtocolException("values[$index] 必须是整数")
        val number = value.toDouble()
        if (number < range.first || number > range.last) throw ProtocolException("values[$index] 超出范围")
        return number.toInt()
    }
}
