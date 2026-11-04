# 工程入口

本目录为交付工程根目录engineering。先阅读上一级readme.md中的环境、接线、JTAG、QSPI及源码重建步骤。

- development/directional_path_0212/fw：当前PS应用。
- development/adaptive_raw8_0210/pl/sf21.xpr：PL工程。
- development/qspi_boot_0212/build：配套发布BOOT/FSBL/bit/ELF。
- build：固件与PL重建入口、重建记录。
- src/android：Android Studio源码。
- app/SoundField.apk：手机安装包。
- tools：下载、固化和诊断脚本。
- test_results：实测数据与历史记录。

保持这些目录的相对位置。换机时修改tools脚本中的本机AMD工具安装位置及开发串口COM号；具体配置文件见上一级readme.md。
