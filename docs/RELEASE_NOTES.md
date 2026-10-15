# v0.1.1 — Qwen3.8-27B / RTX 5070 Ti Laptop 12GB：检索上下文与 Agent 续接修复

修复长/短会话切换、缓存检查点接管时检索索引可能属于旧上下文的问题，保留已验证的长上下文、量化与推理配置。源码和引擎 ZIP 同步发布；v0.1.0 资产保留原样供对照。

**特别感谢 Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)：基础架构来自他分享的版本，此前死循环问题也由他解决。** 本发行版由 CSUNX233 继续适配，保留 NInfer、KVMem、Qwen、ISTA-DASLab 和其他上下游的归属。

## 改动

- 检索索引、评分和采集工作区归 Program 所有，随 endpoint / rewrite / long-anchor / shared-prefix 检查点恢复索引与排名。普通 RAM 的 16 页共享分片以 COW 保存变化，避免每次复制整个索引。
- 按绝对 token 位置追加，修正跨页与半页处理，跳过已索引前缀的重复累加；评分使用每层真实 count，排除缺失页。评分版本改变时刷新 ring 选择。
- KVMem 内容检索当前明确要求单并发 + Legacy 缓存；不支持的 Hybrid / 多并发直接报错，避免静默串用状态。
- 缓存代理透传排队、等待响应和流式传输期间的取消；异常截断不伪造成功结束。文本截图结果后追加当前文件进度提示，引导继续验收，避免把保存路径误当成已看图；不改旧消息或静态系统规则，不强制结束任务。
- 公开 9 项框架回归与脱敏原生实测，并加入 CI；更新部署手册、版本和哈希、升级步骤及引擎打包工具。

## 实测与成本

RTX 5070 Ti Laptop 12GB / i7-14650HX / 32GB RAM，Windows，CUDA 13.0.48 / sm120a。保留 xhigh、生产采样、MTP 3、256K 逻辑容量、36K Device KV、7 GiB Host KV 和既有检索预算；未增加 GPU buffer。新增 RAM 索引快照满 256K 约 256.25 MiB / 不相关上下文，共享前缀复用分片，另需原活动索引空间。

- 约 49K 工具续接 TTFT 257 → 248 ms；切换新短会话后回到长会话 234 → 228 ms。3,341-token 新会话不再对旧上下文的 771 页打分，修复后为当前输入对应的 50 / 52 页。
- 62,346-token 输入后累计正常生成 43,775 tokens，再工具调用和三轮续接，四个验证码全部正确；工具结果只新算 52 tokens、TTFT 915 ms，后续三轮 399 / 411 / 382 ms。重复行夹具的 208–223 tok/s 不代表普通 Agent 速度。
- 固定短输出逐字相同；短文 66.57 → 67.50 tok/s，代码 86.26 → 84.61 tok/s，综合解码耗时约 +0.31%。49K 冷输入单次 TTFT 59.795 → 61.557 秒（约 +2.95%）。没有统计上成立的整体提速结论，不能承诺零开销。
- BF16 独立 sum/count oracle、FP64 评分 oracle（最大绝对误差约 3.01e-7）、预取生命周期、1350 项独立 ranking 检查、Context Store、数学/证明/结构化工具及框架检查通过。GPU 占用峰值对照均为 11,124 MiB。

未重放原私有约 177K 截图任务，不能保证所有重新规划都已解决。decode 生成历史尚未完整建立语义索引、完全缓存问句可能沿用同上下文排名、磁盘/跨进程恢复及满 256K 端到端压力仍需验证。构建为受影响单元增量重编并保留其余原静态库，没有声称干净全量构建或完整上游评测。未采用 ActKV 的有损淘汰。完整 [对照与证据](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.1/docs/BENCHMARKS.md#原生检索上下文修复2026-10-15)。

## 下载 / 升级

下载本版本 Source code ZIP，按 [安装说明](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/blob/v0.1.1/docs/INSTALL.md) 执行 `setup.ps1 -ModelPath "<现有模型.ninfer>"`。已有用户先等待任务结束，用本安装 `stop.cmd` 关闭、更新源码，再重跑安装器；只更新源码不会替换旧 exe。保留自己的配置、模型和会话，不必重新下载模型。

引擎 ZIP 只含程序和许可；安装器从 NVIDIA/BtbN 获取原固定依赖。需要 Windows x64、兼容 CUDA 13 的驱动、x64 VC++ 运行库，dsh 需要 Node.js 24+。推荐核显渲染桌面/浏览器，启动前清理不必要的独显占用，**仅为引擎程序禁用 CUDA 系统内存自动回退**，保留引擎主动管理的 7 GiB Host KV。其他 50 系 12GB 以上可尝试，非 50 系需自行编译架构。

引擎 SHA-256：`34fd593d76bd332c1b8470ce5ffe4fd9e7e9820229558cffd1cf9ddaa386224b`。归档校验值见附件 `SHA256SUMS`。

代码 Apache-2.0，第三方组件、模型与外部依赖分别遵守原许可。本版为单机验证的社区预发布版。推荐 xhigh；动态 MTP 是尚未验证、未启用的后续方向。
