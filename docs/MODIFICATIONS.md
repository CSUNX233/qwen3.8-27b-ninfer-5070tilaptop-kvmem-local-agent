# 本发行版的修改范围

基础引擎和大多数内核来自上游及原分享包；本页描述后续本机工作。逐项 patch 和验证记录在 `benchmarks/evidence/`。继承的 Windows/TMA、KVMem-ring、头部保留、MTP 取消恢复、one-dot 检索等修复也随源码保留，不将其归为本轮原创。

| 后续改动 | 目的 | 关键位置 |
|---|---|---|
| 检索索引移到 Host RAM | 释放显存，保留原 FP32 索引与打分数学 | `engine/src/core/mapped_host_buffer.*`、`ops/kvmem/mean_k_index.*` |
| 大缓存接管与页生命周期 | 共享不可变历史、尾页 COW、统一主模型/MTP/临时页预算、按需恢复、释放顺序 | `models/qwen3_5/program/storage/`、`planning/request_plan.cpp`、`transactions/materialization.cpp` |
| 检索分数异步回传 | 减少 GPU 结束后再启动 CPU 选页的串行等待 | `ops/kvmem/kvmem_select.*`、`kvmem_score.h` |
| 双 scratch 预取索引 | 一个缓冲打分时提前上传下一层索引；最终 scratch 共 32 MiB | `ops/kvmem/mean_k_index.*`、`execution/text.cpp` |
| 5070 Ti 小矩阵调度 | 只改变指定 IQ2_S / IQ3_XXS / Q2_K、T=1/4 的线程与行分组 | `engine/src/ops/linear/gguf/ggml_bridge_vec.cuh` |
| dsh 接入与公开部署 | 相对路径、隔离 home、固定依赖和哈希、严格守卫、搜索/浏览器、收尾提醒 | 根目录脚本、`scripts/`、`integrations/` |

最新 CUDA 调度只匹配 compute capability 1200 且 46 SM 的设备以及实际验证过的形状，共 11 个路由；其他形状保留原调度。每行数学、量化解码、累加和归约顺序保持原样，无新增 GPU buffer。启动前设置 `NINFER_VEC5070_DISABLE=1` 可以关闭这项调度；必须重启，不能对已经捕获的 CUDA Graph 热切换。

未改变权重、主模型量化、rk4v4、context、MTP 3、检索选择预算、生产采样和默认 ngram copy draft。`--lm-head-draft` 没有启用，避免额外常驻 proposal 权重。没有启用本地 profiling 探针。

代码从本机源码快照导出；已记录的修改文件有醒目修改说明，完整列表在 `SOURCE_PROVENANCE.json`。原工作部署是保留静态库后的增量重编译/链接，Release 二进制 SHA-256 为 `940e68278ae91c60e7ed8e70fc495efec1686fb16b7362f0002d6498e609413d`；公开源码增加归属注释和部署文件。发布验证不冒充对所有上游测试的干净全量重跑。
