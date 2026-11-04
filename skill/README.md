# 技能包

本目录按指南3.3.5.2组织，供陌生队伍复用。入口为 verified-fpga-release/SKILL.md，不要求安装本项目或使用PYNQ；Python脚本仅依赖标准库。

|工作单元|适用范围与用法|失效条件|来源与验证|
|---|---|---|---|
|发布清单核验|Python 3.10以上执行 scripts/verify_manifest.py <root> <manifest.json>，格式为 files:[{path,bytes,sha256}]|清单过期、文件缺失、哈希变化或越过根目录即失败；不证明硬件性能|源于bit/ELF混用风险；有效样例通过、篡改单字节和路径逃逸样例拒绝，见validation.json|
|Vivado报告提取|执行 scripts/analyze_reports.py <utilization.rpt> <timing.rpt>，输出LUT/FF/BRAM/DSP与总时序|报告标题/表格格式不支持或字段缺失时停止；不能据正裕量断言CDC与外部约束完备|本作品0.19.0/0.21.0真实报告比对；不完整报告拒绝，见validation.json|
|提示词工作流|按references/prompts.md替换器件、接口和证据路径后交给模型|缺少输入或未执行工具，输出只能是方案，不可报验收通过|来自八麦分工、暂停测量干扰及固化过程；效果为明确证据与条件，不宣称跨任务通过率|
|踩坑案例|references/failure_cases.md按触发条件排查|没有当前场景确认、原始文件或独立验证时保持未确认|取自实际日志，并明确近场/混响/无播放、无效FIR和冷启动的适用边界|

本技能可用于其他FPGA板卡的交付一致性检查、资源时序表提取和证据管理。板卡管脚、声学阈值与寄存器地址必须自行配置，不能直接推广。本包未实现通用PYNQ Overlay或DMA封装，不申报PYNQ专项能力。
