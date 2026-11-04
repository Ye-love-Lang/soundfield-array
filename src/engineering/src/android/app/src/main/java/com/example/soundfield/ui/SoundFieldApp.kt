package com.example.soundfield.ui

import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.example.soundfield.DemoFrame
import com.example.soundfield.ConnectionState
import com.example.soundfield.LocalRecordingState
import com.example.soundfield.SoundFieldViewModel
import com.example.soundfield.StreamState
import com.example.soundfield.ui.theme.Amber
import com.example.soundfield.ui.theme.Cyan
import com.example.soundfield.ui.theme.Muted
import com.example.soundfield.ui.theme.Panel
import kotlinx.coroutines.delay
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin
import kotlin.math.roundToInt

@Composable
fun SoundFieldApp(viewModel: SoundFieldViewModel) {
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }
    val lifecycleOwner = LocalLifecycleOwner.current

    LaunchedEffect(lifecycleOwner, viewModel) {
        lifecycleOwner.lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                delay(180)
                viewModel.advanceDemo()
            }
        }
    }
    LaunchedEffect(selectedTab) {
        if (selectedTab == 1) viewModel.refreshNetworks()
    }

    Scaffold(
        bottomBar = {
            NavigationBar(containerColor = Panel) {
                listOf("声场", "通信", "录音").forEachIndexed { index, label ->
                    NavigationBarItem(
                        selected = selectedTab == index,
                        onClick = { selectedTab = index },
                        icon = { Text(listOf("◎", "⌁", "◉")[index], fontSize = 21.sp) },
                        label = { Text(label) },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = Color(0xFF0A1923),
                            selectedTextColor = Cyan,
                            indicatorColor = Cyan,
                            unselectedIconColor = Muted,
                            unselectedTextColor = Muted
                        )
                    )
                }
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState())
                .padding(horizontal = 18.dp, vertical = 20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Header(viewModel)
            when (selectedTab) {
                0 -> MonitorPage(viewModel)
                1 -> ConnectionPage(viewModel)
                else -> RecordingsPage(viewModel)
            }
        }
    }
}

@Composable
private fun Header(viewModel: SoundFieldViewModel) {
    val connected = viewModel.connectionState == ConnectionState.CONNECTED
    val label = when {
        connected && viewModel.frame.simulated -> "模拟设备已连接 · 模拟数据"
        connected -> "设备已连接 · 实时数据"
        viewModel.connectionState == ConnectionState.CONNECTING -> "正在连接 · 本地模拟数据"
        else -> "本地演示 · 模拟数据 · 未连接设备"
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("SOUNDFIELD  /  01", color = Cyan, style = MaterialTheme.typography.labelMedium)
        Text("声场感知与选择性拾音", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(8.dp).background(Amber, CircleShape))
            Spacer(Modifier.width(7.dp))
            Text(label, color = Amber, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
private fun MonitorPage(viewModel: SoundFieldViewModel) {
    val frame = viewModel.frame
    SectionCard {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("空间声场", fontWeight = FontWeight.SemiBold)
            Text("72 点 · 5° 间隔", color = Muted, style = MaterialTheme.typography.labelSmall)
        }
        Text("相对声能  0–1000", color = Muted, style = MaterialTheme.typography.bodySmall)
        PolarPlot(frame, onSourceClick = viewModel::setTargetAngle)
        Text("0° 正右方 · 逆时针增加", color = Muted, style = MaterialTheme.typography.labelSmall)
    }

    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        MetricCard("检测声源", "${frame.sources.size} 个", Modifier.weight(1f))
        MetricCard("拾音目标", "${frame.targetAngleDeg}°", Modifier.weight(1f))
    }

    SectionCard {
        Text("选择声源", fontWeight = FontWeight.SemiBold)
        frame.sources.forEach { source ->
            Row(
                Modifier.fillMaxWidth().clickable { viewModel.setTargetAngle(source.angleDeg) }
                    .padding(vertical = 7.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(Modifier.size(9.dp).background(if (source.id == 0) Cyan else Amber, CircleShape))
                Spacer(Modifier.width(12.dp))
                Text("声源 ${source.id + 1}", modifier = Modifier.weight(1f))
                Text("${source.angleDeg}°", color = Cyan, fontWeight = FontWeight.Bold)
                Spacer(Modifier.width(12.dp))
                Text("${source.strength}/1000", color = Muted, style = MaterialTheme.typography.bodySmall)
            }
        }
        Text(
            if (viewModel.connectionState == ConnectionState.CONNECTED) "点击声源将发送 SET_ANGLE" else "点击声源可设置演示拾音方向",
            color = Muted, style = MaterialTheme.typography.labelSmall
        )
    }

    AngleControl(frame.targetAngleDeg, viewModel.connectionState == ConnectionState.CONNECTED, viewModel::setTargetAngle)

    SectionCard {
        Text("设备状态", fontWeight = FontWeight.SemiBold)
        val status = viewModel.deviceStatus
        Text("手机录音：${when (viewModel.localRecordingState) {
            LocalRecordingState.STARTING, LocalRecordingState.RECORDING -> "进行中"
            LocalRecordingState.SAVING -> "保存中"
            else -> "已停止"
        }}  ·  实时播放：${if (viewModel.streamState == StreamState.PLAYING) "进行中" else "已停止"}", color = Muted)
        Text(viewModel.connectionMessage, color = Muted, style = MaterialTheme.typography.bodySmall)
        viewModel.commandError?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
    }
}

@Composable
private fun AngleControl(angle: Int, connected: Boolean, onApply: (Int) -> Unit) {
    var input by rememberSaveable { mutableStateOf(angle.toString()) }
    var sliderAngle by remember { mutableIntStateOf(angle) }
    var invalid by remember { mutableStateOf(false) }
    LaunchedEffect(angle) { input = angle.toString(); sliderAngle = angle; invalid = false }
    fun commit() {
        val parsed = input.toIntOrNull()
        if (parsed != null && parsed in 0..359) {
            onApply(parsed)
            invalid = false
        } else invalid = true
    }
    SectionCard {
        Text("手动拾音方向", fontWeight = FontWeight.SemiBold)
        Slider(
            value = sliderAngle.toFloat(),
            onValueChange = { sliderAngle = it.toInt().coerceIn(0, 359) },
            onValueChangeFinished = { onApply(sliderAngle) },
            valueRange = 0f..359f
        )
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            OutlinedTextField(
                value = input,
                onValueChange = { input = it; invalid = false },
                modifier = Modifier.weight(1f),
                label = { Text("角度 0–359") },
                suffix = { Text("°") },
                singleLine = true,
                isError = invalid,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                keyboardActions = KeyboardActions(onDone = { commit() })
            )
            Button(onClick = { commit() }) { Text("应用") }
        }
        if (invalid) Text("请输入 0 到 359 的整数", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        Text(
            if (connected) "应用后向设备发送 SET_ANGLE，并等待设备确认" else "当前只改变本地演示画面，未向设备发送命令",
            color = Muted, style = MaterialTheme.typography.bodySmall
        )
    }
}

@Composable
private fun ConnectionPage(viewModel: SoundFieldViewModel) {
    var address by rememberSaveable { mutableStateOf(viewModel.host) }
    var control by rememberSaveable { mutableStateOf(viewModel.controlPort.toString()) }
    var audio by rememberSaveable { mutableStateOf(viewModel.audioPort.toString()) }
    var http by rememberSaveable { mutableStateOf(viewModel.httpPort.toString()) }
    SectionCard {
        Text("以太网通信", fontWeight = FontWeight.SemiBold)
        Text("当前：${viewModel.connectionMessage}", color = Amber)
        Text(
            "以太网支持 TCP 控制、PCM 实时播放与手机录音；设备 WAV 下载取决于固件能力。",
            color = Muted, style = MaterialTheme.typography.bodyMedium
        )
        OutlinedTextField(value = address, onValueChange = { address = it }, label = { Text("设备 IPv4 地址") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(value = control, onValueChange = { control = it }, label = { Text("控制端口") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true, modifier = Modifier.weight(1f))
            OutlinedTextField(value = audio, onValueChange = { audio = it }, label = { Text("音频参考端口") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true, modifier = Modifier.weight(1f))
            OutlinedTextField(value = http, onValueChange = { http = it }, label = { Text("HTTP 端口") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true, modifier = Modifier.weight(1f))
        }
        Button(onClick = { viewModel.saveNetworkSettings(address, control, audio, http) }) { Text("保存配置") }
        Text("网络接口", fontWeight = FontWeight.SemiBold)
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = viewModel.selectedNetworkHandle == null, onClick = { viewModel.selectNetwork(null) }, label = { Text("自动选以太网") })
            FilterChip(selected = viewModel.selectedNetworkHandle == -1L, onClick = { viewModel.selectNetwork(-1L) }, label = { Text("系统默认路由") })
            viewModel.ethernetOptions.forEach { option ->
                FilterChip(selected = viewModel.selectedNetworkHandle == option.handle, onClick = { viewModel.selectNetwork(option.handle) }, label = { Text(option.label) })
            }
        }
        Button(onClick = viewModel::refreshNetworks) { Text("刷新接口") }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(onClick = { if (viewModel.saveNetworkSettings(address, control, audio, http)) viewModel.connect() }, enabled = viewModel.connectionState != ConnectionState.CONNECTING && viewModel.connectionState != ConnectionState.CONNECTED) {
                Text(if (viewModel.connectionState == ConnectionState.ERROR) "重新连接" else "连接设备")
            }
            Button(onClick = viewModel::disconnect, enabled = viewModel.connectionState == ConnectionState.CONNECTED || viewModel.connectionState == ConnectionState.CONNECTING) { Text("断开") }
        }
        viewModel.commandError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    }
    SectionCard {
        Text("接口状态", fontWeight = FontWeight.SemiBold)
        Text(viewModel.deviceInfo?.let { "${it.name} · 固件 ${it.firmwareVersion} · ${it.sampleRateHz} Hz" } ?: "未获取设备信息", color = Muted)
        Text(if (viewModel.connectionState == ConnectionState.CONNECTED) "声场与状态由设备返回" else "当前声场与声源由手机本地生成", color = Muted, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun RecordingsPage(viewModel: SoundFieldViewModel) {
    val connected = viewModel.connectionState == ConnectionState.CONNECTED
    val recording = viewModel.localRecordingState == LocalRecordingState.STARTING ||
        viewModel.localRecordingState == LocalRecordingState.RECORDING
    val canRecord = connected && viewModel.deviceInfo?.supportsAudio == true
    val canDownload = connected && viewModel.deviceInfo?.supportsFileDownload == true
    val createLocalDocument = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        viewModel.completeLocalRecordingDocument(uri)
    }
    val createDocument = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        viewModel.completeDocumentDownload(uri)
    }
    SectionCard {
        Text("手机录音 · 设备 PCM", fontWeight = FontWeight.SemiBold)
        Text(viewModel.localRecordingMessage, color = Amber)
        Button(onClick = {
            if (recording) viewModel.stopLocalRecording()
            else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) viewModel.startLocalRecording()
            else createLocalDocument.launch(viewModel.prepareLocalRecordingFilename())
        }, enabled = (canRecord || recording) && viewModel.localRecordingState != LocalRecordingState.SAVING) {
            Text(if (recording) "停止并保存手机 WAV" else "开始手机录音")
        }
        Text("手机接收 TCP PCM 并持续写入本地 WAV；可同时实时试听，不受 Zynq 的 20 秒 RAM 录音上限影响。",
            color = Muted, style = MaterialTheme.typography.bodySmall)
        if (!canRecord && !recording) Text("需连接支持实时 PCM 的以太网设备", color = Muted, style = MaterialTheme.typography.bodySmall)
        Text("设备旧版 WAV（兼容下载）", fontWeight = FontWeight.SemiBold)
        Button(onClick = viewModel::refreshRecordings, enabled = canDownload) { Text("刷新设备录音列表") }
        if (canDownload && viewModel.recordings.isEmpty()) Text("设备暂无已保存的录音", color = Muted)
        viewModel.recordings.forEach { item ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Column(Modifier.weight(1f)) {
                    Text(item.filename, maxLines = 2)
                    Text("${item.sizeBytes} 字节", color = Muted, style = MaterialTheme.typography.bodySmall)
                }
                Button(onClick = {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                        viewModel.downloadRecording(item, filename = viewModel.suggestedRecordingFilename(item))
                    else createDocument.launch(viewModel.prepareDocumentDownload(item))
                }, enabled = canDownload && viewModel.downloadingId == null) { Text("下载") }
            }
        }
        if (viewModel.downloadingId != null) Button(onClick = viewModel::cancelDownload) { Text("取消下载") }
        Text(viewModel.downloadMessage, color = Muted, style = MaterialTheme.typography.bodySmall)
        if (connected && !canDownload) Text("设备未声明 WAV 下载能力", color = Muted, style = MaterialTheme.typography.bodySmall)
        viewModel.commandError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    }
    FilterGainControl(viewModel)
    SectionCard {
        Text("手机已保存的 WAV", fontWeight = FontWeight.SemiBold)
        Button(onClick = viewModel::refreshSavedRecordings) { Text("刷新本地文件") }
        if (viewModel.savedRecordings.isEmpty()) Text("尚无本地录音", color = Muted)
        viewModel.savedRecordings.forEach { item ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Column(Modifier.weight(1f)) {
                    Text(item.filename, maxLines = 2)
                    Text("${item.sizeBytes} 字节", color = Muted, style = MaterialTheme.typography.bodySmall)
                }
                Button(onClick = { viewModel.playSavedRecording(item) }) {
                    Text(if (viewModel.playingRecordingUri == item.uri) "停止" else "试听")
                }
            }
        }
        Text(viewModel.playbackMessage, color = Muted, style = MaterialTheme.typography.bodySmall)
    }
    SectionCard {
        Text("实时音频", fontWeight = FontWeight.SemiBold)
        val canPlay = connected && viewModel.deviceInfo?.supportsAudio == true
        val running = viewModel.streamState == StreamState.STARTING || viewModel.streamState == StreamState.PLAYING || viewModel.streamState == StreamState.RECONNECTING
        Text(viewModel.streamMessage, color = if (viewModel.streamState == StreamState.ERROR) MaterialTheme.colorScheme.error else Muted)
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(onClick = { if (running) viewModel.pauseStream() else viewModel.startStream() }, enabled = canPlay) {
                Text(when {
                    running -> "暂停"
                    viewModel.streamState == StreamState.PAUSED -> "继续播放"
                    viewModel.streamState == StreamState.ERROR -> "重试播放"
                    else -> "播放实时音频"
                })
            }
            if (running || viewModel.streamState == StreamState.PAUSED) {
                Button(onClick = viewModel::stopStream, enabled = canPlay) { Text("停止") }
            }
        }
        if (viewModel.streamState == StreamState.PLAYING) {
            val stats = viewModel.audioBufferStats
            Text("网络缓冲约 ${stats.queuedMs} ms · 丢弃 ${stats.droppedBlocks} 块 · 欠载 ${stats.underruns} 次", color = Muted, style = MaterialTheme.typography.bodySmall)
        }
        if (!canPlay) {
            Text("需连接支持 48 kHz PCM 的以太网设备", color = Muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun FilterGainControl(viewModel: SoundFieldViewModel) {
    val available = viewModel.connectionState == ConnectionState.CONNECTED &&
        viewModel.deviceInfo?.supportsGainControl == true
    val currentGain = viewModel.filterGainDb
    val targetGain = viewModel.filterGainTargetDb ?: currentGain ?: 0
    val canEdit = available && currentGain != null && !viewModel.filterGainLocked
    var input by rememberSaveable { mutableStateOf("") }
    var invalid by remember { mutableStateOf(false) }
    LaunchedEffect(viewModel.filterGainTargetDb, viewModel.connectionState) {
        input = viewModel.filterGainTargetDb?.toString() ?: ""
        invalid = false
    }
    SectionCard {
        Text("设备增益 · 实时播放与录音", fontWeight = FontWeight.SemiBold)
        Text(
            when {
                !available -> "当前设备未声明增益调节能力"
                currentGain == null -> "正在等待设备当前增益；可点击重新读取"
                else -> "设备当前值：$currentGain dB"
            }, color = Muted
        )
        Slider(
            value = targetGain.toFloat(),
            onValueChange = { viewModel.setFilterGainFromSlider(it.roundToInt()) },
            onValueChangeFinished = viewModel::finishFilterGainDrag,
            valueRange = 0f..96f,
            steps = 95,
            enabled = canEdit
        )
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            OutlinedTextField(
                value = input,
                onValueChange = { next ->
                    input = next
                    val parsed = next.toIntOrNull()
                    invalid = next.isNotEmpty() && (parsed == null || parsed !in 0..96)
                    if (!invalid && parsed != null) viewModel.setFilterGain(parsed)
                },
                modifier = Modifier.weight(1f),
                label = { Text("增益 0–96") },
                suffix = { Text("dB") },
                singleLine = true,
                isError = invalid,
                enabled = canEdit,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
            )
            Button(
                onClick = viewModel::toggleFilterGainLock,
                enabled = available && currentGain != null
            ) { Text(if (viewModel.filterGainLocked) "解除锁定" else "锁定") }
        }
        if (invalid) Text("请输入 0 到 96 的整数 dB", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        if (available) Text(
            if (viewModel.filterGainLocked) "已锁定：滑动条和输入框不可修改" else "滑动或输入有效整数后立即发送到设备",
            color = Muted, style = MaterialTheme.typography.bodySmall
        )
        if (available) {
            Button(onClick = viewModel::refreshFilterGain, enabled = !viewModel.filterGainBusy) { Text("重新读取设备值") }
            Text(if (viewModel.filterGainBusy) "正在等待设备响应…" else viewModel.filterGainMessage,
                color = Muted, style = MaterialTheme.typography.bodySmall)
            viewModel.filterGainError?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
        }
        Text("高增益可能使 16 位音频削波。建议从 0 dB 开始，逐步增大并监听实时播放及检查录音。",
            color = Amber, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun SectionCard(content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = Panel)
    ) {
        Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp), content = content)
    }
}

@Composable
private fun MetricCard(label: String, value: String, modifier: Modifier = Modifier) {
    Card(modifier, shape = RoundedCornerShape(18.dp), colors = CardDefaults.cardColors(containerColor = Panel)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(label, color = Muted, style = MaterialTheme.typography.labelMedium)
            Text(value, color = Cyan, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun PolarPlot(frame: DemoFrame, onSourceClick: (Int) -> Unit) {
    val currentSources by rememberUpdatedState(frame.sources)
    val currentOnSourceClick by rememberUpdatedState(onSourceClick)
    Canvas(
        Modifier.fillMaxWidth().aspectRatio(1f).pointerInput(Unit) {
            detectTapGestures { tap ->
                val center = Offset(size.width / 2f, size.height / 2f)
                val radius = size.width.coerceAtMost(size.height) * 0.39f
                currentSources.firstOrNull { source ->
                    val marker = polarPoint(center, radius * 0.79f, source.angleDeg)
                    hypot(tap.x - marker.x, tap.y - marker.y) <= 28.dp.toPx()
                }?.let { currentOnSourceClick(it.angleDeg) }
            }
        }
    ) {
        val center = Offset(size.width / 2f, size.height / 2f)
        val radius = size.minDimension * 0.39f
        val grid = Color(0xFF294450)
        for (ring in 1..4) drawCircle(grid, radius * ring / 4, center, style = Stroke(1.dp.toPx()))
        for (angle in 0 until 360 step 30) {
            drawLine(grid, center, polarPoint(center, radius, angle), strokeWidth = 1.dp.toPx())
        }
        val target = polarPoint(center, radius, frame.targetAngleDeg)
        drawLine(Amber.copy(alpha = 0.85f), center, target, strokeWidth = 2.5.dp.toPx())

        val path = Path()
        frame.spectrum.forEachIndexed { index, value ->
            val point = polarPoint(center, radius * (0.13f + value / 1000f * 0.84f), index * 5)
            if (index == 0) path.moveTo(point.x, point.y) else path.lineTo(point.x, point.y)
        }
        path.close()
        drawPath(path, Cyan.copy(alpha = 0.16f))
        drawPath(path, Cyan, style = Stroke(2.dp.toPx()))
        drawCircle(Cyan, 4.dp.toPx(), center)

        frame.sources.forEach { source ->
            val point = polarPoint(center, radius * 0.79f, source.angleDeg)
            val color = if (source.id == 0) Cyan else Amber
            drawCircle(color.copy(alpha = 0.20f), 15.dp.toPx(), point)
            drawCircle(color, 6.dp.toPx(), point)
            drawCircle(Color(0xFF0C1821), 2.dp.toPx(), point)
        }

        val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
            color = android.graphics.Color.rgb(160, 183, 194)
            textSize = 12.sp.toPx()
            textAlign = android.graphics.Paint.Align.CENTER
        }
        drawIntoCanvas { canvas ->
            listOf(0, 90, 180, 270).forEach { angle ->
                val point = polarPoint(center, radius + 17.dp.toPx(), angle)
                canvas.nativeCanvas.drawText("$angle°", point.x, point.y + 4.dp.toPx(), paint)
            }
        }
    }
}

private fun polarPoint(center: Offset, radius: Float, degrees: Int): Offset {
    val radians = degrees * PI / 180.0
    return Offset(center.x + (radius * cos(radians)).toFloat(), center.y - (radius * sin(radians)).toFloat())
}
