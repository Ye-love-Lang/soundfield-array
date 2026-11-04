# BX71 0.21.2 QSPI断电自启动

本目录用于将当前0.21.2应用固化到板载单片W25Q128（16 MiB）Flash。应用ELF与PL bit保持原样；新增支持QSPI的FSBL与启动镜像。Flash写入结果见flash_programmed_verified.txt，实际断电启动结果见cold_boot_verified.txt；文件未出现即相应阶段未确认，不能将生成BOOT.bin等同于自启动验收。

**2026-11-03实板结果：Flash写入与真实断电自启动均通过。** 用户确认切QSPI并断电上电后，只读脚本读回boot_mode=1、0.21.2-eight-mic-localpath、八路采集fresh及FIR active=1/failures=0。证据为test_results/qspi_coldboot0212_20261103_175433.txt。没有重新初始化PS或下载程序；正常使用保持QSPI拨码，上电即可运行，无需电脑或JTAG下载。此次网口link_up=0，未额外进行App重连与声学试听。

## 使用

首次写入或以后更新这份固定镜像：板子保持供电、JTAG连接，启动拨码均向上（JTAG模式）。在Vivado Tcl Console执行：

```tcl
source {D:/FPGA_Learning/Work_project/soundfield_8mic/tools/program_qspi_0212.tcl}
```

脚本锁定BOOT.bin/FSBL/bit/ELF的CRC；写入Flash偏移0，擦除工具按镜像长度处理必要扇区，不使用整片擦除。实际镜像4,345,932字节，工具按64 KiB扇区将写入区域向上取整至0x430000。执行期间保持供电和下载器连接，成功要求编程与读回校验均通过。

写入后，**断电**再切启动拨码。按BX71用户手册图19的观察方向（USB接口在左），QSPI为左侧拨码向上、右侧拨码向下；手册照片标号为1上、2下，部分资料使用0上、1下。认准对应MIO4低/MIO5高，勿只按不一致的编号猜测。重新上电后不再执行JTAG下载脚本，等待程序自行启动，再连接App 192.168.77.10:7000。

串口及JTAG仍接电脑时，可以运行仅查询的验证脚本：

```tcl
source {D:/FPGA_Learning/Work_project/soundfield_8mic/tools/check_qspi_boot_0212.tcl}
```

该脚本要求启动模式寄存器为QSPI=1，只读寄存器并发送串口查询，**不复位、不初始化PS、不下载bit/ELF**，避免用JTAG加载成功冒充Flash启动。实际断电动作需用户完成。正常自启动后不需要每次烧录，JTAG仅在开发/更新时使用。

## 文件和构建范围

- build/BOOT.bin：FSBL、原PL配置、原应用共三份输入。Bootgen将应用ELF的两个可加载段分开存储，因此共4个低层分区是正常现象。
- build/fsbl_qspi.elf：基于原平台FSBL与BSP，加入官方qspips_v3_15驱动和BX71单片QSPI配置；读Flash使用x1 fast-read路径。编程工具使用qspi-x4-single，这与FSBL读取宽度不同不构成镜像混用。
- src/ps7_init.c/.h/.tcl：由独立Vivado工程启用QSPI后生成。DDR/PLL初始化操作与原工程一致，增加MIO1～6、QSPI参考/CPU时钟等初始化；不修改声学PL数据通路。
- build/manifest.json：镜像及输入文件SHA-256、CRC与大小；hardware_flashed/cold_boot_validated已在真实冷启动验证后更新为true，并附两阶段日志路径。
- build/commands.json、build.log、ps_init_changes.json：直接调用已安装ARM工具的命令、过程和初始化差异。首轮板卡宏适配有编译错误，已修正后重编；不将首次失败记成成功。
- boot.bif：原工程固定路径下的镜像打包描述。

BOOT.bin SHA-256：a812062da05f94929c9eb9da9dd998e9023262de5f1114a41d51f1ad05bb3a76，CRC32=2772263000。

应用ELF SHA-256：262b799931ea34e8e9aceaf40e0d098297b0ad399558fe04175de2ee1a8fe00f。版本仍为0.21.2-eight-mic-localpath，固化不改变算法，也不代表新增局部标定已通过声学验收。

## 参数与边界

这里固化的是程序和PL配置。App选择的角度、增益、连接状态及RAM校准参数没有新增持久化；上电使用程序默认值。手机每次重新上电后仍需重新连接App。

原Flash曾通过只读助手导出flash_before_0212.bin，16,777,216字节，板端与电脑端CRC32均为2759631178，SHA-256为080acf35a507ac9849cfcba47dc2ad83e01b75663a516279c8b9d243b719643e。该文件内容全0；擦除前未用第二条读取路径独立确认，故CRC一致只能证明本次导出传输一致，不能承诺它能恢复原厂程序。不得把此文件当作已经验证可启动的恢复镜像。

已有0.20.2/0.21.1等工程版本与JTAG恢复入口保留。若临时调试，仍可在JTAG模式运行旧的匹配下载脚本；这不会自动改变Flash中已写入的启动镜像。要恢复其他版本的断电自启动，必须重新打包并校验该版本的匹配BOOT镜像。

## 资料依据

本机BX71用户手册第14～15页、裸机教程第156～160页与原理图U4确认启动拨码、流程和器件。AMD启动流程说明见[Boot Image Creation（UG821）](https://docs.amd.com/r/en-US/ug821-zynq-7000-swdev/Boot-Image-Creation)。本次不涉及技术报告编写。
