# SoundField — 八麦声场感知与选择性拾音

队伍40199 · 西安电子科技大学。基于BX71 Zynq-7020、八通道ICS-43434圆形阵列和Android以太网上位机。

系统以48 kHz同步采集八路声音，显示水平面相对空间谱与声源方位，支持手动方向选择、PL多通道FIR波束形成、耳机实时监听、手机WAV录音及QSPI断电自启动。

## 推荐目录

| 目录 | 内容 |
|---|---|
| `src/engineering/` | 保持内部依赖关系的Vivado、ARM、Android源码和配套镜像 |
| `sim/` | RTL测试平台说明 |
| `build/` | 从源码构建与下载入口说明 |
| `board/` | 麦克风阵列板与接线说明 |
| `data/` | FPGA资源、时序等可复核记录 |
| `skill/` | 可复用技能、验证脚本与失败案例 |
| `report/` | 最终PDF、图片及协作记录 |

工程内部保留原目录关系，避免仅为改变目录名称而破坏XPR、BSP和构建脚本依赖。本副本已恢复AMD、Android和技能入口所需的原始文件名大小写，可直接使用，无需再次执行提交包的prepare_workspace.py。

## 开始运行

1. 安装Vivado/Vitis 2026.1和JTAG/串口驱动。硬件接线见`report/design_report.pdf`表2；供电采用稳压5 V，IO按板卡3.3 V约束连接。
2. 将Vivado工作目录设为`src/engineering`。根据本机环境检查`tools`中AMD工具位置和串口号，串口115200、8N1。
3. 临时JTAG运行：两个启动拨码均向上，断电再上电，在Vivado Tcl Console执行`source tools/run_0212_validation.tcl`。
4. QSPI固化：首次写入前，按板卡与AMD工具文档备份并核验自己板卡的现有Flash，将16 MiB备份存为`development/qspi_boot_0212/flash_before_0212.bin`，并在同目录的`backup_verified.txt`中记录核验结果。该备份及记录由使用者自行准备，公开副本不附个人板卡备份。随后在JTAG模式下执行`source tools/program_qspi_0212.tcl`，等待写入及读回校验成功后断电。按BX71手册图19、USB接口在左的视角，左拨码上、右拨码下，再上电。日常QSPI运行无需重复烧录。
5. 安装`src/engineering/app/SoundField.apk`，手机USB-C网卡通过网线直连板卡。设备地址`192.168.77.10`，控制TCP `7000`，实时音频TCP `9000`。先设0 dB，用耳机监听；U1=90°、U3=180°，从声孔面俯视逆时针增加。

## 从源码重建

在`src/engineering`工作目录使用`build/rebuild_firmware.py`和`build/rebuild_pl.tcl`；完整说明见根目录`build/README.md`。Android Studio打开`src/engineering/src/android`，使用JDK17、SDK35及项目Gradle wrapper。

配套已验收固件为0.21.2，位于`src/engineering/development/qspi_boot_0212/build`。当前PS源码位于`development/directional_path_0212/fw`，PL工程位于`development/adaptive_raw8_0210/pl/sf21.xpr`。Vivado打开后生成IP/BD输出产物，再综合实现。

## 实现与结果

PL负责共享I2S时序、八路采集、28对多延迟互相关和八路512抽头FIR；PS负责方向扫描、多源轨迹、实际混合声音协方差和加载权重设计、lwIP传输。实际混合协方差加载方案属于MPDR类，具体实现见源码及报告。

已有0.20.2受控固定音基线：300～3200 Hz测试频点合计13.196 dB的180°/90°输出能量比。该指标对应报告所述基线条件，与相对单麦SIR是不同指标。完整0.5～3 m角度精度统计、同输入单麦SIR及长时间运行仍有待补充验收。间歇人声、强弱差和混响下的多源识别继续优化。

手机负责实时试听和本地WAV保存；设备端WAV下载当前未提供。实时外放可能形成声学反馈，演示使用耳机。增益、角度与RAM标定参数上电后重新设置。

公开副本省略历史开发工作日志、原始语音快照、个人调试串口记录和重复构建日志；完整赛道提交包单独保存。没有将省略的现场记录当作新增验收结果。

## 第三方说明

本项目原创部分采用MIT许可。AMD/Xilinx BSP、IP、生成代码及Android依赖保留各自版权与许可，本仓库的公开不改变第三方文件的许可条件。详见`LICENSE`和`THIRD_PARTY_NOTICES.md`。
