package com.example.soundfield

import android.app.Application
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.media.MediaPlayer
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.soundfield.audio.AudioBufferStats
import com.example.soundfield.audio.PcmStreamPlayer
import com.example.soundfield.protocol.Command
import com.example.soundfield.protocol.ControlProtocol
import com.example.soundfield.protocol.DeviceCommandException
import com.example.soundfield.protocol.DeviceInfo
import com.example.soundfield.protocol.DeviceStatus
import com.example.soundfield.protocol.RecordingEntry
import com.example.soundfield.protocol.ProtocolException
import com.example.soundfield.recordings.RecordingRepository
import com.example.soundfield.recordings.SavedRecording
import com.example.soundfield.recordings.LocalWavRecorder
import com.example.soundfield.transport.ControlClient
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException

enum class ConnectionState { DEMO, CONNECTING, CONNECTED, ERROR }
enum class StreamState { STOPPED, STARTING, PLAYING, RECONNECTING, PAUSED, ERROR }
enum class LocalRecordingState { STOPPED, STARTING, RECORDING, SAVING, ERROR }
data class EthernetOption(val handle: Long, val label: String)

class SoundFieldViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("connection", Context.MODE_PRIVATE)
    private val connectivity = application.getSystemService(ConnectivityManager::class.java)
    private val recordingRepository = RecordingRepository(application)
    private var tick = 0L
    private var generation = 0L
    private var client: ControlClient? = null
    private var connectJob: Job? = null
    private var pollJob: Job? = null
    private var activeNetwork: Network? = null
    private var streamJob: Job? = null
    private var stopCommandJob: Job? = null
    private var player: PcmStreamPlayer? = null
    private var wantsPlayback = false
    private var streamGeneration = 0L
    private var localRecorder: LocalWavRecorder? = null
    private var localSaveJob: Job? = null
    private var localDocumentUri: Uri? = null
    private var pendingLocalFilename: String? = null
    private var downloadJob: Job? = null
    private var downloadGeneration = 0L
    private var wavPlayer: MediaPlayer? = null
    private var playbackGeneration = 0L
    private var pendingFilterGainDb: Int? = null
    private var filterGainSendJob: Job? = null
    private var filterGainDragging = false
    private var pendingDocumentEntry: RecordingEntry? = null
    private var pendingDocumentFilename: String? = null

    var frame by mutableStateOf(DemoField.frame(0, 90))
        private set
    var connectionState by mutableStateOf(ConnectionState.DEMO)
        private set
    var connectionMessage by mutableStateOf("本地 Demo 运行中")
        private set
    var commandError by mutableStateOf<String?>(null)
        private set
    var deviceInfo by mutableStateOf<DeviceInfo?>(null)
        private set
    var deviceStatus by mutableStateOf<DeviceStatus?>(null)
        private set
    var filterGainDb by mutableStateOf<Int?>(null)
        private set
    var filterGainTargetDb by mutableStateOf<Int?>(null)
        private set
    var filterGainLocked by mutableStateOf(false)
        private set
    var filterGainBusy by mutableStateOf(false)
        private set
    var filterGainMessage by mutableStateOf("尚未读取设备增益")
        private set
    var filterGainError by mutableStateOf<String?>(null)
        private set
    var recordings by mutableStateOf<List<RecordingEntry>>(emptyList())
        private set
    var savedRecordings by mutableStateOf<List<SavedRecording>>(emptyList())
        private set
    var downloadingId by mutableStateOf<String?>(null)
        private set
    var downloadMessage by mutableStateOf("尚未下载 WAV")
        private set
    var playingRecordingUri by mutableStateOf<String?>(null)
        private set
    var playbackMessage by mutableStateOf("未试听")
        private set
    var streamState by mutableStateOf(StreamState.STOPPED)
        private set
    var streamMessage by mutableStateOf("未播放")
        private set
    var localRecordingState by mutableStateOf(LocalRecordingState.STOPPED)
        private set
    var localRecordingMessage by mutableStateOf("手机尚未录音")
        private set
    var audioBufferStats by mutableStateOf(AudioBufferStats(0, 0, 0))
        private set
    var host by mutableStateOf(preferences.getString("host", "192.168.7.1") ?: "192.168.7.1")
        private set
    var controlPort by mutableStateOf(preferences.getInt("control_port", 7000))
        private set
    var audioPort by mutableStateOf(preferences.getInt("audio_port", 9000))
        private set
    var httpPort by mutableStateOf(preferences.getInt("http_port", 8000))
        private set
    var ethernetOptions by mutableStateOf<List<EthernetOption>>(emptyList())
        private set
    /** null = auto Ethernet (fallback to default route); -1 = default route. */
    var selectedNetworkHandle by mutableStateOf<Long?>(null)
        private set

    init {
        refreshNetworks()
        viewModelScope.launch {
            recordingRepository.recoverLocalRecordings()
            savedRecordings = recordingRepository.savedRecordings()
        }
    }

    fun advanceDemo() {
        if (connectionState == ConnectionState.CONNECTED) return
        tick++
        frame = DemoField.frame(tick, frame.targetAngleDeg)
    }

    fun refreshNetworks() {
        ethernetOptions = connectivity.allNetworks.mapNotNull { network ->
            val capabilities = connectivity.getNetworkCapabilities(network)
            if (capabilities?.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) == true)
                EthernetOption(network.networkHandle, "以太网 · ${network.networkHandle}")
            else null
        }
    }

    fun selectNetwork(handle: Long?) {
        selectedNetworkHandle = handle
    }

    fun saveNetworkSettings(address: String, control: String, audio: String, http: String): Boolean {
        val trimmed = address.trim()
        val validAddress = trimmed.split('.').let { parts ->
            parts.size == 4 && parts.all { part ->
                val value = part.toIntOrNull()
                part.isNotEmpty() && part.all(Char::isDigit) && value != null && value in 0..255
            }
        }
        val ports = listOf(control, audio, http).map { it.toIntOrNull() }
        if (!validAddress || ports.any { it == null || it !in 1..65535 }) {
            commandError = "请输入有效的 IPv4 地址和 1–65535 端口"
            return false
        }
        disconnect()
        host = trimmed
        controlPort = ports[0]!!
        audioPort = ports[1]!!
        httpPort = ports[2]!!
        preferences.edit().putString("host", host).putInt("control_port", controlPort)
            .putInt("audio_port", audioPort).putInt("http_port", httpPort).apply()
        commandError = null
        connectionMessage = "配置已保存 · 本地 Demo 运行中"
        return true
    }

    fun connect() {
        if (connectionState == ConnectionState.CONNECTING || connectionState == ConnectionState.CONNECTED) return
        disconnect()
        val token = generation
        connectionState = ConnectionState.CONNECTING
        connectionMessage = "正在连接 $host:$controlPort"
        connectJob = viewModelScope.launch {
            try {
                val network = chooseNetwork()
                val connected = ControlClient.connect(host, controlPort, network, viewModelScope) { reason ->
                    viewModelScope.launch { failConnection(token, reason) }
                }
                if (token != generation) { connected.close(); return@launch }
                client = connected
                activeNetwork = network
                connected.request(Command.PING)
                val info = ControlProtocol.info(connected.request(Command.GET_INFO))
                if (token != generation) return@launch
                deviceInfo = info
                connectionState = ConnectionState.CONNECTED
                connectionMessage = "已连接 ${info.name} · $host:$controlPort"
                commandError = null
                pollJob = viewModelScope.launch { pollDevice(token, connected) }
                if (info.supportsGainControl) refreshFilterGain()
                if (info.supportsFileDownload) refreshRecordings()
            } catch (_: CancellationException) {
                // A newer connection has taken over.
            } catch (e: Exception) {
                failConnection(token, e.message ?: "连接失败")
            }
        }
    }

    fun disconnect() {
        finishLocalRecording("设备连接已断开")
        generation++
        pendingDocumentEntry = null
        pendingDocumentFilename = null
        cancelDownload()
        stopSavedPlayback()
        stopStreamLocally()
        connectJob?.cancel()
        pollJob?.cancel()
        client?.close("用户已断开连接")
        client = null
        activeNetwork = null
        deviceInfo = null
        deviceStatus = null
        resetFilterGain()
        recordings = emptyList()
        connectionState = ConnectionState.DEMO
        connectionMessage = "本地 Demo 运行中"
        commandError = null
        frame = DemoField.frame(tick, frame.targetAngleDeg)
    }

    fun setTargetAngle(angle: Int) {
        if (angle !in 0..359) return
        val active = client
        if (connectionState != ConnectionState.CONNECTED || active == null) {
            if (connectionState != ConnectionState.CONNECTING) frame = frame.copy(targetAngleDeg = angle)
            return
        }
        val token = generation
        viewModelScope.launch {
            try {
                active.request(Command.SET_ANGLE, JSONObject().put("angle_deg", angle))
                val status = ControlProtocol.status(active.request(Command.GET_STATUS))
                if (token == generation) {
                    deviceStatus = status
                    frame = frame.copy(targetAngleDeg = status.targetAngleDeg)
                    commandError = null
                }
            } catch (e: DeviceCommandException) {
                if (token == generation) commandError = e.message
            } catch (e: Exception) {
                failConnection(token, e.message ?: "设置角度失败")
            }
        }
    }

    fun prepareLocalRecordingFilename(): String = LocalWavRecorder.suggestedFilename().also {
        pendingLocalFilename = it
    }

    fun completeLocalRecordingDocument(uri: Uri?) {
        val filename = pendingLocalFilename
        pendingLocalFilename = null
        if (uri != null && filename != null) startLocalRecording(uri, filename)
    }

    fun startLocalRecording(documentUri: Uri? = null, filename: String = LocalWavRecorder.suggestedFilename()) {
        if (connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsAudio != true || localRecordingState == LocalRecordingState.STARTING ||
            localRecordingState == LocalRecordingState.RECORDING || localRecordingState == LocalRecordingState.SAVING) return
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q && documentUri == null) {
            localRecordingState = LocalRecordingState.ERROR
            localRecordingMessage = "请先选择手机 WAV 保存位置"
            return
        }
        stopSavedPlayback()
        try {
            val sink = LocalWavRecorder.create(getApplication(), filename)
            localRecorder = sink
            localDocumentUri = documentUri
            localRecordingState = LocalRecordingState.STARTING
            localRecordingMessage = "正在接收设备 PCM，准备写入手机 WAV"
            player?.setRecorder(sink)
            if (player != null) {
                localRecordingState = LocalRecordingState.RECORDING
                localRecordingMessage = "正在录入手机：${sink.filename}"
            } else ensurePcmSession()
        } catch (e: Exception) {
            localRecordingState = LocalRecordingState.ERROR
            localRecordingMessage = "手机录音无法开始：${e.message ?: "未知错误"}"
        }
    }

    fun stopLocalRecording() = finishLocalRecording()

    private fun finishLocalRecording(reason: String? = null, stopSession: Boolean = true) {
        if (localRecordingState != LocalRecordingState.STARTING &&
            localRecordingState != LocalRecordingState.RECORDING) return
        val sink = localRecorder
        localRecorder = null
        player?.setRecorder(null)
        val documentUri = localDocumentUri
        localDocumentUri = null
        if (stopSession && !wantsPlayback) stopStream()
        if (sink == null) {
            localRecordingState = LocalRecordingState.ERROR
            localRecordingMessage = reason ?: "未收到设备 PCM"
            return
        }
        localRecordingState = LocalRecordingState.SAVING
        localRecordingMessage = "正在完成手机 WAV 文件"
        localSaveJob = viewModelScope.launch {
            try {
                val file = withContext(Dispatchers.IO) { sink.finish() }
                val saved = recordingRepository.publishLocalWav(file, sink.filename, documentUri)
                savedRecordings = listOf(saved) + savedRecordings
                localRecordingState = LocalRecordingState.STOPPED
                localRecordingMessage = when {
                    saved.uri.startsWith("file:") -> "已保存到 App 私有目录：${saved.filename}（下载目录不可用）"
                    reason != null -> "$reason；已保存手机 WAV：${saved.filename}"
                    else -> "已保存手机 WAV：${saved.filename}"
                }
            } catch (e: Exception) {
                localRecordingState = LocalRecordingState.ERROR
                localRecordingMessage = "手机 WAV 保存失败：${e.message ?: "未知错误"}"
            } finally {
                localSaveJob = null
            }
        }
    }

    fun refreshFilterGain() {
        val active = client ?: return
        if (connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsGainControl != true || filterGainBusy) return
        val token = generation
        filterGainBusy = true
        filterGainError = null
        viewModelScope.launch {
            try {
                val returned = ControlProtocol.filterGain(active.request(Command.GET_FILTER_STATUS))
                if (token == generation) {
                    filterGainDb = returned
                    if (pendingFilterGainDb == null && !filterGainDragging) filterGainTargetDb = returned
                    filterGainMessage = "设备当前增益：$returned dB"
                }
            } catch (_: CancellationException) {
                // A newer connection has taken over.
            } catch (e: Exception) {
                if (token == generation) filterGainError = e.message ?: "读取设备增益失败"
            } finally {
                if (token == generation) {
                    filterGainBusy = false
                    sendPendingFilterGain()
                }
            }
        }
    }

    fun setFilterGain(gainDb: Int) {
        if (gainDb !in 0..96) {
            filterGainError = "请输入 0 到 96 的整数 dB"
            return
        }
        if (client == null) return
        if (connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsGainControl != true || filterGainLocked || filterGainDb == null) return
        if (gainDb == filterGainTargetDb) return
        filterGainTargetDb = gainDb
        pendingFilterGainDb = gainDb
        filterGainError = null
        sendPendingFilterGain()
    }

    fun setFilterGainFromSlider(gainDb: Int) {
        if (filterGainLocked) return
        filterGainDragging = true
        setFilterGain(gainDb)
    }

    fun finishFilterGainDrag() {
        filterGainDragging = false
        if (!filterGainBusy && pendingFilterGainDb == null) filterGainTargetDb = filterGainDb
    }

    fun toggleFilterGainLock() {
        if (connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsGainControl != true) return
        filterGainLocked = !filterGainLocked
        if (filterGainLocked) {
            filterGainDragging = false
            pendingFilterGainDb = null
            filterGainTargetDb = filterGainDb
        }
    }

    private fun sendPendingFilterGain() {
        val active = client ?: return
        if (filterGainBusy || filterGainLocked || pendingFilterGainDb == null ||
            connectionState != ConnectionState.CONNECTED) return
        val token = generation
        filterGainBusy = true
        filterGainSendJob = viewModelScope.launch {
            try {
                while (token == generation && !filterGainLocked) {
                    val next = pendingFilterGainDb ?: break
                    pendingFilterGainDb = null
                    if (next == filterGainDb) continue
                    val returned = ControlProtocol.filterGain(
                        active.request(Command.SET_FILTER_GAIN, JSONObject().put("gain_db", next))
                    )
                    if (token != generation) break
                    filterGainDb = returned
                    filterGainMessage = "设备确认增益：$returned dB"
                    if (pendingFilterGainDb == null && !filterGainDragging) filterGainTargetDb = returned
                }
            } catch (_: CancellationException) {
                // A newer connection has taken over.
            } catch (e: Exception) {
                if (token == generation) {
                    pendingFilterGainDb = null
                    filterGainTargetDb = filterGainDb
                    filterGainError = e.message ?: "设置设备增益失败"
                }
            } finally {
                if (token == generation) {
                    filterGainBusy = false
                    filterGainSendJob = null
                }
            }
        }
    }

    private fun resetFilterGain() {
        filterGainSendJob?.cancel()
        filterGainSendJob = null
        pendingFilterGainDb = null
        filterGainDragging = false
        filterGainDb = null
        filterGainTargetDb = null
        filterGainBusy = false
        filterGainMessage = "尚未读取设备增益"
        filterGainError = null
    }

    fun refreshRecordings() {
        if (deviceInfo?.supportsFileDownload != true) return
        val active = client ?: return
        if (connectionState != ConnectionState.CONNECTED) return
        val token = generation
        viewModelScope.launch {
            try {
                val list = ControlProtocol.recordings(active.request(Command.LIST_RECORDINGS))
                if (token == generation) { recordings = list; commandError = null }
            } catch (e: DeviceCommandException) {
                if (token == generation) commandError = e.message
            } catch (e: Exception) {
                failConnection(token, e.message ?: "获取录音列表失败")
            }
        }
    }

    fun suggestedRecordingFilename(entry: RecordingEntry): String = recordingRepository.suggestedFilename(entry)

    fun prepareDocumentDownload(entry: RecordingEntry): String {
        val filename = suggestedRecordingFilename(entry)
        pendingDocumentEntry = entry
        pendingDocumentFilename = filename
        return filename
    }

    fun completeDocumentDownload(uri: Uri?) {
        val entry = pendingDocumentEntry
        val filename = pendingDocumentFilename
        pendingDocumentEntry = null
        pendingDocumentFilename = null
        if (uri == null) return
        if (entry != null && filename != null &&
            connectionState == ConnectionState.CONNECTED && deviceInfo?.supportsFileDownload == true &&
            downloadingId == null) downloadRecording(entry, uri, filename)
        else viewModelScope.launch(Dispatchers.IO) {
            try { getApplication<Application>().contentResolver.delete(uri, null, null) } catch (_: Exception) { }
        }
    }

    fun refreshSavedRecordings() {
        viewModelScope.launch { savedRecordings = recordingRepository.savedRecordings() }
    }

    fun downloadRecording(entry: RecordingEntry, documentUri: Uri? = null, filename: String = suggestedRecordingFilename(entry)) {
        if (connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsFileDownload != true || downloadingId != null) return
        val token = ++downloadGeneration
        downloadingId = entry.id
        downloadMessage = "正在下载 ${entry.filename}"
        downloadJob = viewModelScope.launch {
            try {
                val saved = recordingRepository.download(entry, host, httpPort, activeNetwork, documentUri, filename) { received, total ->
                    viewModelScope.launch {
                        if (token == downloadGeneration && downloadingId == entry.id) downloadMessage = if (total > 0)
                            "正在下载 ${entry.filename} · ${received * 100 / total}%" else "正在下载 ${entry.filename} · $received 字节"
                    }
                }
                if (token == downloadGeneration) {
                    savedRecordings = listOf(saved) + savedRecordings
                    downloadMessage = "已保存到下载文件夹：${saved.filename}"
                }
            } catch (_: CancellationException) {
                if (token == downloadGeneration) downloadMessage = "下载已取消"
            } catch (e: Exception) {
                if (token == downloadGeneration) downloadMessage = "下载失败：${e.message ?: "未知错误"}"
            } finally {
                if (token == downloadGeneration) downloadingId = null
            }
        }
    }

    fun cancelDownload() {
        downloadGeneration++
        downloadJob?.cancel()
        downloadJob = null
        if (downloadingId != null) downloadMessage = "下载已取消"
        downloadingId = null
    }

    fun playSavedRecording(item: SavedRecording) {
        if (playingRecordingUri == item.uri) { stopSavedPlayback(); return }
        if (streamState != StreamState.STOPPED && streamState != StreamState.ERROR) stopStream()
        stopSavedPlayback()
        val token = ++playbackGeneration
        try {
            val media = MediaPlayer()
            wavPlayer = media
            playingRecordingUri = item.uri
            playbackMessage = "正在准备 ${item.filename}"
            media.setOnPreparedListener {
                if (token == playbackGeneration) {
                    try { it.start(); playbackMessage = "正在试听 ${item.filename}" }
                    catch (e: Exception) { stopSavedPlayback(); playbackMessage = "试听失败：${e.message}" }
                }
            }
            media.setOnCompletionListener {
                if (token == playbackGeneration) stopSavedPlayback()
            }
            media.setOnErrorListener { _, _, _ ->
                if (token == playbackGeneration) { stopSavedPlayback(); playbackMessage = "试听失败，文件可能已损坏或被删除" }
                true
            }
            val uri = Uri.parse(item.uri)
            if (uri.scheme == "file") media.setDataSource(uri.path ?: throw IOException("手机录音路径无效"))
            else media.setDataSource(getApplication(), uri)
            media.prepareAsync()
        } catch (e: Exception) {
            stopSavedPlayback()
            playbackMessage = "试听失败：${e.message ?: "无法打开文件"}"
        }
    }

    fun stopSavedPlayback() {
        playbackGeneration++
        try { wavPlayer?.release() } catch (_: Exception) { }
        wavPlayer = null
        playingRecordingUri = null
        playbackMessage = "未试听"
    }

    fun startStream() {
        if (connectionState != ConnectionState.CONNECTED || deviceInfo?.supportsAudio != true) return
        if (streamState == StreamState.STARTING || streamState == StreamState.PLAYING || streamState == StreamState.RECONNECTING) return
        stopSavedPlayback()
        wantsPlayback = true
        if (player != null) {
            player?.setPlaybackEnabled(true)
            streamState = StreamState.PLAYING
            streamMessage = "正在播放 · 48 kHz / 单声道 / S16LE"
            return
        }
        streamState = StreamState.STARTING
        streamMessage = "正在启动 48 kHz PCM"
        ensurePcmSession()
    }

    private fun ensurePcmSession() {
        val active = client ?: return
        if (streamJob?.isActive == true || connectionState != ConnectionState.CONNECTED ||
            deviceInfo?.supportsAudio != true ||
            (!wantsPlayback && localRecorder == null)) return
        val token = ++streamGeneration
        streamJob = viewModelScope.launch {
            stopCommandJob?.join()
            var failures = 0
            while (token == streamGeneration && connectionState == ConnectionState.CONNECTED &&
                (wantsPlayback || localRecorder != null)) {
                try {
                    val format = ControlProtocol.streamFormat(active.request(Command.START_STREAM))
                    if (token != streamGeneration) return@launch
                    val started = PcmStreamPlayer.connect(
                        host, format.port, activeNetwork, viewModelScope,
                        wantsPlayback, localRecorder,
                        { error ->
                            viewModelScope.launch {
                                if (token == streamGeneration && localRecorder != null)
                                    finishLocalRecording("手机写入 PCM 失败：${error.message ?: "未知错误"}")
                            }
                        },
                        { stats ->
                            viewModelScope.launch {
                                if (token == streamGeneration) audioBufferStats = stats
                            }
                        }
                    )
                    if (token != streamGeneration) { started.close(); return@launch }
                    player = started
                    if (localRecorder != null) {
                        localRecordingState = LocalRecordingState.RECORDING
                        localRecordingMessage = "正在录入手机：${localRecorder?.filename}"
                    }
                    if (wantsPlayback) {
                        streamState = StreamState.PLAYING
                        streamMessage = "正在播放 · 48 kHz / 单声道 / S16LE"
                    }
                    throw started.failure.await()
                } catch (_: CancellationException) {
                    return@launch
                } catch (e: DeviceCommandException) {
                    if (token == streamGeneration) {
                        if (localRecorder != null) finishLocalRecording(e.message ?: "设备拒绝 PCM", stopSession = false)
                        if (wantsPlayback) {
                            streamState = StreamState.ERROR
                            streamMessage = e.message ?: "设备拒绝实时播放"
                        }
                    }
                    return@launch
                } catch (e: ProtocolException) {
                    if (token == streamGeneration) {
                        if (localRecorder != null) finishLocalRecording(e.message ?: "PCM 格式不匹配", stopSession = false)
                        if (wantsPlayback) {
                            streamState = StreamState.ERROR
                            streamMessage = e.message ?: "PCM 格式不匹配"
                        }
                    }
                    return@launch
                } catch (e: Exception) {
                    if (token != streamGeneration) return@launch
                    if (localRecorder != null) finishLocalRecording("音频连接中断", stopSession = false)
                    if (!wantsPlayback) return@launch
                    failures++
                    if (failures >= 3) {
                        streamState = StreamState.ERROR
                        streamMessage = "音频连接失败：${readableConnectionError(e.message ?: "未知错误")}"
                        return@launch
                    }
                    streamState = StreamState.RECONNECTING
                    streamMessage = "音频中断，正在重连（$failures/2）"
                } finally {
                    if (token == streamGeneration) {
                        player?.close()
                        player = null
                        audioBufferStats = AudioBufferStats(0, 0, 0)
                        if (connectionState == ConnectionState.CONNECTED) {
                            try { active.request(Command.STOP_STREAM) } catch (_: Exception) { }
                        }
                    }
                }
                if (wantsPlayback) delay(500L * failures)
            }
        }
    }

    fun stopStream() {
        wantsPlayback = false
        player?.setPlaybackEnabled(false)
        if (localRecorder != null) {
            streamState = StreamState.STOPPED
            streamMessage = "实时播放已停止，手机录音继续"
            return
        }
        val active = client
        stopStreamLocally()
        if (active != null && connectionState == ConnectionState.CONNECTED) {
            val token = generation
            stopCommandJob = viewModelScope.launch {
                try {
                    active.request(Command.STOP_STREAM)
                    if (token == generation) deviceStatus = ControlProtocol.status(active.request(Command.GET_STATUS))
                } catch (e: DeviceCommandException) {
                    if (token == generation) commandError = e.message
                } catch (e: Exception) {
                    failConnection(token, e.message ?: "停止音频失败")
                }
            }
        }
    }

    fun pauseStream() {
        if (streamState != StreamState.PLAYING && streamState != StreamState.STARTING && streamState != StreamState.RECONNECTING) return
        stopStream()
        streamState = StreamState.PAUSED
        streamMessage = if (localRecorder != null) "实时播放已暂停，手机录音继续" else "已暂停，继续时重新连接 PCM"
    }

    private fun stopStreamLocally() {
        wantsPlayback = false
        streamGeneration++
        streamJob?.cancel()
        streamJob = null
        player?.close()
        player = null
        streamState = StreamState.STOPPED
        streamMessage = "未播放"
        audioBufferStats = AudioBufferStats(0, 0, 0)
    }

    private suspend fun pollDevice(token: Long, active: ControlClient) {
        while (token == generation) {
            try {
                val status = ControlProtocol.status(active.request(Command.GET_STATUS))
                val spectrum = ControlProtocol.spectrum(active.request(Command.GET_SPECTRUM))
                val sources = ControlProtocol.sources(active.request(Command.GET_SOURCES))
                if (token != generation) return
                deviceStatus = status
                frame = DemoFrame(
                    spectrum.values, sources.sources, status.targetAngleDeg,
                    status.simulated || spectrum.simulated || sources.simulated || (deviceInfo?.simulated == true)
                )
                delay(400)
            } catch (_: CancellationException) {
                return
            } catch (e: Exception) {
                failConnection(token, e.message ?: "设备数据刷新失败")
                return
            }
        }
    }

    private fun chooseNetwork(): Network? {
        val networks = connectivity.allNetworks
        if (selectedNetworkHandle == -1L) return null
        if (selectedNetworkHandle != null) {
            return networks.firstOrNull { it.networkHandle == selectedNetworkHandle }
                ?: throw IOException("所选以太网接口已断开，请刷新接口列表")
        }
        return networks.firstOrNull {
            connectivity.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) == true
        }
    }

    private fun failConnection(token: Long, message: String) {
        if (token != generation) return
        finishLocalRecording("设备连接异常：${readableConnectionError(message)}")
        generation++
        cancelDownload()
        stopStreamLocally()
        pollJob?.cancel()
        client?.close(message)
        client = null
        activeNetwork = null
        deviceInfo = null
        deviceStatus = null
        resetFilterGain()
        recordings = emptyList()
        connectionState = ConnectionState.ERROR
        connectionMessage = readableConnectionError(message)
        frame = DemoField.frame(tick, frame.targetAngleDeg)
    }

    private fun readableConnectionError(message: String): String = when {
        message.contains("ENETUNREACH", ignoreCase = true) -> "网络不可达，请检查接口选择和设备 IP"
        message.contains("ECONNREFUSED", ignoreCase = true) -> "连接被拒绝，请检查控制服务和端口"
        message.contains("timed out", ignoreCase = true) -> "连接超时，请检查网络和设备"
        else -> message.take(160)
    }

    override fun onCleared() {
        localRecorder?.let { sink ->
            player?.setRecorder(null)
            localRecorder = null
            try { sink.finish() } catch (_: Exception) { sink.discard() }
            localRecordingState = LocalRecordingState.STOPPED
        }
        disconnect()
        super.onCleared()
    }
}
