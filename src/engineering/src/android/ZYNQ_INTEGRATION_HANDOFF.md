# 给 Zynq 开发对话：Android 上位机联调交接

请先读取本文件，再实现或核对 Zynq 通信固件。本文描述**当前 Android App 实际接受的接口**和分阶段联调门槛，不表示 Zynq 端已经完成。Android 工程中的协议解析见 `app/src/main/java/com/example/soundfield/protocol/ControlProtocol.kt`，电脑模拟设备见 `tools/mock_zynq.py`，用户验收项目见 `ACCEPTANCE.md`。若固件已有不同字段或确需改变协议，请先提出差异并与 Android 侧对齐，不要单方面改名或改变数据含义。

> **录音架构更新**：用户要求录音 PCM 存在手机上位机。Android「开始手机录音」发送 `START_STREAM`，连接响应指定的 TCP 音频端口，把收到的 48 kHz/单声道/S16LE PCM 边接收边写入手机 WAV；「停止并保存」完成 WAV 头并发送 `STOP_STREAM`（若仍在实时试听，则保留共享流）。不再为此按钮发送 `START_RECORD`/`STOP_RECORD`，也不依赖 Zynq 的整段 RAM 录音、`GET_STATUS.recording`、`LIST_RECORDINGS` 或 HTTP 8000。当前已知固件 RAM 录音约 20 秒会自动停止，该旧路径不应作为手机录音的实现。Zynq 只需持续提供 PCM 流，RAM 仅用于短时传输缓冲。已有设备 WAV 下载作为兼容入口保留；固件是否修改尚需 Zynq 侧对话确认。

> **角度约定已由用户更新：0° 在阵列俯视图正右方，逆时针增加；90° 在上、180° 在左、270° 在下。** 这覆盖最初提示词中的“0° 正上方、顺时针”。App 不旋转协议角度：`GET_SPECTRUM` 第 0 点、`GET_SOURCES.angle_deg`、`GET_STATUS.target_angle_deg` 和 `SET_ANGLE.args.angle_deg` 必须使用同一新约定。若 PL 内部仍用旧约定，应在 Zynq 对外接口统一换算。

## 何时可以开始联调

| 门槛 | Zynq 端达到的状态 | 可立即测试的 Android 功能 | 暂时不要求 |
|---|---|---|---|
| A：基础控制与显示 | 手机能访问 Zynq PS 的 TCP 7000；一个连接可连续处理 NDJSON v1 请求；`PING`、`GET_INFO`、`GET_STATUS`、`GET_SPECTRUM`、`GET_SOURCES` 的字段和范围符合下表，连续运行不异常断开。声场数据此时可以由 PS 模拟生成，但所有相关 `simulated` 必须为 `true`。 | App 连接、设备信息、约每 400 ms 刷新的声场图和声源列表。**达到 A 就开始第一轮联调，不必等 PL 波束形成、音频或存盘完成。** | TCP 9000、HTTP 8000、真实声源定位。 |
| B：角度控制 | 在 A 基础上实现 `SET_ANGLE`；命令成功后 `GET_STATUS.target_angle_deg` 立即反映目标角度。 | 点击声源、手动设置角度与状态回读。 | 实时 PCM 和 WAV 下载。 |
| C：实时音频与手机录音 | 在 A 基础上，真实输出已稳定时才把 `GET_INFO.capabilities.stream_audio` 设为 `true`；`START_STREAM` 返回精确格式和实际端口，TCP 音频端口可持续输出处理后的原始 PCM，`STOP_STREAM` 能终止并释放流。流需支持持续录制，不能约 20 秒后因旧 RAM 录音满而停止。 | 手机实时播放、手机 WAV 录制及离线试听；录音与试听共享 PCM 连接，可分别暂停或停止试听。 | Zynq 整段 RAM 存盘、HTTP 文件服务。 |
| D：旧设备 WAV 文件（可选兼容） | 若仍需设备存盘下载，在文件完成写入、ID/大小稳定且 HTTP 服务可访问后才把 `GET_INFO.capabilities.file_download` 设为 `true`；实现 `LIST_RECORDINGS` 和 HTTP GET。 | App 设备旧版 WAV 列表、下载到手机、完整性校验和离线试听。 | 手机端录音不依赖此能力。 |
| E：设备增益 | 以太网固件实现 `GET_FILTER_STATUS`、`SET_FILTER_GAIN`，并仅在两者可用时声明 `GET_INFO.capabilities.gain_control:true`；设备上电默认 0 dB，读写均返回实际 `gain_db` 整数 0–96。增益作用于设备输出的实时 PCM 和新录制的音频。 | 「录音」页连接后读取当前值；解锁后滑动条或输入有效整数即发送设置，显示设备回传值或错误。锁定后不能修改。 | 旧固件不声明该能力时 App 禁用控件。 |

能力可以分开上线。例如只达到 A/B 时，`GET_INFO.capabilities` 应明确返回 `{"stream_audio":false,"file_download":false,"gain_control":false}`。Android 因此不会启动 PCM、请求录音文件列表或读取/设置增益。旧固件缺少 `gain_control` 时 Android 也视为 `false`。`GET_INFO` 中采样率仍应报告系统约定的 `48000` Hz。真实 PL 数据未接入前请保持 `simulated:true`；只有数据确实来自实测链路时才改为 `false`。

## 联调前的共同条件

1. **网络**：Zynq 的 IP 可配置，默认测试值可用 `192.168.7.1`。手机经 USB-C 以太网与 Zynq 在同一网段，能访问设备 IP；App「通信」页可以指定手机识别的以太网 Network。控制 TCP 7000、音频 TCP 9000、WAV HTTP 8000 是默认端口；App 控制端口和 HTTP 端口可编辑，音频实际端口以 `START_STREAM` 响应为准。
2. **控制帧**：UTF-8 JSON，每条消息后接 `\n`；单条含换行不超过 2048 字节。TCP 是字节流，Zynq 必须自行处理拆包、粘包和多条连续请求。`v` 固定为整数 `1`；正整数 `id` 原样回传；请求有对象 `args`，成功响应有对象 `data`，失败响应有 `error.code` 和 `error.message`。不要把日志文字混入控制字节流。
3. **并发与时限**：App 同一控制连接可能同时存在轮询与用户命令，请按每个请求的 `id` 回应。App 单个命令默认约 3 秒等待；连接建立约 4 秒等待。连续轮询依次请求 `GET_STATUS`、`GET_SPECTRUM`、`GET_SOURCES`，一轮结束约 400 ms 后再开始下一轮。
4. **数据含义**：声场 `values` 是 0–1000 的相对声能，不是 dB SPL。角度范围 0–359；采样点依次对应 0、5、10、…、355°。声源数量可以为 0，但字段和数组仍须存在。

## Android 当前必需的响应字段

| 命令 | `data` 中需要的字段及约束 | 备注 |
|---|---|---|
| `PING` | 建议 `uptime_ms`；Android 只要求成功响应的 `data` 是对象 | 连接后第一条命令。 |
| `GET_INFO` | `device_name` 字符串、`firmware_version` 字符串、`protocol_version:1`、`sample_rate_hz:48000`、`simulated` 布尔值、`capabilities.stream_audio`、`capabilities.file_download`、`capabilities.gain_control` 布尔值 | Android 严格校验协议版本、采样率和 `simulated`。`gain_control` 缺失或 `false` 时「录音」页增益控件禁用。 |
| `GET_STATUS` | `target_angle_deg` 整数 0–359、`recording` 布尔值、`streaming` 布尔值、`simulated` 布尔值 | 启动后持续轮询；角度和录音命令后再次请求。 |
| `GET_SPECTRUM` | `step_deg:5`、`values` 为**恰好 72 个**整数且每个在 0–1000、`simulated` 布尔值 | 单条完整响应不得超过 2048 字节。 |
| `GET_SOURCES` | `sources` 数组、`simulated` 布尔值；数组项含 `source_id` 非负整数、`angle_deg` 整数 0–359、`strength` 整数 0–1000 | 无声源时返回 `"sources":[]`。 |
| `SET_ANGLE` | 请求 `args.angle_deg` 为整数 0–359；成功时返回对象 `data`，并使下一次 `GET_STATUS.target_angle_deg` 与设置值一致 | 用户可点击声源或输入任意角度。 |
| `START_RECORD` / `STOP_RECORD` | 旧固件命令，可保留 | 当前 Android 手机录音按钮不发送这两个命令；无需为手机录音实现。 |
| `LIST_RECORDINGS` | `recordings` 数组；每项包含非空 `id`、非空 `filename`、非负整数 `size_bytes` | 仅以太网且 `file_download=true` 时请求。无文件返回空数组。 |
| `START_STREAM` | `sample_rate_hz:48000`、`channels:1`、`bits_per_sample:16`、`encoding:"S16LE"`、`port` 为实际 TCP 音频端口 | 仅以太网且 `stream_audio=true` 时请求；格式不符 App 拒绝播放和手机录音。 |
| `STOP_STREAM` | 成功响应的 `data` 是对象 | 停止或重建音频流时发送。 |
| `GET_FILTER_STATUS` | 成功响应 `data.gain_db` 为整数 0–96 | 仅以太网且 `gain_control=true` 时在连接后读取一次，也可在「录音」页手动重新读取；不自动改变设备增益。 |
| `SET_FILTER_GAIN` | 请求 `args.gain_db` 为整数 0–96；成功响应 `data.gain_db` 为设备实际采用的整数 0–96 | 解锁后滑动条改变或输入框出现有效整数时自动发送；连续快速修改时 App 最多同时等待一条增益响应，期间只保留最新待发送值。锁定后不再产生新设置命令；不接受时使用标准失败响应 `error.code`、`error.message`，App 显示错误并恢复上一次已确认值。 |

统一响应例子（具体 `id` 应对应请求）：

```json
{"v":1,"id":1,"ok":true,"data":{"device_name":"Zynq-7020 SoundField","firmware_version":"0.1","protocol_version":1,"sample_rate_hz":48000,"simulated":true,"capabilities":{"stream_audio":false,"file_download":false,"gain_control":false}}}
{"v":1,"id":2,"ok":true,"data":{"target_angle_deg":90,"recording":false,"streaming":false,"simulated":true}}
{"v":1,"id":3,"ok":true,"data":{"sources":[{"source_id":0,"angle_deg":45,"strength":850}],"simulated":true}}
{"v":1,"id":4,"ok":false,"error":{"code":"INVALID_ARGUMENT","message":"angle_deg must be 0..359"}}
```

`GET_SPECTRUM` 的 `data` 还必须包含 `{"step_deg":5,"values":[72 个整数],"simulated":true}`；此处方括号内文字是说明，不是可发送的 JSON。可以直接对照 `tools/mock_zynq.py` 的实际响应。

增益命令示例（每条实际传输时都要加换行；`id` 只是示例）：

```json
{"v":1,"id":20,"cmd":"GET_FILTER_STATUS","args":{}}
{"v":1,"id":20,"ok":true,"data":{"gain_db":0}}
{"v":1,"id":21,"cmd":"SET_FILTER_GAIN","args":{"gain_db":30}}
{"v":1,"id":21,"ok":true,"data":{"gain_db":30}}
{"v":1,"id":22,"ok":false,"error":{"code":"INVALID_ARGUMENT","message":"gain_db must be 0..96"}}
```

设备应在 `SET_FILTER_GAIN` 成功返回前完成增益应用；返回实际采用值，不能只回显请求值。0 dB 为固件默认值，App 连接后只读取，不主动发送 0 dB。用户滑动时可快速出现多条顺序设置请求；固件须逐条按 `id` 回应。点击「锁定」会清除尚未发送的目标值，但已发送且正在等待响应的命令可能仍完成；App 会显示设备最终确认值。高增益可能导致 16 位 PCM 和 WAV 削波；Zynq 端需在量化/输出前考虑饱和，联调时从 0 dB 逐步上调并检查实时播放及新录音。模拟服务可用 `--no-gain-control` 验旧固件、`--reject-gain-once` 验错误显示。

## PCM 与 WAV 的额外门槛

- TCP 音频端口只发送**连续原始** 48 kHz、单声道、16-bit、有符号小端 PCM；没有 JSON、WAV 头或压缩。可按 10 ms/960 字节发送，但不得依赖 TCP read 的边界。开始流后需容许 App 再连接音频端口；停止或控制连接断开时应释放对应音频资源。
- 手机录音直接消费同一 TCP PCM 流，写入手机本地 WAV；不调用 `START_RECORD`/`STOP_RECORD`。Zynq 的音频发送不要绑定旧 RAM 录音的 20 秒容量或自动停止条件。持续送出的 PCM 必须按样本时间顺序排列，并在目标网络负载下避免环形缓冲溢出丢样；当前裸 PCM 协议没有序号，Android 无法检测固件内部丢样。TCP 断开、PCM 接收失败或手机空间不足时 Android 停止录音并显示原因。Android 单个 WAV 目前限约 2 GB。手机端录音期间可开始、暂停、停止实时试听，而录音仍继续。
- HTTP GET `http://设备IP:8000/api/recordings/{id}` 返回已完成的 WAV。`id` 来自 `LIST_RECORDINGS`，URL 中会转义；建议使用稳定且 URL 安全的 ID。Android 目前接受完整 RIFF/WAVE、48 kHz、单声道、16-bit PCM，要求实际字节数等于 `LIST_RECORDINGS.size_bytes`，若服务器提供 `Content-Length` 也必须相等，RIFF 头部长度必须吻合。文件仍在写入时不要列为可下载完成文件。
- `LIST_RECORDINGS` 与其他控制响应同样受 2048 字节上限约束。若预期文件很多，当前协议没有分页字段；先保持响应在限制内，分页方案需要双方确认后再实现。

## 最小联调顺序与交接信息

1. Zynq 侧先在电脑上用 TCP 客户端自测门槛 A：一次连接连续发送多条请求，确认每条都以 `\n` 结束、`id` 对应、72 点和布尔字段正确，并能运行至少 1 分钟。参考 `tools/mock_zynq.py` 的行为，不要把电脑模拟服务的结果当作 Zynq 实测。
2. 告知 Android 联调人员：Zynq IP/掩码、控制/音频/HTTP 实际端口、当前已达到 A–F 哪些门槛、`GET_INFO` 实际响应、示例 `GET_STATUS`/`GET_SPECTRUM`/`GET_SOURCES`，如支持增益还需提供 `GET_FILTER_STATUS`/`SET_FILTER_GAIN` 响应；同时提供 `simulated` 值、固件版本和尚未实现的命令。
3. 先完成 A 的连接与声场显示，再测 B 的角度，然后接 C 的持续 PCM 和手机录音；D 的设备文件为可选独立功能，增益 E 可在以太网基础连接稳定后单独测试。每一步按 `ACCEPTANCE.md` 对应项目记录实际结果；遇到失败保留原始请求/响应和端口日志。

**当前状态说明**：旧版 Android 曾验证控制、音频和设备 WAV 下载/试听；手机本地录音改动尚未真机或 Zynq 实测，真实 Zynq 与 USB 网卡直连仍待实物联调。Android 现只支持以太网通信，不再需要蓝牙模块或 SPP 固件。本文件是启动联调的目标条件，不是对 Zynq 当前进度的判断。
