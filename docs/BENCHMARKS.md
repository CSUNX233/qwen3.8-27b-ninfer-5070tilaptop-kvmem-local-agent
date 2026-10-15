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
