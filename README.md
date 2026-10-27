# NInfer + KVMem：12GB 显卡运行 27B 本地 Agent

在消费级显卡上运行 **Qwen3.8-27B 本地 Agent**，支持代码、文件与网页工具任务。NInfer 负责模型推理，KVMem 把历史 KV 保存在 RAM、按需调入 GPU，配合 MTP 推测解码、缓存守卫和可选 dsh 网页界面，减少长任务续接时的重复计算。逻辑上下文配置为 256K；不代表每步读取全部历史，也不代表已完成满 256K 验收。[English](README.en.md)

## RTX 5070 Ti Laptop 12GB 历史实测

实测机器：**RTX 5070 Ti Laptop 12GB / i7-14650HX / 32GB RAM / Windows**；模型为 Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP3。下表保留旧版 RK4V4 条件下的记录，**不是 v0.1.5 / NVFP4 重新测得的速度**。

| 任务 | 实测速度 | 测量条件 |
|---|---:|---|
| 短写作解码 | 71.81 tokens/s | 小矩阵调度开启，两轮算术均值 |
| 短代码解码 | 84.49 tokens/s | 同组短任务，两轮算术均值 |
| 缓存重复代码解码 | 85.00 tokens/s | 同组缓存请求，两轮算术均值 |
| 110,019-token 冷预填充 | 620.48 tokens/s，约 177.31 秒 | 更早阶段单次记录，随后解码 69.28 tokens/s |

短任务测速显式使用 temperature=0、seed=1234、reasoning=none，日常预设为 xhigh；功耗、任务、KV 格式和缓存命中都会影响速度。不同阶段记录不能拼成一次统一测试。[完整条件与历史实测](docs/BENCHMARKS.md) · [作品展示](docs/demo/README.md)

## v1.0.0 更新

**在 v0.1.5 的模型、NVFP4、39168/6144 页池、MTP3、xhigh、采样和回答惩罚设置不变的前提下，发布查询层与 agent 执行层升级。**

- **查询层（引擎开关，默认 legacy，启动器可显式开启）**：`related-tool-head` 选择最后用户之后与用户末段词法最相关的最新工具头；`suffix refresh=tail` 在 KV 映射前刷新；`QUERY_ASSEMBLE=1` 跨块收集完整查询（上限 512 token）并在同一历史域重评分。真实 116,907-token 故障历史完整目标路径/合法 read-edit 由 **1/4 → 4/4**（latest-tool-head 系列），同历史留出 8 seeds 由 **4/8 → 8/8**；因果四类 **8/8**；旧延迟压力首轮仍 **2/4**；六类短续接路径/严格 **10/12、7/12** 持平。
- **agent 执行层**：`edit-guard` 在单文件精确替换时执行前校验目标/动作/old-new，错误动作不执行、不静默改路径；`task-ledger` 在多动作任务想停时提醒剩余动作。结构化反馈后一次恢复 **10/10**（generic 0/10）；两个复杂任务由 **1/2 → 2/2**（三动作 move+edit+write 完成，0 额外改动）。
- **长文本**：500 行 / 1500 行编号文本在足够 `max_tokens` 下 1..N 连续、无缺行/重复/坏行；500 行/12288 的失败是 xhigh reasoning 占用预算，不是序列 bug。
- **边界**：不宣称“全面优于”。因果/短任务持平，旧压力首轮未提高；真实浏览器/游戏多步闭环、256K、其他显卡/模型未验收。H7 等权双查询、H8 固定事务页、H11 工具头扩展均已拒绝，不默认启用。私有原始请求/日志留在本机，公开汇总见[脱敏证据](benchmarks/evidence/path-query-v1.0.0/verification.json)和[路径查询实验](docs/PATH_QUERY_EXPERIMENT.md)。
- **安装/回退**：验证引擎 SHA-256 `71259a21f7b525ca7477193a292e4d3623cc58b9efd785005ee934cc29673507`；v0.1.5 原引擎 `38bc4d8c6be07afd6c74b902cc33403bf2ee139a2c50129d363d68d74a7609d6` 作为回退。源码标签 `v1.0.0`；本轮没有上传 1.35GB 引擎附件，按 [BUILD.md](docs/BUILD.md) 构建或使用本机升级包。详细说明见 [Release Notes](docs/RELEASE_NOTES.md)。

## v0.1.5 历史


**在 v0.1.4 上移植 C2.1，改善长任务的缓存恢复和 KV 回迁。** 保留工具解析、采样、NVFP4、FP16 GDN 和回答独立惩罚计数修复，默认仍为纯文字 + MTP、xhigh，不启用视觉。[下载 v0.1.5](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) · [更新说明](docs/RELEASE_NOTES.md)

## 显卡与模型选择

**经多位使用者实测反馈，RTX 30 系及以后、12GB 及以上显卡可以稳定运行本项目；16GB 卡可选更大的 IQ3_S 模型，通常能获得更高质量。** 这是社区使用反馈，需选择匹配显卡架构的引擎，并给系统、后台和 KV 留足余量，不是对每种设备/任务的逐卡保证。

| 硬件 | 引擎与模型建议 |
|---|---|
| RTX 30 / 40 系，12GB+ | 使用对应架构的兼容构建；参考[编译说明](docs/BUILD.md)，不能直接运行本次 sm120a 附件 |
| RTX 50 系，12GB+ | 本次 Windows x64 / sm120a 附件；直接实测设备为 5070 Ti Laptop 12GB |
| 16GB 及以上 | 可以选择更大的 IQ3_S 文字模型，另行准备含 MTP 的 `.ninfer` 并验证预算；本次完整版两份制品的文字权重仍是 IQ3_XXS |

推荐 32GB 系统内存、SSD，以及兼容 CUDA 13 的 NVIDIA 驱动。不同架构还需适配内核与依赖，不能只改 exe 文件名。

## 这一版改善了什么

- **减少长历史续接的重复预填充。** 在消息边界保留靠近尾部的滚动长锚点，同时保留有用的早期回退点；同 lineage 恢复时可消费旧 owner，保留可复用的不可变 KV/状态。
- **减少冷页逐页回迁的开销。** 合格冷页按最多 32 页批量回迁 Host；大块分配失败逐级缩批，允许部分推进，保留 sink、页顺序、来源 pin 和尾页约束。
- **基线明确回到 v0.1.4。** 保留上一版正确性修复，没有改变模型权重、KV 格式、注意力公式或检索窗口；这轮重点是缓存生命周期，不能据此宣称总体模型质量提升。
- **安装包拆成两种。** 无模型版支持自己的 `.ninfer`；完整版只含 GSQ-RCO 文字+MTP 和 Huihui 文字+视觉+MTP 两份制品，均自带引擎和用户态运行依赖。

| 本轮检查 | 结果与边界 |
|---|---|
| 12 项定向回归 | 通过，覆盖锚点、解析器、回迁、Host 压力、缓存恢复与隔离 |
| 约 68K 藏针，7 次请求 | 压缩前后均 7/7；最后一次压缩后请求复用 58,330 tokens、耗时 1.93 秒 |
| 约 68K 小游戏状态/代码续接，7 次请求 | 压缩前后均 6/6 字段、6/6 Node 运行检查；最后一次请求复用 58,459 tokens、耗时 2.77 秒 |
| 日常 API 合成冒烟 | 7 项通过，含短 xhigh、工具续接及 CPU 读图 |

两个长夹具各总耗时约 153/156 秒，thinking-off、greedy；上表的 1.93/2.77 秒仅为最后一次请求。14 个输出 token 序列与旧 C2.1 同夹具一致。整卡显存每 5 秒采样峰值约 11,176/11,174 MiB；**没有原版 v0.1.4 同条件峰值对照，不能声称显存零增量或普遍提速。** 锚点会使用有界 Host 内存，压力下可被回收。[完整记录与限制](docs/C2P1_V014_PORT.md) · [脱敏证据](benchmarks/evidence/c2p1-v014-port/verification.json)

## 下载方式

**v1.0.0 为源码标签**：本轮未上传引擎附件；验证引擎 SHA-256 为 `71259a21f7b525ca7477193a292e4d3623cc58b9efd785005ee934cc29673507`。旧版 v0.1.5 附件继续保留。

1. **GitHub：** [v0.1.5 Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) 提供引擎 ZIP、SHA-256 和源码。引擎附件不是整合包，不含模型、Node 或运行 DLL；配合源码安装器使用。
2. **夸克网盘：** 链接见维护者的视频简介/置顶评论；上传后由维护者提供，不在此编造链接。两个整合包都含引擎、Node、dsh、运行 DLL、VC++ 安装器、安装与纠错文档，无需从 GitHub 获取部署文件。

| 网盘包 | 内容与入口 |
|---|---|
| `NInfer-v0.1.5-Windows-sm120a-no-models.zip` | 不含模型；运行 `01-setup-your-model.cmd` 选择自己的文字+MTP v3 `.ninfer` |
| `NInfer-v0.1.5-Windows-sm120a-full.zip` | GSQ-RCO IQ3_XXS 文字+MTP，以及完整 Huihui 文字+视觉+MTP 制品；先运行 `01-setup-gsq.cmd`，不再附带 Huihui 纯文字副本 |

**完整版默认 GSQ 纯文字，关闭视觉**，一次只加载一个模型。需要读图时，停止服务后显式运行 `09-enable-cpu-vision.cmd`；切换至 Huihui，视觉塔和前处理全部使用 CPU，文字/MTP 仍使用 GPU。首次读图会增加等待与 RAM 占用。`09-disable-vision-gsq.cmd` 切回 GSQ 并关闭视觉。[整合包说明](docs/OFFLINE_BUNDLE.md) · [安装提示词](docs/INSTALL_PROMPT.md) · [纠错文档](docs/TROUBLESHOOTING.md)

两包根目录均有中文安装说明、安装提示词、纠错文档、更新说明及独立的 `显存不足处理顺序.txt`。**安装完整性与 `04-verify.cmd` 不强制要求模型**，没有某个未选用模型也能通过；部署/启动只检查当前选择的模型。需要额外核对已保留模型时可用 `-CheckModels`。[具体用法](docs/OFFLINE_BUNDLE.md)

## 快速安装

使用网盘包按上表入口安装后运行 `02-start.cmd`，关闭用 `03-stop.cmd`。系统仍需驱动、VC++ 运行库与 Microsoft Edge；包内提供 VC++ 安装器，不捆绑显卡驱动。

使用 GitHub 源码安装（Node.js 24+，自行准备模型）：

```powershell
git clone https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent.git
cd qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\qwen3.8-27b-gsq-rco-iq3-xxs.ninfer"
.\start.cmd
```

新部署默认 NVFP4、xhigh、MTP3、DeviceKV **36864** / Retrieve **3840** / HostKV **7168 MiB**，sink 12288、recent 19456。不要直接套用维护者的 39168/6144 进阶档。升级已有安装见[安装说明](docs/INSTALL.md)，只 `git pull` 不会替换旧 exe。

显存不足依次处理：**优先混合显卡模式 → 对实际引擎关闭 CUDA 系统内存回退 → 关闭后台应用 → 优先换 RK4V4 → 再配套缩小页池与检索窗口。** 保留 KVMem 的 7GiB Host KV，它与驱动溢出不同。缩检索窗口可能影响历史召回；NVFP4 与 RK4V4 也不保证相同输出。[操作步骤](docs/OFFLINE_BUNDLE.md)

默认接口：引擎 `http://127.0.0.1:1234/v1`，缓存守卫 `http://127.0.0.1:1236/v1`，dsh 网页 `http://127.0.0.1:3080`。登录凭据只保存在本机日志，不要公开。

## 作品、来源与边界

维护者使用本地模型制作了[鹈鹕骑自行车动画](docs/demo/README.md)，也用近期候选制作了象棋游戏。作品展示不是受控性能或质量评测；旧版测速保留在[历史实测](docs/BENCHMARKS.md)，不当作本轮复测。

![本地模型生成的鹈鹕动画](output/playwright/pelican-preview.png)

特别感谢 Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)：维护者使用的基础架构来自他分享的版本，此前死循环问题也由他解决。基础引擎、KVMem 及其他作者归属见[上下游与许可](docs/UPSTREAM_DOWNSTREAM.md)与 [NOTICE](NOTICE)。

代码以 Apache-2.0 开源，第三方组件遵守各自许可。模型另行授权，**Huihui Swift 衍生部分适用 Swift Open License v1.0，含商业收入门槛**；不能将整个模型当作纯 Apache-2.0。[模型与许可](docs/MODELS.md)

本版是增量构建：从明确 v0.1.4 基线重编 71 个 Host 单元，复用 11 个已验证 CUDA 对象及原静态库，尚非干净全量构建。未重跑全量 CTest、全 256K、所有架构或长期 xhigh 真实项目评测。长历史中段通过检索选页，缓存保存不等于每次全量读取；搜索访问互联网仍需网络。默认单并发、回环地址，不自动修改驱动或启用人物技能。

[部署维护手册](docs/DEPLOYMENT_AGENT_GUIDE.md) · [源码编译](docs/BUILD.md) · [修改范围](docs/MODIFICATIONS.md) · [集成组件](integrations/README.md) · [v0.1.4 历史更新](docs/RELEASE_NOTES-v0.1.4.md)
