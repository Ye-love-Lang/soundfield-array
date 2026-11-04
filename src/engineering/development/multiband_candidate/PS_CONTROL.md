# PS 控制接入（0.20.0 候选）

候选完整源码在 `fw/`，正式 0.19.0 源码与备份未覆盖。空间滤波仍在 PL；PS 分步计算/上传系数，不处理实时八路卷积。此方案是几何模型的扇区抑制，不是基于实测协方差的 MVDR。目前只选择一个最强、已确认且距目标至少45度的干扰方向。

- 每次设计257个频点和8路IFFT，共265步；主循环每次执行一步。每轮上传最多32个系数。
- 干扰轨迹每100ms观察一次，连续3次同ID及相近方向才采纳；丢失1200ms后释放，干扰更新间隔至少500ms。目标附近不设置干扰零陷。这不改变 DOA 声源数量/ID。
- 已启用FIR后更新分8级插值，每级保持至少10ms，所有bank保持共同256采样延时；每通道定点系数和严格为0。首次启用前拒绝START_STREAM，避免在已播放时从旧通路跳到另一延时。
- 配置错误、FIR过载或提交超时进入故障状态并请求返回旧DAS；新的START_STREAM返回SPATIAL_NOT_READY。故障后需重新下载整套程序恢复，不自动反复重启。
- GET_FILTER_STATUS 保留旧字段，增加 spatial_supported/active/phase/target_deg/interference_deg/updates/failures/max_poll_ms。角度360表示未应用或无干扰。GET_STATUS的target_angle_deg仍是用户请求值；实际系数更新完成以spatial_target_deg及phase=1为准。
- phase：0无匹配硬件，1空闲，2设计，3插值，4上传，5等bank切换，6级间保持，7故障。

完整A9应用20个C源文件已用本机ARM GCC编译链接，无编译告警；build/build_manifest.json记录命令及输入SHA256。PS配置及AXI基址0x43C00000不变，沿用已验证BSP。host_stubs仅用于主机测试，不能加入板端固件。

整机独立项目为 `../sectorfir_pl/sf20.xpr`，sf20_synth/sf20_impl。已生成bit，最终已设时序约束满足：WNS0.155ns、WHS0.055ns；LUT16674、寄存器20304、BRAM36等效21、DSP24。沿用的外部I/O/CDC等方法学告警仍存在，不能说整个约束体系已全面完善。

候选重烧入口：`source {D:/FPGA_Learning/Work_project/soundfield_8mic/tools/run_0200_validation.tcl}`。脚本锁定配套bit/XSA/ELF/init的CRC，包含JTAG和串口启动诊断。它是易失JTAG下载，不是断电自启动。0.19.0的原恢复脚本和冻结备份继续可用。

30dB现场抑制、真实人声保真、方向切换听感、以太网连续播放和高增益尚须单独验收；编译、仿真和时序通过不等于这些现场指标已通过。
