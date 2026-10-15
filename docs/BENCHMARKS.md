# 本机实测：条件、结果与边界

这些是单台机器的工程测量，不是跨机器能力排名。引擎保留既有模型权重、量化、采样与检索预算；质量检查支持指定优化路径的数值/行为一致性，不能证明模型不会出错或长上下文召回总是完整。

## 维护者实测反馈与作品

**200K 上下文：维护者反馈日常任务依旧稳定、没有观察到明显降速，重复任务更快。** 此反馈由 CSUNX233 在发布整理时提供；未附相同任务、功耗、冷/热状态的分项日志，因此不将它转换成精确 token/s、不声称“任何上下文均不降速”，也不替代满 256K 的整模型压力测试。

**鹈鹕骑自行车：实际模型生成 HTML，维护者报告约 7 分钟。** [保留原文件](demo/tihuriding.html)，可下载后用浏览器打开；[样例记录](demo/README.md)。这不是本次重新计时的运行，原提示词、输入/输出 token 数和当时采样未提供；7 分钟不换算成解码吞吐。项目推荐 **xhigh 思考**，已作为默认配置。

基础架构来自 Bilibili **0xFFFF7** 提供的版本，此前死循环问题亦由他解决；后续本机优化与这些贡献分别列明。

## 环境

| 项目 | 实际配置 |
|---|---|
| GPU | NVIDIA RTX 5070 Ti **Laptop** 12GB，46 SM，cc1200 / sm120a |
| CPU / RAM | Intel i7-14650HX / 32GB |
| 软件 | Windows x64，NVIDIA 驱动 617.14；CUDA 13.0.48，MSVC 14.44.35207 |
| 模型 | Qwen3.8-27B GSQ-RCO IQ3_XXS，带 MTP，转换为 v3 `.ninfer` |
| 上下文 | max-context 262144；Device KV 36864；Host KV 7168 MiB；rk4v4 |
| 检索 | recent 19456，sink 12288，retrieve 3840；RAM 索引 256.25 MiB；双 scratch 共 32 MiB |
| 执行 | 并发 1，prefill chunk 128，MTP 3，CUDA Graph allowance 64 MiB，gdn-state-fp16 |
| 常规默认采样 | reasoning xhigh；presence 1；思考后 temp 0.7 / top_p 0.95 / presence 1 |
| 短吞吐测量 | 请求显式 temperature=0、seed=1234、reasoning_effort=none；写作 768、代码 1024 输出上限 |

测量中的旧版是**本轮 CUDA 调度优化之前**、已经包含 Host KV 和检索修复的生产程序。不能把 +2.44% 解读成相对原始 NInfer 的全部收益，也不能把不同日期、池预算和 workload 的结果合并成统一提速倍率。

## 最新解码调度 A/B

使用同一新版程序，通过启动前 `NINFER_VEC5070_DISABLE=1` 关闭新调度，分别做两轮。每轮预热后按相同顺序：写作 → 代码 → 重复写作 → 重复代码。表格为两轮该项速率的算术均值；汇总提升用相同总生成 tokens / 总解码时间计算。

| 请求 | 关调度 token/s | 开调度 token/s |
|---|---:|---:|
| 写作 | 67.22 | 71.81 |
| 代码 | 83.67 | 84.49 |
| 缓存重复写作 | 64.56 | 65.16 |
| 缓存重复代码 | 83.69 | 85.00 |

总解码时间约 95,482.40 → 93,204.08 ms，汇总吞吐 **+2.444%**。小样本受时钟、温度、功耗和顺序影响，不提供统计显著性或普遍提升承诺。

数值验证：独立 CPU 解包实际量化权重，FP64 oracle；Linear / LinearAdd / SwiGLU 每模式 42 项检查通过。开关两种模式的 **3,686,400 bytes FP32/BF16 输出逐位相同**。短任务消息、用量、MTP 统计一致；数学、xhigh 思考和工具调用样例对照一致。一个数学夹具在关闭思考时原模型本就会答错，最终对照使用生产 xhigh；不把错误样例藏成“模型能力已保证”。

原始两轮数值、检查条件和所有逐项结果：[`decode-5070/verification.json`](../benchmarks/evidence/decode-5070/verification.json)；[算子结果](../benchmarks/evidence/decode-5070/qualification.json)；[qualification 源码](../benchmarks/qualification/qualify_vec.cpp)。调度仅在 cc1200、SM46 和匹配形状启用；没有新增 GPU buffer。

## 62K 相同请求顺序、干净启动对照

| 指标 | 原生产版 | 新版 |
|---|---:|---:|
| 冷 prefill token/s，62002 输入 | 571.86 | 557.17 |
| 冷 decode token/s | 71.97 | 76.87 |
| 缓存重复 decode token/s | 71.43 | 76.18 |
| 缓存重复 TTFT | 184 ms | 234 ms |
| 召回请求 TTFT | 650 ms | 830 ms |

每项仅一次，消息和用量逐字相同。**这轮优化没有证明预填充或 TTFT 更快。** 早期基线先跑了短请求，导致长文措辞不同；此表使用补做的同序列 clean-start 对照。

## 长生成 → 工具 → 多轮续接

最新压力流程：62,346 tokens 输入，正常生成 22,528 tokens，不设置 ignore_eos；随后强制真实结构化 `read_file` 调用，再写入工具结果并连续追问三轮。工具结果阶段需要正确报告头、中、尾以及工具新增的四个验证码。

- 工具调用请求：总输入 84,925，命中 84,873，**新算 52 tokens**；prefill 813.5 ms，TTFT 1234 ms。
- 工具结果及随后三轮：四个验证码全部正确；TTFT 313–445 ms。
- 观察峰值显存约 11,122 MiB，0.5 秒采样；不能排除未采到的瞬时峰值。

长输出使用固定重复文本以稳定触发页生命周期问题，draft 接受率接近 100% 时出现 221.55 token/s。这是**合成缓存压力夹具**，不代表真实 coding Agent 的常规解码速度。真实工具续接与普通写作速度分别报告。

## 较早的 110K 和检索测量

在 Host RAM 索引、36K 页池配置下，110,019-token 冷输入单次记录：prefill 620.48 token/s，约 177.31 秒，decode 69.28 token/s；一次 110K 工具续接 prefill 约 460 ms。这些发生在最新小矩阵调度之前，不冒充当前 Release 的重复测量。

检索双缓冲计时：56K+ 历史区间，每 chunk 的 16 层索引上传暴露阻塞中位数约 5.71 → 0.50 ms；这是带探针的内部阶段测量。无探针端到端冷 prefill 一次 576.35 → 586.71 token/s，不能把内部阶段约十倍下降宣传成全模型十倍提速。[检索预取证据](../benchmarks/evidence/index-prefetch/verification.json)

Host 索引初版为单 scratch 16 MiB，后续双 scratch 32 MiB，最终净显存节省为 224.25 MiB；旧阶段记录中约 240 MiB 的数字不适用于最终双缓冲版本。[索引证据](../benchmarks/evidence/index-offload/verification.json)

## dsh 工具验证执行规则（2026-10-15）

这是 dsh 提示词层的独立试验，不是引擎解码提速。使用实际 dsh 命令行、隔离的会话目录和同一生产模型路由，保持 **xhigh / 73728 输出额度 / 原采样**。任务是在预置 Python stub 中实现 `parse_duration(text)`，随后实际执行 `python -m unittest -v`；支持不重复的非负整数 h/m/s 分量、大小写及顺序变化、分量间空白，拒绝非法语法和错误类型。两轮验收文件哈希相同，7 项测试方法均通过。

| 指标 | 规则未启用参考轮 | 规则启用轮 |
|---|---:|---:|
| 首次工具调用 | 11.016 s | 11.031 s |
| 首次写文件 | 31.874 s | 45.283 s |
| 总任务时间 | 63.074 s | 53.747 s |
| 工具调用次数 | 10 | 5 |
| 总输出 tokens | 3714 | 3256 |
| 思考字符数 | 7383 | 7632 |
| 验收测试方法 | 7 通过 | 7 通过 |

最初一次测试因命令行没有 preset header，规则没有进入实际请求，因此明确作为**未启用参考轮**。修正 provider 识别后，启用轮确认系统提示实际包含该规则且仅一次；工具序列为 glob → read → read → write → pwsh → 正常结束。真实提示词注册检查通过：网页 preset、命令行本地 provider、其他路由/辅助调用排除、重复装配文本一致、卸载及重载不重复。

**每种条件仅一次，采样随机性、缓存与启动耗时存在差异。** 启用轮首次写文件更晚，总任务时间更短，不能据此宣称稳定提速、有效思考完全不受影响或一般任务质量等价；思考字符数也不是思考质量指标。规则不设思考上限，不改采样与输出额度，不额外请求模型；固定系统规则实测增加 272 提示 tokens，首次启用可能影响原会话前缀复用。

[脱敏验证摘要](../benchmarks/evidence/tool-grounded-execution/verification.json) 仅包含该人工夹具的统计和检查结论，不公开用户会话、本机配置或原始日志。[接入与停用](../integrations/README.md#工具验证与分步执行)。

## 截图续接与 KV 索引审计（2026-10-15）

在同一生产引擎上执行人工构造的长档案 → 结构化截图工具调用 → 文本工具结果 → 最终 JSON，再重复续接并切换新短会话。截图工具为预置返回的夹具，没有真实打开浏览器或传入图片。使用 xhigh、原采样和原 36,864-token Device KV / 7 GiB Host KV；**仅这些测试请求的输出额度为 4096**，未修改日常 73728 配置。生成输出仅 85–162 tokens，不能代替前述长生成压力测试。

| 请求 | 输入 tokens | 缓存命中 tokens | 客户端首 token | 结果 |
|---|---:|---:|---:|---|
| 长输入首次工具调用 | 49,101 | 0 | 63,850 ms | 正确调用预置截图工具 |
| 长输入工具续接 | 49,316 | 49,262 | 284 ms | 三个验证码、验收阶段和交付动作正确 |
| 重复同一续接请求 | 49,316 | 49,311 | 134 ms | 三个验证码、验收阶段和交付动作正确 |
| 全新短会话工具调用 | 3,341 | 0 | 3888 ms | 正确调用预置截图工具 |
| 短会话工具续接 | 3,547 | 3,499 | 197 ms | 三个验证码、验收阶段和交付动作正确 |

长续接实际接管了 **202 个 Host-backed 页面**，主模型需要 770 页而 GPU 池为 576 页；没有记录页池耗尽、恢复失败或索引禁用。长续接和重复续接均未产生新的语义 `SELECT`，沿用此前打分。短会话的打分仍报告 `n_blocks=771`（约 49K），而该输入仅需约 53 页，复现了索引范围未随当前上下文切换的问题。源码使用进程级索引单例，首次创建后只累加 sum/count，缺少 continuation 归属与恢复；`oracle_ok=1` 仅表示分数归一化自检通过，不能证明索引归属或召回正确。

以上五次请求均成功，不证明所有 KV 路径正确，也不能认定索引问题就是原约 177K 任务重新规划的唯一原因。这是修改引擎前的审计基线，后续原生修复见下一节。独立的真实取消测试通过缓存代理断开生成，在客户端观测到约 **104 ms** 内引擎回到空闲，引擎记录 HTTP 499 / client disconnected。截图续接提示及代理生命周期另有 9 项离线回归通过；原 177K 模型行为未重放。[脱敏摘要](../benchmarks/evidence/screenshot-kv-audit/verification.json)。

## 原生检索上下文修复（2026-10-15）

本地修复索引与缓存上下文归属、检查点恢复、绝对 token 位置的跨页追加和重复前缀去重；评分从每层真实 count 归一化，未建索引的页不参与 softmax；ring 选页在评分版本变化时更新。索引与采集工作区归 Program，缓存检查点保留索引及排名，RAM 按 16 页分片 COW。保持 xhigh、生产采样、量化、MTP 3、Device KV 36,864 tokens、Host KV 7 GiB、sink/recent/retrieve 以及 5070 Ti 小矩阵内核调度。未新增 GPU buffer；新增普通 RAM 快照，完整 256K 索引为 **256.25 MiB**，共享历史复用分片，不同会话需要独立片段。

同一机器上分别干净启动旧引擎与修复引擎，使用前述合成夹具。49K 夹具使用 xhigh、生产默认采样和请求级 4096 输出额度。输出有随机差异，以下只比较客户端首 token 与语义检查；没有把总耗时差异解释为思考提速。

| 项目 | 旧引擎 | 最终修复引擎 |
|---|---:|---:|
| 49,101-token 冷输入 TTFT | 59,795 ms | 61,557 ms |
| 约 49K 工具续接 TTFT | 257 ms | 248 ms |
| 重复相同续接 TTFT | 143 ms | 133 ms |
| 全新 3,341-token 输入的评分范围 | 771 页 | 50 / 52 页 |
| 新短会话工具续接 TTFT | 183 ms | 185 ms |
| 从短会话切回约 49K 历史 TTFT | 234 ms | 228 ms |
| 固定短文 decode | 66.57 tok/s | 67.50 tok/s |
| 固定代码 decode | 86.26 tok/s | 84.61 tok/s |
| 本轮 GPU 总占用峰值 | 11,124 MiB | 11,124 MiB |

两边短文和代码为 request-only temperature=0、seed=1234、reasoning=none 的固定速度夹具，输出逐字相同；日常 xhigh 未改变。六步上下文切换中的全部验证码均正确，没有页池耗尽、恢复失败或索引禁用。冷输入最终一次比基线约慢 2.95%，先前候选重复约慢 6.6%；这组次序单次对照不足以证明系统性 prefill 回退或提升。短文/代码综合解码耗时约增加 0.31%，不宣称本次有端到端提速。

进一步使用 `benchmarks/run.py --mode agent`：62,346-token 输入，先生成 **21,247** tokens 自然停止，再续写 **22,528** tokens，累计 **43,775**，随后工具调用时输入 **106,219** tokens。工具结果及三轮续接四个验证码均正确；工具调用只新算 51 tokens，工具结果只新算 52 tokens，没有重新预填整个历史。工具结果 TTFT **915 ms**，其后三轮 **399 / 411 / 382 ms**；压力服务 GPU 峰值 **11,120 MiB**。重复行夹具使用请求级 presence=0、temperature=0、reasoning=none、seed=1234，没有 `ignore_eos`，重复输出受 ngram copy draft 加速，约 208–223 tok/s **不能作为普通任务 decode 宣传值**。

原生资格检查覆盖：16 层×4 KV heads×256 dim×4096 页的 Host/Device 索引、非对齐追加、半页补齐、稀疏覆盖、重复重放、COW/回滚、不同 Program 工作区隔离；BF16 表示输入与独立求和/count oracle 一致，评分相对 FP64 softmax oracle 最大绝对误差约 **3.01×10⁻⁷**。预取生命周期、1350 项独立 ranking oracle、异步评分调度、KV Context Store 和 9 项框架回归通过；数学、证明、结构化工具调用检查也通过。未重跑全量上游测试。

验证二进制 SHA-256：`34fd593d76bd332c1b8470ce5ffe4fd9e7e9820229558cffd1cf9ddaa386224b`。**此引擎随 v0.1.1 发布，本机运行版本与下载清单的 exe 哈希一致。** v0.1.0 旧资产保留原样供对照；源码、归档及逐文件哈希见下载清单和来源记录。[脱敏原生证据](../benchmarks/evidence/screenshot-kv-audit/native-verification.json)。没有重放原约 177K 私有任务，也没有证明所有“截图后重新规划”均由索引造成；decode 新历史全量建索引、完全缓存问句的重新评分、磁盘/跨进程索引恢复及满 256K 端到端压力仍是边界。

参考上游 [kvmem/kvmem 的固定版本](https://github.com/kvmem/kvmem/tree/0af10ece3d78edb905c62357e52a62147df69d58) 的上下文/尾块恢复契约，独立实现，没有复制上游源码。用户提供的 [ActKV 论文](https://arxiv.org/abs/2609.31395) 是有损压缩方案，文中平均为 FullKV 的 98.53% 准确率；本轮没有引入其淘汰或自适应缩减策略。

## 如何复现

安装后先停止其他模型，用 `start.ps1 -EngineOnly` 启动，等待 ready：

```powershell
python .\benchmarks\run.py --mode short --label on-a
```

关闭，再在**同一个 PowerShell 会话**中设置环境变量并重启，以重新构建对应 CUDA Graph：

```powershell
.\stop.cmd
$env:NINFER_VEC5070_DISABLE='1'
powershell -ExecutionPolicy Bypass -File .\start.ps1 -EngineOnly
python .\benchmarks\run.py --mode short --label off-a
```

两边各重复一次，从干净启动按相同顺序执行。恢复新调度时移除该进程环境变量后重启。`--mode long` 生成三段验证码的长文输入，执行冷、热和召回；`--mode agent` 使用合成重复输出重现长生成后的工具续接。默认生成结果写到被 Git 忽略的 `runtime/benchmarks/`。执行长测试前留出时间，避免与正在使用的 Agent 争用 GPU。

`timings.predicted_per_second` 是引擎解码速率，`prompt_per_second` 是实际新算 prompt 部分速率；TTFT 在客户端记录第一个非空文本/思考/工具 delta。不能用“总历史 tokens / 缓存恢复几百毫秒”计算预填充吞吐。

## 尚未验证

满 256K 的整模型长生成和多轮稳定性；不同显卡/功耗档位；大规模真实任务成功率；所有网页站点与 dsh 后续版本。KVMem 选择性检索不是全历史逐 token 注意力，少量验证码命中不能替代系统性的长上下文质量评估。

动态 / 自适应 MTP 可能继续提升 decode，但本预设尚未启用，也未进行速度、额外显存与质量对照。它目前是后续研究方向。
