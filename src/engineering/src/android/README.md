# 声场感知与选择性拾音 Android 上位机

当前版本可离线运行本地 Demo，也可通过以太网 TCP 7000 连接遵循 NDJSON v1 协议的设备。电脑模拟服务提供 TCP 控制、实时音频和 HTTP WAV 下载测试。界面会区分本地演示、已连接的模拟设备和真实设备。

给另一个 Codex 对话开发 Zynq 固件时，请先读取 [Zynq 联调交接文档](ZYNQ_INTEGRATION_HANDOFF.md)：其中按基础控制、角度、手机 PCM 录音和旧设备 WAV 列出了**何时即可开始联调**及 Android 当前接受的字段。尤其注意角度约定为 **0° 正右方、逆时针递增**，手机录音不依赖 Zynq 整段 RAM 存盘。

## 当前功能

- Jetpack Compose 竖屏主界面、通信和录音页面骨架。
- 72 点、每点间隔 5° 的动态极坐标声场图；0° 在正右方，逆时针增加。数值是 0–1000 的相对声能。声场采样、声源方向与拾音目标角度均按此坐标约定显示和发送，Zynq 端需使用相同角度约定。
- 两个模拟声源；点击图中标记或列表可改变演示拾音方向，也可用滑块或 0–359° 数字输入。
- 可编辑并保存设备 IPv4 地址及控制、音频、HTTP 端口；默认分别为 `192.168.7.1`、`7000`、`9000`、`8000`。可以自动选择手机识别的以太网接口、指定接口或使用系统默认路由。
- TCP 控制实现 `PING`、`GET_INFO`、`GET_STATUS`、`GET_SPECTRUM`、`GET_SOURCES`、`SET_ANGLE`、`LIST_RECORDINGS`、`START_STREAM`、`STOP_STREAM`；支持增益的以太网设备还可使用 `GET_FILTER_STATUS`、`SET_FILTER_GAIN`。协议解析器仍认识旧固件 `START_RECORD`/`STOP_RECORD`，当前手机录音按钮不发送这两条命令。
- TCP 响应按 `id` 匹配；支持拆包、粘包、2048 字节限制、超时、断线和异常 JSON 错误。连接后约每 400 ms 刷新状态、声场和声源。
- 实时播放先发送 `START_STREAM`，校验响应是 48 kHz、单声道、16-bit S16LE，再通过设备返回的端口接收原始 PCM 并由 `AudioTrack` 流式播放。网络接收与播放写入分别在后台执行；按 10 ms 分块，允许 TCP 任意拆包和奇数字节边界。启动预缓冲约 100 ms，播放队列上限 500 ms；音频断线会尝试重新建立试听流。单独试听暂停会关闭音频流，继续时重新握手；录音期间试听暂停或停止不会关闭共享 PCM 流。
- 「录音」页点击“开始手机录音”后，手机通过 `START_STREAM` 接收同一 TCP PCM，边接收边写本地 48 kHz 单声道 16-bit WAV，停止后完成文件头。Android 10+ 发布到“下载/SoundField”；旧版 Android 通过系统文件选择器指定位置。可同时试听并独立暂停试听；手机录音不受 Zynq 约 20 秒 RAM 文件上限影响。单个 WAV 限约 2 GB，设备或音频断线会结束录音并显示原因。若导出到下载目录失败，完整 WAV 保留在 App 私有目录并可在 App 内试听。
- 通信页只提供以太网连接；断开连接会关闭 TCP 控制 Socket、音频播放器和正在进行的 PCM 流。
- 「录音」页可调节设备用于实时播放和录音的增益：仅在设备声明 `capabilities.gain_control=true` 后启用，连接时读取当前 0–96 dB 值；解锁后滑动条或数字输入一旦改变为有效整数就发送设置，连续拖动只保留最新待发送值，并显示设备回传值或错误。点击「锁定」后不能修改。高增益可能使 16 位音频削波；旧固件未声明能力时禁用此控件。
- 设备旧版 WAV 可选兼容：以太网下若 `file_download=true`，通过 `LIST_RECORDINGS` 取得 ID，再从 HTTP 8000 下载 WAV。Android 10 及以上保存到系统“下载/SoundField”目录，旧版 Android 使用系统文件选择器指定位置；同名下载会生成不同文件名。完成前校验 HTTP 长度、列表中的文件大小、RIFF/WAVE 结构及 48 kHz 单声道 16-bit PCM 格式。失败或取消时删除未完成文件。已保存文件可在 App 内离线试听，重启 App 后仍显示。
- HTTP 明文只用于受控本地有线网络，下载 URL 由 App 根据用户配置的设备 IPv4 和录音 ID 构造，不接受外部跳转。

## 构建与安装

在 Android Studio 中打开本目录，使用 Android Studio 自带 JDK 和已安装的 Android SDK 35，同步 Gradle 后运行 `app`。命令行可执行：

```powershell
$env:JAVA_HOME='E:\Andriod Studio\jbr'
.\gradlew.bat :app:assembleDebug '-Pkotlin.compiler.execution.strategy=in-process' '-Pkotlin.incremental=false'
```

生成的 APK 位于 `app/build/outputs/apk/debug/app-debug.apk`。用 USB 连接启用开发者选项和 USB 调试的手机，在 Android Studio 选择该手机并运行 `app`，或用 `adb install -r app/build/outputs/apk/debug/app-debug.apk` 安装。录音改动已完成编译，真机和 Zynq 录音行为尚待用户按 `ACCEPTANCE.md` 验收。

## 电脑模拟服务联调

在 Windows 电脑运行以下命令（只使用 Python 标准库）：

```powershell
py -3 tools\mock_zynq.py --host 0.0.0.0 --port 7000 --audio-port 9000 --http-port 8000
```

通过 `ipconfig` 查找电脑与手机可互通的 IPv4 地址，在 App「通信」页输入该地址，选择对应网络接口并点击「连接设备」。电脑防火墙需允许此程序在当前局域网接受 TCP 7000、9000 和 8000 连接。连接成功后应显示“模拟设备已连接 · 模拟数据”，声场与声源随模拟服务变化；点击声源或应用角度后，设备状态显示新目标角度；「录音」页可开始手机 PCM 录音、停止并保存 WAV，也可点击“播放实时音频”收听 440 Hz 正弦波。设备自带的 2 秒测试 WAV 仍可下载并试听。播放时页面显示网络缓冲、丢弃块数和 `AudioTrack` 欠载次数；断开服务后，App 应回到本地 Demo，点击「重新连接」可恢复。电脑模拟服务的自动测试命令：

```powershell
py -3 -m unittest discover -s tools -p 'test_*.py' -v
```

模拟服务的 `GET_INFO` 会报告 `stream_audio=true`、`file_download=true`、`gain_control=true`。`START_STREAM` 返回 48 kHz、单声道、16-bit S16LE 和音频端口；TCP 9000 只发送原始 PCM 字节。HTTP 8000 提供 `/api/recordings/{id}`；加 `--truncate-download-once` 可让第一次下载只返回一半文件，用于测试 App 的失败提示和清理。加 `--no-gain-control` 可模拟旧固件，加 `--reject-gain-once` 可测试设备拒绝设置后的提示。模拟服务在控制台打印增益读写命令。

需要测试音频断线重连时，在模拟服务命令后加 `--drop-audio-once-after-ms 500`，服务会只断开第一次 PCM 连接；App 应自动重建音频流。音频连接实际使用 `START_STREAM` 返回的端口，通信设置中的音频端口用于配置和核对模拟服务。

Android 模拟器也可使用 `adb reverse tcp:7000 tcp:7000`、`adb reverse tcp:9000 tcp:9000`、`adb reverse tcp:8000 tcp:8000`，然后在 App 中填入 `127.0.0.1` 并选择“系统默认路由”。这仅用于模拟器联调；真机与 Zynq 的直连仍需选择手机识别的 USB 以太网接口。初次试听请降低手机媒体音量。

## 验收

逐项操作和通过条件见 [验收表](ACCEPTANCE.md)。真实 Zynq 和手机 USB 网卡链路尚需实物联调；电脑模拟服务与 Android 模拟器可以先覆盖其余流程。
