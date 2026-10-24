# v0.1.5 — 长历史缓存恢复与批量 KV 回迁

本版把已试用的 C2.1 移植到明确的 **v0.1.4 / 4e4ee4262f333b75b4d94afdf41d31d2c1f4e500** 基线。重点是减少长任务续接的重复计算和改善 Host 回迁行为，保留 v0.1.4 的工具解析、采样阶段/耦合采样、NVFP4 分区、FP16 GDN 舍入和回答独立惩罚修复。[上一版说明](RELEASE_NOTES-v0.1.4.md)

## 主要变化

- **滚动长锚点：** 在追加消息边界补充近尾锚点，按间距节流，同时保留有用的早期回退点。相同 lineage 恢复可消费旧活动 owner，保留 immutable KV/状态；不同 named session 与 readonly 请求继续隔离。
- **批量冷页回迁：** eligible 冷页最多 32 页一批；Host 连续 extent 不足时逐级减半至单页，允许部分推进，避免大块申请失败阻断仍可完成的回迁。保留 oldest-first、sink、preferred-page、source-pin 与尾页约束，已有 Host 副本直接复用。
- **公开启动器接入 C2.1：** 启用 `--auto-long-anchors`，默认 `NINFER_C2_ROLLING_ANCHOR=1`、`NINFER_C2_CONSUME_ANCHOR=1`、`NINFER_KV_DEMOTE_BATCH=32`；Host state slots 8、长锚点间距 49152、每 continuation 最多 2 个，Device state slots 0。诊断时可在启动前显式设两项锚点开关为 0、batch 为 1，然后正常重启。
- **双整合包：** 无模型版可指定自己的 v3 `.ninfer`；完整版仅保留 GSQ-RCO 文字+MTP 和完整 Huihui 文字+视觉+MTP 两份制品，不再重复附带 Huihui 纯文字副本，减少约10.80GB。均包含引擎和用户态依赖，上传夸克后由维护者提供链接。完整版默认GSQ纯文字；CPU视觉显式切到Huihui，关闭视觉切回GSQ。配置读取显式使用 UTF-8，避免 Windows PowerShell 5.1 把中文模型路径读成乱码。
- **文档刷新：** 更新 README、安装/纠错入口、模型与硬件说明；移除把旧测量称为当前结果的首页表述。
- **安装器后续修订（引擎不变）：** 安装与 `04-verify.cmd` 默认只校验程序、依赖及部署文件，不强制任何模型存在；部署/启动再检查当前所选模型。可选 `-CheckModels` 只额外校验仍保留的原包模型，缺少未选模型不报错。整合包包含部署、纠错提示词与显存不足处理顺序；README 顶部恢复项目介绍和明确标注版本条件的 5070 Ti Laptop 历史测速。此修订随源码 `main` 和重打整合包提供，已发布的 `v0.1.5` 源码标签与引擎附件保持原样；报告问题请同时记录整合包 `source_commit`。

## 实测结果

12 项定向回归全部通过，覆盖锚点选择、frontend、工具解析、materialization budget、resource manager、sampling defaults、batch32/batch1 回迁、Host 压力、context store、KV cache 和真实模型恢复/隔离。20 页回迁夹具的复制批次从 20 次变 1 次，只表示该夹具，不能直接换算整模型加速倍数。

| 长任务（各 7 次请求） | 压缩前后正确性 | 最后一次压缩后续接 |
|---|---|---|
| 藏针，最高 68,358-token 输入 | 均 7/7 字段 | 复用 58,330 tokens，新增 186，1.925 秒 |
| 小游戏状态与代码，最高 68,540-token 输入 | 均 6/6 字段、6/6 Node 运行检查 | 复用 58,459 tokens，新增 200，2.766 秒 |

整个任务各约 153/156 秒，包含首次预填充；最后请求耗时不能当成全任务耗时。14 个输出 token 序列与旧 C2.1 同夹具贪心结果一致。测试为 thinking-off 合成夹具，不是长期 xhigh 网页项目开发。替换日常后另有 7 项 API 冒烟通过：冷/热文字、短 xhigh、工具调用/结果续接、CPU 读图、图片后文字续接。[完整记录](C2P1_V014_PORT.md) · [脱敏结果](../benchmarks/evidence/c2p1-v014-port/verification.json)

## 默认设置、硬件与代价

**发布默认纯文字 + MTP，不启用视觉。** 保留 NVFP4、xhigh、MTP3、回答 presence=1、AnswerPenaltyScope=answer、逻辑上下文 262144。公开起步组合仍为 DeviceKV36864 / Retrieve3840 / HostKV7168MiB，sink12288 / recent19456，不自动套用本机进阶预算。

社区多位用户实测反馈：**30 系及以后、12GB 及以上卡可稳定使用项目；16GB 卡可用更大的 IQ3_S 模型，通常质量更高。** 需要对应架构引擎与合适预算。此次预编译附件仍为 **Windows x64 / sm120a / 50 系**，30/40 系需自行编译或使用已验证的对应版本，不能直接使用此附件。网盘完整版附带 IQ3_XXS，不含 IQ3_S。

显存不足按“混合显卡模式 → 对引擎关闭驱动溢出 → 关闭后台 → 优先 RK4V4 → 再配套缩页池/检索”处理，保留 KVMem Host KV。[操作入口](OFFLINE_BUNDLE.md)

锚点使用有界 Host 状态/KV，捕获有额外工作，压力下仍可能被淘汰；批量回迁可能增加单批等待。未修改注意力公式、KV 量化编码或检索窗口，不保证所有任务更快或整体质量提高。每 5 秒整卡显存采样峰值 11,176/11,174 MiB，与旧 C2.1 同任务记录相同；**没有原正式 v0.1.4 同条件对照，无法给出相对该版的峰值增量**。

## 下载、升级与回退

[v0.1.5 Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) 提供引擎和 SHA-256。源码安装用户先停止本安装、备份配置及旧引擎，更新源码，再执行原 `setup.ps1 -ModelPath "<模型.ninfer>"`；只拉源码不会更新 exe。无需重新下载现有模型或清空磁盘会话，进程缓存会重建。[升级步骤](INSTALL.md)

夸克包分 `no-models` 与 `full`，都自带引擎；详细见[整合包说明](OFFLINE_BUNDLE.md)。完整包的视觉制品含 text,vision,mtp；启用后视觉前处理/编码固定 CPU，文字仍在 GPU；默认不启用。回退请成套恢复旧引擎与启动配置，或解压旧版到独立目录，避免旧 exe 接收新增参数；旧 Release 保留。

## 构建与未覆盖范围

CUDA13.0.48 / MSVC14.44.35207 / sm120a；71 个 Host 覆盖单元重编，复用 11 个已验证 CUDA 对象和原静态库，**不是干净全量构建**。未重跑全量 CTest、满 256K、所有显卡或总体质量评测，未宣称循环问题全部解决。发布打包验收不重复占用日常显卡做长测试。

最终引擎 SHA-256：

```text
38bc4d8c6be07afd6c74b902cc33403bf2ee139a2c50129d363d68d74a7609d6
```

[来源与历史记录](../SOURCE_PROVENANCE.json) · [源码构建](BUILD.md) · [模型许可](MODELS.md)
