# v0.1.0 — RTX 5070 Ti Laptop 12GB · Qwen3.8-27B · KVMem Local Agent

首个面向 Windows / RTX 5070 Ti Laptop 12GB 的公开预发布版，维护者 CSUNX233。基于 NInfer-all、NInfer、沈三殊 KVMem 方案及 NInfer2-Share-C 分享包，保留上下游归属与许可证。

**特别感谢 Bilibili 的 0xFFFF7：本项目使用的基础架构来自他提供的版本，死循环问题也由他解决。**

推荐默认 **xhigh**。维护者反馈 **200K 上下文稳定、未观察到明显降速，重复任务更快**，该日常使用观察与控制测速分别报告。仓库保留了维护者实测约 **7 分钟**生成的鹈鹕骑自行车 HTML 及预览。动态 MTP 也许能进一步提升 decode，**尚未验证，默认未启用**。

**本机 RTX 5070 Ti Laptop 12GB 已明确实测可用。** 推荐独显 + 核显混合模式，桌面与浏览器尽量用核显；无混合模式也可尝试，但需先关闭不必要的 GPU 进程并预留显存。其他 RTX 50 系 12GB 及以上显卡欢迎尝试，非 50 系需按自身架构重新编译；欢迎补齐适配和实测。脚本不自动更改系统显卡模式。

- 公开 C++/CUDA 引擎源码、Host RAM 检索索引、异步检索/预取、长缓存接管修复，以及 46 SM cc1200 指定量化矩阵调度。
- 256K 配置逻辑容量，36K Device KV、7 GiB Host KV、MTP 3；已验证 110K 输入和 62K+22K 生成后的工具/多轮续接。尚未测试满 256K 整模型稳定性。
- 固定短任务两轮 A/B：约 65–85 token/s；调度汇总解码吞吐 +2.44%。收益随任务变化；预填充与 TTFT 没有普遍加速证据。
- 提供固定版本安装、独立配置、启动/关闭、dsh Agent 网页、无付费 API 的搜索/正文提取和按需浏览器工具；可选 CPU 视觉有独立旧阶段实测。

**下载方式：** 下载本版本的 Source code ZIP 并解压，按 [README](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.0/README.md) 运行 `setup.ps1 -ModelPath <模型.ninfer>`。安装器会下载此处的引擎 ZIP，并从 NVIDIA/BtbN 下载哈希固定的运行依赖。

引擎 ZIP 只含程序及许可；不是包含模型的一键整包。权重按 [模型文档](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.0/docs/MODELS.md) 单独下载/转换。Node.js 24+、x64 VC++ 运行库与兼容 CUDA 13 的 NVIDIA 驱动需先安装。

`SHA256SUMS` 核验 Release 归档；引擎文件 SHA-256：`940e68278ae91c60e7ed8e70fc495efec1686fb16b7362f0002d6498e609413d`。更多数据、局限与复现脚本见 [实测报告](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.0/docs/BENCHMARKS.md)。

代码 Apache-2.0；dsh 及 vendored 组件保留各自许可；模型和专有运行库另行遵守其条款。此版是单机验证的社区下游发行版，不是上游官方发布，也不承诺所有硬件、任务或网页都能按相同性能工作。
