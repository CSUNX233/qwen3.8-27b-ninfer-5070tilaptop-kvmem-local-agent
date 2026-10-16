# v0.1.2 — Qwen3.8-27B / 5070 Ti Laptop 12GB：生成历史索引与长上下文续接

长任务生成的新历史现在按实际提交的 token 补入语义索引，缓存问句在有新增历史时可重新评分；完全重复、恰好缓存到问句边界的请求保留原排名，避免无意义刷新造成召回退步。本版打包维护者当前使用且已验证的引擎，保留 rk4v4、xhigh、原采样和 MTP3。

**特别感谢 Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)：基础架构下载自他分享的版本，此前死循环问题也是由他解决的。** CSUNX233 在其基础上继续适配；保留 NInfer、KVMem、Qwen、ISTA-DASLab 及其他上下游的归属与许可。

## 优化与修复

- 普通 decode 和 MTP 验证后，只把已接受、已提交的主模型 pre-RoPE K 加入 mean-K 索引；拒绝草稿和 MTP 头不会污染主模型采集。处理零提交、跨页、重复提交与检查点恢复。
- 对缓存中的相同问句保留精确 token 身份和原 BF16 查询向量；合法前缀恢复且问句后已有历史时重新评分。缓存前沿恰好等于问句末尾时跳过刷新，保留完全重复请求的原评分域。不是对任何缓存重复都强制重排。
- 非评分分块恢复融合的 QK norm + RoPE，并在同一内核采集归一化、旋转前的 K，避免为建索引拆开算子；需要查询评分的分块保留原路径。
- 发布检索预算的分档实测和原生 FP64 / 精确位对照。安装脚本默认启用已验证的两项 rk4v4 修复，显式环境变量 `0` 可用于诊断对照。
- 增加部署顺序：先清显存、从完整较小页池组合起步，验收后再逐档增加；部署手册的可复制 Agent 提示词和根 AGENTS.md 都有此要求。

## 部署先从小池起步

**公开默认：DeviceKV 36864 / RetrieveTokens 3840。** 配套 sink 12288、recent 19456、Host KV 7168 MiB、rk4v4、MTP3、chunk128、xhigh。先完成启动、短任务、缓存和工具续接，再验证目标长度的召回。较小仅相对进阶档，不保证任意 12GB 设备直接可用。

维护者目前使用 **39168 / 6144**；该档 CUDA 启动可用约 **84 MiB**，适合先建立基线再尝试。不要直接复制这档到所有机器；也不能只缩小 DeviceKV，却维持装不下的 sink/recent/retrieve/MTP/临时页预算。整卡 `nvidia-smi` 空闲与 CUDA 可用量是不同口径。

强烈建议在 NVIDIA 控制面板的**实际引擎程序设置**里禁用 CUDA 系统内存自动回退，保留主动管理的 7 GiB Host KV。建议核显渲染桌面和浏览器；无核显也可在清理独显占用后尝试。非 50 系需自行编译相应架构。

## 本机实测及边界

RTX 5070 Ti Laptop 12GB / i7-14650HX / 32GB RAM，Windows，CUDA 13.0.48 / sm120a。以下是新引擎 **6144 / 39168**、两修复开启、xhigh / 原采样 / MTP3 的合成 API 夹具结果：

| 流程 | 结果 | 预填充 / 解码 |
|---|---|---|
| 74K 生成历史后 store → confirm → done | 通过 | 检查提交历史索引及工具续接 |
| 64K Agent：三任务、六次工具调用 | 通过 | 续接 prefill 396–679 ms，加权 decode 84.11 tok/s |
| 128K 首次 / 完全重复 | 5/5、5/5 | prefill 217.85 s / 128.03 ms；decode 55.84 / 76.70 tok/s |
| 200K 首次 / 完全重复 | **4/5、4/5** | prefill 424.62 s / 112.01 ms；decode 55.83 / 67.63 tok/s |

128K 首次输出 5412 tokens、96.91 s，重复输出 1994 tokens、25.99 s；200K 首次输出584、重复769 tokens。不能用热缓存耗时除总历史长度宣传冷预填充速率。128K / 200K 输出与同预算旧引擎对应输出逐字一致，这两组题没有证明修复额外提高准确率。200K 两次均漏 BRIDGE-039；默认3840档该题首次触及输出上限、重复2/5，不能宣传200K全召回。

6144档 runtime 1014.6 MiB，较3840档972.8 MiB多约41.8 MiB，主要是更大页预算；采样整卡峰值11158 MiB，没有观察到 OOM / 页池耗尽。查询缓存主要用RAM；chunk128下查询上传缓冲最多约1.5 MiB，不能称为绝对零显存成本。

原生验证通过30次 CUDA Graph变输入/位置重放、独立FP64归一化及精确Q/K位对照、Host/Device已提交历史索引oracle、缓存查询新评分与新鲜查询对照。单个seed重复不是独立样本；合成API验收不能替代真实自主dsh浏览器/视觉全链路。满256K及8192档长文未完成，NVFP4 KV未部署。**新一轮CPU增量检索、分页批处理、GPU注意力优化不在本次引擎包内，也没有预先宣称提速。**

完整 [实测与限制](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.2/docs/BENCHMARKS.md#语义历史修复与检索预算2026-10-16)；[安装与逐档预算](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.2/docs/INSTALL.md)。构建为受影响Host/CUDA单元增量重编并复用其余静态库，不声称干净全量构建。

## 下载 / 升级

使用本标签的 Source code ZIP，执行 `setup.ps1 -ModelPath "<现有模型.ninfer>"`。已有安装先完成任务，再用本安装`stop.cmd`关闭，更新源码并重跑安装器；模型无需重下，自己的配置和会话保留。只更新源码不会自动替换exe或改大页池。v0.1.0 / v0.1.1资产保留。

引擎ZIP只含程序与许可，安装器获取固定哈希的cuBLAS和LGPL FFmpeg运行库；不含模型、DLL、SDK、会话或登录凭据。Windows x64、CUDA13兼容驱动、VC++ x64运行库；dsh需要Node.js24+。

引擎SHA-256：`275ea27ba01b57ca3f3a14361e5f7a4d8c4b04575475f1fe536ca5dedb6c8bd1`；归档校验见附件`SHA256SUMS`。代码Apache-2.0；第三方与模型遵守原许可。本版为单机验证的社区预发布版。建议xhigh；动态MTP可能提升decode，尚未验证或启用。
