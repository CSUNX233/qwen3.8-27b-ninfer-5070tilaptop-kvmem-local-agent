# v0.1.4 两模型一键整合包

适用Windows x64、RTX5070Ti Laptop12GB和32GB RAM；其他50系需单独验证，非50系需要重新编译。**网盘一键包已包含引擎、两个ninfer模型、Node、dsh和所需运行依赖，只下载夸克包即可部署，不需要访问GitHub或再下载这些文件。** 网盘包由维护者上传，GitHub Release仍只放引擎与源码。

## 包内内容

- v0.1.4引擎，固定版本cuBLAS13和LGPL shared FFmpeg；完整许可和来源。
- 两个v3 `.ninfer` 文本+MTP模型：Huihui Qwen3.8-27B abliterated Swift-1.5 GSQ-RCO IQ3_XXS、原版Qwen3.8-27B GSQ-RCO IQ3_XXS。分别约10.06GiB，只加载所选一个，不含额外视觉塔。
- 便携Node24.14.1、锁定dsh0.1.5-rc.3及网页工具npm依赖，离线安装无需重新npm下载。附Microsoft VC++ x64运行库安装器。
- 启动/停止、模型切换、回答惩罚作用范围和KV格式切换入口，安装/纠错提示词及更新说明。
- `offline-bundle.json` 与 `SHA256SUMS`记录文件大小、来源和SHA-256；`04-verify.cmd`完整核对。

**系统仍需已安装兼容CUDA13的NVIDIA驱动和Microsoft Edge。** 驱动按显卡/系统从NVIDIA或电脑厂商获取，本包不安装或修改驱动。预编译文字服务无需完整CUDA Toolkit、Python、Visual Studio或额外下载模型。

## 解压和安装

启动前建议依次处理显存：

1. **优先开启混合显卡模式**（电脑厂商控制中心/BIOS中的Hybrid/Optimus），让桌面和浏览器使用核显，独显留给推理；按厂商要求重启。无核显的设备跳过此项。
2. **关闭引擎的CUDA系统内存回退（溢出内存）**：NVIDIA控制面板 → 管理3D设置 → 程序设置 → 添加本包 `runtime/engine/ninfer-serve.exe` → `CUDA - Sysmem Fallback Policy` → `Prefer No Sysmem Fallback`。只对引擎设置，正常重启引擎后生效；本包不自动修改驱动。此项防止显存不足时悄悄落入系统内存而严重变慢，不会凭空增加显存。**保留KVMem的HostKV=7168MiB**，它与驱动溢出不同。
3. 关闭无关后台应用、游戏和其他独显负载，确保不是另一个模型占着显存。
4. 仍不足时，优先用 `07-kv-rk4v4.cmd` 切回RK4V4，再启动。
5. 仍不足，再用 `08-pool-small.cmd` 适当缩小整套预算：DeviceKV34560 / Retrieve1536，sink12288 / recent19456保持，给临时页保留与默认组合相同的1280-token名义余量。该小档只核对配置和名义容量，未追加实模验收；检索窗口减少可能损失历史召回，需验证自己的长任务。逻辑上下文上限仍262144，不等于仍有相同有效召回。`08-pool-default.cmd` 恢复36864/3840。切换前均先停止本包，不自动降低页池或思考强度。

1. 从网盘完整下载ZIP64，解压到较短路径，例如 `D:\AI\ninfer-v014`，不要直接在压缩包里运行。模型较大，所在磁盘应使用NTFS/exFAT，不能放FAT32；下载+解压建议至少55GB空间。
2. 第一次双击 `00-install-vc-runtime.cmd`，按Microsoft安装器提示完成运行库安装；已安装兼容版本可跳过。需要重启时先完成系统重启。
3. 双击 `01-setup-huihui.cmd`（日常Huihui），或 `01-setup-gsq.cmd`（原版GSQ）。安装器核对文件、检测引擎依赖并生成本包独立的dsh配置；不启动模型推理。
4. 双击 `02-start.cmd`。第一次加载权重和捕获CUDA Graph需要等待；就绪后自动打开本机Agent网页。关闭用 `03-stop.cmd`，保留会话、模型和配置。

不在整合包内运行 `setup.ps1` 的在线流程；上述一键安装入口会指定 `-Offline`。首次校验会读取两个模型和全部依赖，可能需要数分钟，不应误判卡死。仅API可用 `scripts/setup-bundle.ps1 -Model huihui -SkipDsh`，之后 `start.ps1 -EngineOnly`。

## 设置与文件位置

默认NVFP4、xhigh、MTP3、逻辑262144、DeviceKV36864 / Retrieve3840 / HostKV7168MiB、sink12288 / recent19456；回答presence=1且 `AnswerPenaltyScope=answer`。公开预算比维护者的39168/6144更保守。NVFP4比此前RK4V4配置多占一些显存，不能承诺所有12GB设备均能启动。

等待任务结束并运行 `03-stop.cmd` 后，可双击 `05-use-huihui.cmd` / `05-use-gsq.cmd` 切模型；`06-answer-counts.cmd` / `06-carry-over-counts.cmd` 开关独立回答计数；`07-kv-nvfp4.cmd` / `07-kv-rk4v4.cmd` 切KV。切换只保存配置，之后运行 `02-start.cmd`；不会自动停止其他服务。更换模型后建议新开会话以免混用旧模型输出，磁盘旧会话保留。

`config/local.json`为本包设置；`runtime/dsh-home`为独立会话；`runtime/logs`为日志；`runtime/node`为便携Node；`runtime/engine`为引擎与DLL。登录URL只在本机日志，不随分享包提供。移动已安装包后先停止，再重跑一键安装以重建路径。

模型来源和许可见[模型说明](MODELS.md)及包内 `model-notices/`。Swift派生部分适用Swift Open License v1.0，包含商业使用收入门槛；保留Huihui模型卡和UkisAI许可，不能将其当作纯Apache-2.0模型。包内模型由现有GGUF转换为ninfer-v3，不重新训练或量化。

[安装提示词](INSTALL_PROMPT.md) · [纠错文档与提示词](TROUBLESHOOTING.md) · [本版提升与边界](RELEASE_NOTES.md)

## 构建与验证范围

本次35,053文件（含两个模型）完整校验、离线安装、两模型路由及KV/计数/页池配置切换通过；便携dsh在独立端口认证网页HTTP200，未连接日常模型或发送推理请求。修正Windows PowerShell的Node版本检测兼容性；完整性校验另覆盖篡改、缺失与路径越界拒绝。[发布验收记录](../benchmarks/evidence/publication/verification-v014.json)。

维护者用 `scripts/package-offline.py` 从公开源码文件、明确模型和全新npm依赖构建；不打入旧dsh home、cookie、token、用户配置、缓存、原始私有日志或本机Node目录。模型/依赖SHA写入包内清单；解压校验、便携Node、引擎帮助和离线dsh配置属于安装验收，不等于真实模型任务验收。引擎已有29/29等固定夹具结果见更新说明，最终整合包不额外生成模型输出。
