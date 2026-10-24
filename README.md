# NInfer + KVMem：12GB 显卡运行 27B 本地 Agent

**v0.1.5 已发布：在 v0.1.4 上移植 C2.1，改善长任务的缓存恢复和 KV 回迁。** 保留工具解析、采样、NVFP4、FP16 GDN 和回答独立惩罚计数修复，默认仍为纯文字 + MTP、xhigh，不启用视觉。[下载 v0.1.5](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) · [更新说明](docs/RELEASE_NOTES.md) · [English](README.en.md)

维护者在 **RTX 5070 Ti Laptop 12GB / 32GB RAM / Windows** 上使用 Qwen3.8-27B GSQ-RCO IQ3_XXS；KVMem 把历史 KV 保存在 RAM、按需调入 GPU，配合 MTP、缓存守卫和可选 dsh 网页工具。逻辑上下文配置为 256K；不代表每步读取全部历史，也不代表已完成满 256K 验收。

## 显卡与模型选择

**经多位使用者实测反馈，RTX 30 系及以后、12GB 及以上显卡可以稳定运行本项目；16GB 卡可选更大的 IQ3_S 模型，通常能获得更高质量。** 这是社区使用反馈，需选择匹配显卡架构的引擎，并给系统、后台和 KV 留足余量，不是对每种设备/任务的逐卡保证。

| 硬件 | 引擎与模型建议 |
|---|---|
| RTX 30 / 40 系，12GB+ | 使用对应架构的兼容构建；参考[编译说明](docs/BUILD.md)，不能直接运行本次 sm120a 附件 |
| RTX 50 系，12GB+ | 本次 Windows x64 / sm120a 附件；直接实测设备为 5070 Ti Laptop 12GB |
| 16GB 及以上 | 可以选择更大的 IQ3_S 文字模型，另行准备含 MTP 的 `.ninfer` 并验证预算；本次完整版附带的两份文字模型仍是 IQ3_XXS |

推荐 32GB 系统内存、SSD，以及兼容 CUDA 13 的 NVIDIA 驱动。不同架构还需适配内核与依赖，不能只改 exe 文件名。

## 这一版改善了什么

- **减少长历史续接的重复预填充。** 在消息边界保留靠近尾部的滚动长锚点，同时保留有用的早期回退点；同 lineage 恢复时可消费旧 owner，保留可复用的不可变 KV/状态。
- **减少冷页逐页回迁的开销。** 合格冷页按最多 32 页批量回迁 Host；大块分配失败逐级缩批，允许部分推进，保留 sink、页顺序、来源 pin 和尾页约束。
- **基线明确回到 v0.1.4。** 保留上一版正确性修复，没有改变模型权重、KV 格式、注意力公式或检索窗口；这轮重点是缓存生命周期，不能据此宣称总体模型质量提升。
- **安装包拆成两种。** 无模型版支持自己的 `.ninfer`；完整版含双文字模型及可选 CPU 视觉制品，均自带引擎和用户态运行依赖。

| 本轮检查 | 结果与边界 |
|---|---|
| 12 项定向回归 | 通过，覆盖锚点、解析器、回迁、Host 压力、缓存恢复与隔离 |
| 约 68K 藏针，7 次请求 | 压缩前后均 7/7；最后一次压缩后请求复用 58,330 tokens、耗时 1.93 秒 |
| 约 68K 小游戏状态/代码续接，7 次请求 | 压缩前后均 6/6 字段、6/6 Node 运行检查；最后一次请求复用 58,459 tokens、耗时 2.77 秒 |
| 日常 API 合成冒烟 | 7 项通过，含短 xhigh、工具续接及 CPU 读图 |

两个长夹具各总耗时约 153/156 秒，thinking-off、greedy；上表的 1.93/2.77 秒仅为最后一次请求。14 个输出 token 序列与旧 C2.1 同夹具一致。整卡显存每 5 秒采样峰值约 11,176/11,174 MiB；**没有原版 v0.1.4 同条件峰值对照，不能声称显存零增量或普遍提速。** 锚点会使用有界 Host 内存，压力下可被回收。[完整记录与限制](docs/C2P1_V014_PORT.md) · [脱敏证据](benchmarks/evidence/c2p1-v014-port/verification.json)

## 下载方式

1. **GitHub：** [v0.1.5 Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) 提供引擎 ZIP、SHA-256 和源码。引擎附件不是整合包，不含模型、Node 或运行 DLL；配合源码安装器使用。
2. **夸克网盘：** 链接见维护者的视频简介/置顶评论；上传后由维护者提供，不在此编造链接。两个整合包都含引擎、Node、dsh、运行 DLL、VC++ 安装器、安装与纠错文档，无需从 GitHub 获取部署文件。

| 网盘包 | 内容与入口 |
|---|---|
| `NInfer-v0.1.5-Windows-sm120a-no-models.zip` | 不含模型；运行 `01-setup-your-model.cmd` 选择自己的文字+MTP v3 `.ninfer` |
| `NInfer-v0.1.5-Windows-sm120a-full.zip` | Huihui Swift-1.5 与原版 GSQ-RCO 两份 IQ3_XXS 文字模型，加完整 Huihui CPU 视觉制品；先运行 `01-setup-huihui.cmd` 或 `01-setup-gsq.cmd` |

**完整版也默认关闭视觉**，不同时加载两个文字模型。需要读图时，停止服务后显式运行 `09-enable-cpu-vision.cmd`；视觉塔和前处理全部使用 CPU，文字/MTP 仍使用 GPU。首次读图会增加等待与 RAM 占用。[整合包说明](docs/OFFLINE_BUNDLE.md) · [安装提示词](docs/INSTALL_PROMPT.md) · [纠错文档](docs/TROUBLESHOOTING.md)

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
