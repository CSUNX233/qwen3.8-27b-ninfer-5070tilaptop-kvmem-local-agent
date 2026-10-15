# 本发行版的修改范围

基础引擎和大多数内核来自上游及原分享包；本页描述后续本机工作。逐项 patch 和验证记录在 `benchmarks/evidence/`。继承的 Windows/TMA、KVMem-ring、头部保留、MTP 取消恢复、one-dot 检索等修复也随源码保留，不将其归为本轮原创。

| 后续改动 | 目的 | 关键位置 |
|---|---|---|
| 检索索引移到 Host RAM | 释放显存，保留原 FP32 索引与打分数学 | `engine/src/core/mapped_host_buffer.*`、`ops/kvmem/mean_k_index.*` |
| 大缓存接管与页生命周期 | 共享不可变历史、尾页 COW、统一主模型/MTP/临时页预算、按需恢复、释放顺序 | `models/qwen3_5/program/storage/`、`planning/request_plan.cpp`、`transactions/materialization.cpp` |
| 检索分数异步回传 | 减少 GPU 结束后再启动 CPU 选页的串行等待 | `ops/kvmem/kvmem_select.*`、`kvmem_score.h` |
| 双 scratch 预取索引 | 一个缓冲打分时提前上传下一层索引；最终 scratch 共 32 MiB | `ops/kvmem/mean_k_index.*`、`execution/text.cpp` |
| 检索上下文与检查点 | Program 独占工作区；RAM 分片 COW 快照；索引与排名恢复；绝对 token 追加、真实 count 与选页版本 | `program_impl.*`、`storage/context.cpp`、`prefill.cpp`、`transactions/`、`ops/kvmem/` |
| 5070 Ti 小矩阵调度 | 只改变指定 IQ2_S / IQ3_XXS / Q2_K、T=1/4 的线程与行分组 | `engine/src/ops/linear/gguf/ggml_bridge_vec.cuh` |
| dsh 接入与公开部署 | 相对路径、隔离 home、固定依赖和哈希、严格守卫、搜索/浏览器、收尾提醒、工具验证执行规则 | 根目录脚本、`scripts/`、`integrations/` |

最新 CUDA 调度只匹配 compute capability 1200 且 46 SM 的设备以及实际验证过的形状，共 11 个路由；其他形状保留原调度。每行数学、量化解码、累加和归约顺序保持原样，无新增 GPU buffer。启动前设置 `NINFER_VEC5070_DISABLE=1` 可以关闭这项调度；必须重启，不能对已经捕获的 CUDA Graph 热切换。

未改变权重、主模型量化、rk4v4、context、MTP 3、检索选择预算、生产采样和默认 ngram copy draft。`--lm-head-draft` 没有启用，避免额外常驻 proposal 权重。没有启用本地 profiling 探针。

2026-10-15 增加的 dsh 工具验证执行规则属于提示词引导，保留 xhigh 和输出额度；不修改引擎、不保证解决所有死循环。接入方式与开关见 [集成说明](../integrations/README.md)，独立开发任务结果见 [实测报告](BENCHMARKS.md#dsh-工具验证执行规则2026-10-15)。CUDA 系统内存回退的禁用建议由用户在驱动的程序设置中手动配置，公开脚本不自动修改。

随后补充截图后的尾部验收续接信息，以及缓存代理的断连取消传播。静态系统规则、旧消息和模型参数保持原样；新增离线 HTTP 与真实 dsh 工具流水线回归测试。这些检查覆盖接入与生命周期行为，不能代替约 177K 原任务的模型行为复测。

代码从本机源码快照导出；已记录的修改文件有醒目修改说明，完整列表在 `SOURCE_PROVENANCE.json`。原工作部署是保留静态库后的增量重编译/链接，首版 v0.1.0 二进制 SHA-256 为 `940e68278ae91c60e7ed8e70fc495efec1686fb16b7362f0002d6498e609413d`；公开源码增加归属注释和部署文件。发布验证不冒充对所有上游测试的干净全量重跑。

2026-10-15 本地进一步安装原生检索修复，验证引擎 SHA-256 为 `34fd593d76bd332c1b8470ce5ffe4fd9e7e9820229558cffd1cf9ddaa386224b`。原始 KV 编码、采样、xhigh、MTP 3、Device/Host 页预算及 5070 Ti 内核调度保持原样；修正的是检索索引的状态语义。新快照在普通 RAM，未新增 GPU buffer。本轮源码、框架补丁、9 项回归测试和脱敏证据随 v0.1.1 发布，安装器下载相同哈希的新引擎；v0.1.0 资产不覆盖。此次以状态正确性和缓存续接稳定性为目标，没有统计上成立的总体提速结论。

参考 [kvmem/kvmem](https://github.com/kvmem/kvmem/tree/0af10ece3d78edb905c62357e52a62147df69d58) 的上下文截断、检索恢复与 tail checkpoint 契约，独立实现本引擎的 Program/COW 方案，没有移植该仓库源码。[ActKV](https://arxiv.org/abs/2609.31395) 使用面向动作的 KV 压缩；其平均准确率并非与 FullKV 等同，本轮没有采用淘汰或自适应缩减预算。验证与已知边界见 [实测](BENCHMARKS.md#原生检索上下文修复2026-10-15) 和 [部署手册](DEPLOYMENT_AGENT_GUIDE.md)。
