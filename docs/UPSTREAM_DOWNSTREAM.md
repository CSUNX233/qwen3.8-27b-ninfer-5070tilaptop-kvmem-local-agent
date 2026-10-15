# 上游、下游和许可证

仓库维护者：**[CSUNX233](https://github.com/CSUNX233)**。本仓库是面向一台 5070 Ti Laptop 12GB 机器整理的下游发行版，不把 NInfer、KVMem、模型或量化方法声明为维护者原创。

**特别感谢 Bilibili UP 主 [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)：本机使用的基础架构下载自他提供的版本，死循环问题也是由他解决。** 本项归属由维护者明确提供；不把基础分发和死循环修复归给本仓库的后续调度优化。

## 来源链

| 层次 | 作者 / 项目 | 本仓库关系 |
|---|---|---|
| 基础 C++/CUDA 推理引擎 | [Neroued/ninfer](https://github.com/Neroued/ninfer) | NInfer 原始上游，Apache-2.0 |
| 本包紧邻的源码线 | [iamwavecut/ninfer-all](https://github.com/iamwavecut/ninfer-all) | 分享包声明基于 v0.11.0-rtx3090 的合并线，Apache-2.0 |
| 更早合并 fork 与贡献者 | [原 README](../engine/README.md) / [CONTRIBUTORS](../engine/CONTRIBUTORS.md) | 保留原作者说明；包括 ashalliants、Don-Chad、Warlax、TertiumOrganum1、UDPSendToFailed 等各线贡献 |
| KVMem-ring | 沈三殊 / [shensanshu/ninfer-master-shensanshu-kvmem](https://modelscope.cn/models/shensanshu/ninfer-master-shensanshu-kvmem) | 分享包记录为 Apache-2.0；页环、Host KV、检索与恢复基础。其集成记录引用 `65dbd4f2`，不是本仓库可验证的上游完整 commit |
| NInfer2-Share-C 分享包 | 原分发者，随包文档称 `chatbox` | 包含 Windows 端口、长上下文可见性、取消恢复、检索与 dsh 修复；没有可靠 GitHub 账号映射，不杜撰署名 |
| 本机后续工作 | CSUNX233 维护的本发行版 | Host 索引、异步检索/预取、缓存接管修复、5070 Ti 指定矩阵调度、公开安装与测试整理 |
| 基础架构分发与死循环修复 | Bilibili **0xFFFF7** | 维护者实际下载的基础版本提供者，并解决此前死循环问题；此处明确致谢，不据此推断所有上游代码均由同一人完成 |
| 模型 | [Qwen](https://huggingface.co/Qwen/Qwen3.8-27B) | 架构、基础权重和 tokenizer；模型独立许可 |
| 量化 | [ISTA-DASLab GSQ-RCO](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF) | 本机 IQ3_XXS + MTP 权重来源；不把量化收益归给 CUDA 调度优化 |

收到的分享包 `.git` 是指向作者电脑的失效 worktree 路径，无法重建原提交历史。本仓库从源码快照建立新的下游历史，保留原许可证、作者文件和本机 patch。详见 [SOURCE_PROVENANCE.json](../SOURCE_PROVENANCE.json)。仓库不是 GitHub 原生 fork 关系；这里的来源链是技术与归属说明。

## 下游如何使用

- 直接接 OpenAI 兼容本地客户端：`http://127.0.0.1:1234/v1`。
- 本项目可选 [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness) / dsh `0.1.5-rc.3`：模型经严格缓存守卫 1236 接入，网页默认 3080。
- dsh 联网后端在 CPU/RAM 上执行搜索与正文提取，MCP 浏览器使用 Playwright Core 驱动本机 Edge；浏览器 GPU 由系统选择，可手动设核显。
- 下游再分发须保留 Apache-2.0、NOTICE、适用第三方 MIT/BSD/Unicode 等许可证与修改声明。模型及专有运行库的授权不由本仓库许可证替代。

## 组件许可和分发方式

| 组件 | 许可 | 本项目提供什么 |
|---|---|---|
| 本发行版自有代码 / NInfer 主体 | Apache-2.0 | 源码、已修改文件说明、引擎 Release |
| dsh / 修改后的 compaction 文件 | MIT，Copyright (c) 2026 DeepSeek | npm 固定依赖和两份带 MIT 归属的补丁；`licenses/` 保留许可 |
| ggml、llama-jinja、spdlog、json、cpp-httplib、xgrammar、utf8proc 等 | 各自 MIT/Apache/BSD/Unicode 等 | 源码及原许可证位于 `engine/third_party/` |
| curl / zlib | curl / zlib license | 链接使用，许可证见 `licenses/` |
| NVIDIA CUDA runtime / cuBLAS | NVIDIA CUDA 软件许可 | 引擎含链接运行代码；cuBLAS 从 NVIDIA 固定版本归档下载，不把工具链打包上传 |
| Microsoft VC++ 运行库 | Microsoft 运行库条款 | 用户从 Microsoft 安装，不分发完整 MSVC SDK |
| FFmpeg shared runtime | **LGPL-3.0-or-later**（所选 BtbN 构建） | 安装器从 BtbN 原发布页直接下载，保留其可替换 DLL 使用方式；未把工作安装的 GPL DLL 上传 |
| Node.js / Edge / npm 依赖 | 各自许可 | 用户安装 Node/Edge；npm 从登记源下载并保留包许可 |
| Qwen / GSQ-RCO 权重 | 发布检查时均为 Apache-2.0 | 提供原仓库链接和转换方法，不上传模型 |

可自行下载、使用、修改、再分发或商用本项目代码，条件以实际许可证为准；软件按许可证原样提供。不得移除上游归属、把他人工作标为原创，或把开源代码许可当成对任意模型/依赖的统一授权。

FFmpeg 固定构建：[`autobuild-2026-10-14-13-03`](https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-14-13-03)，FFmpeg revision `045dea2a34`；源与构建配方见 [FFmpeg](https://github.com/FFmpeg/FFmpeg/tree/045dea2a34) / [BtbN build scripts](https://github.com/BtbN/FFmpeg-Builds)。依赖归档 URL 与校验值在 `config/downloads.json`。如另行打包外部 DLL，需自行履行对应源码、许可及通知义务。
