# 构建与部署

以下操作以仓库的`src/engineering`为工作目录。

- ARM：`python build/rebuild_firmware.py --target all --tool-root <AMD2026.1安装目录>`。
- PL：Vivado打开`development/adaptive_raw8_0210/pl/sf21.xpr`，生成IP和BD输出产物；重建入口为`build/rebuild_pl.tcl`。
- Android：Android Studio打开`src/android`，JDK17、SDK35，使用Gradle wrapper的assembleDebug任务。
- 临时JTAG：Vivado Tcl Console执行`source tools/run_0212_validation.tcl`。
- 写Flash：先按板卡和AMD工具文档备份并核验本机Flash，在`development/qspi_boot_0212`准备自己的`flash_before_0212.bin`及`backup_verified.txt`核验记录；JTAG启动模式下执行`source tools/program_qspi_0212.tcl`，读回校验通过后断电切QSPI，再上电。仓库不提供可替代用户自身备份的历史镜像。

刷写前检查工具安装位置、串口、启动拨码及稳定供电。`development/qspi_boot_0212/build`为配套发布镜像；重建产物与已验收发布镜像分开管理。第三方工具和Android依赖由用户按项目配置安装，不随本仓库分发工具安装包。
