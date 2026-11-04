# 构建与准备脚本

- prepare_workspace.py：创建可构建的工作副本，按case_map.json恢复工具所需文件名大小写，逐项验证复制内容；提交目录保持不变。
- rebuild_firmware.py：重新编译PS应用和FSBL。
- rebuild_pl.tcl：在Vivado中重建PL。

这些脚本服务于赛道要求的从零复现。报告制作过程的build_docx、collect_report、qa_report及排版检查文件均未放入正式提交目录。历史声学分析脚本保留在test_results中，作为算法迭代与验证证据。
