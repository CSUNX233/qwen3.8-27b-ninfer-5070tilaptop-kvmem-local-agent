# v0.1.5 离线整合包：无模型版与完整版

两个包均包含**引擎、便携 Node24.14.1、固定 dsh0.1.5-rc.3、网页工具 npm 依赖、cuBLAS13、LGPL shared FFmpeg、VC++ x64 安装器、源码、许可、安装提示词、纠错文档和更新说明**，不依赖 GitHub 在线部署。网盘由维护者上传，链接见视频简介/置顶评论。

社区反馈 RTX30 系及以后、12GB+ 可运行本项目；16GB 可选择更大的 IQ3_S 模型。**这两包的预编译引擎均为 Windows x64 / sm120a（50系），30/40系需对应架构的兼容引擎和依赖，不能直接使用本 exe。** 建议32GB RAM和SSD。系统仍需兼容CUDA13的NVIDIA驱动及Microsoft Edge；不捆绑或自动修改驱动。

## 选择哪个包

| 包名 | 模型内容 | 首次安装入口 |
|---|---|---|
| `NInfer-v0.1.5-Windows-sm120a-no-models.zip` | 无权重，适合已有模型 | `01-setup-your-model.cmd`，输入自己的文字+MTP v3 `.ninfer` 完整路径 |
| `NInfer-v0.1.5-Windows-sm120a-full.zip` | GSQ-RCO IQ3_XXS文字+MTP，以及完整Huihui文字+BF16视觉+MTP；不含单独的Huihui纯文字副本 | `01-setup-gsq.cmd` |

GSQ文字模型约10.06GiB，Huihui视觉制品约10.91GiB；**视觉制品也含完整文字权重**，无需下载/转换GGUF。完整版只保留这两份制品，相比三模型包减少约10.80GB；一次只加载所选一个。默认使用GSQ纯文字；启用CPU视觉时切换到Huihui，关闭视觉时切回GSQ。本包不含IQ3_S。裸mmproj GGUF不能直接交给本服务器。

压缩包采用ZIP64，放NTFS/exFAT盘，不能用FAT32。完整包下载加解压建议预留至少60GB，无模型版至少15GB；实际大小以ZIP及清单为准。解压到短路径如 `D:\AI\ninfer-v015`，不要在压缩包里直接运行。

## 显存准备顺序

包根目录的 `显存不足处理顺序.txt` 可单独打开，内容对应[显存处理指南](VRAM_GUIDE.md)。

1. **优先开混合显卡模式**（Hybrid/Optimus），让桌面和浏览器尽量用核显；按设备要求重启，没有核显则跳过。
2. **关闭实际引擎的驱动溢出内存：** NVIDIA控制面板 → 管理3D设置 → 程序设置 → 添加本包 `runtime/engine/ninfer-serve.exe` → `CUDA - Sysmem Fallback Policy` → `Prefer No Sysmem Fallback`。只针对实际exe设置，重启引擎生效。**保留KVMem HostKV7168MiB**，它与驱动系统内存回退不同。
3. 关闭无关后台、游戏和其他独显应用。
4. 仍显存不足，优先在停止后用 `07-kv-rk4v4.cmd` 切换，再启动；同一引擎支持两种KV格式，输出可能变化。
5. 还不足，用 `08-pool-small.cmd` 配套设为 DeviceKV34560 / Retrieve1536，sink12288 / recent19456保留。小档减少检索预算，可能损失召回，未新增实模长任务验收；不能只缩池而保留装不下的窗口。`08-pool-default.cmd`恢复36864/3840。

脚本不自动更改驱动、显卡模式或其他应用。驱动整卡空闲不等于CUDA可分配余量。

## 安装与启动

1. 完整解压。首次可用 `00-install-vc-runtime.cmd` 安装Microsoft运行库，已安装兼容版本可跳过。
2. 按上表运行安装入口。安装完整性检查只对引擎、依赖和部署文件做SHA-256校验，**不强制任何模型存在**；缺少未选用的文字或视觉模型不会报错，也不扫描约21GiB模型。实际部署/启动只要求当前选用的模型存在；不发送推理请求，不下载依赖。
3. 运行 `02-start.cmd`，等待权重加载和CUDA Graph捕获，然后自动打开本机dsh网页。关闭使用 `03-stop.cmd`，只停止本包进程，保留会话和模型。
4. `04-verify.cmd`重查程序和依赖完整性，没有模型也能通过。包内 `offline-bundle.json`、`SHA256SUMS`记录打包时的文件与模型身份；外层 `.zip.sha256`供上传/下载核对。需要排查模型损坏时，可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-bundle.ps1 -CheckModels`，额外校验仍在目录中的模型，缺失模型仍跳过；原包内模型有损坏会明确报错。制作发布ZIP时仍校验全部实际分发的模型。

无模型版也可命令行安装：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup-bundle.ps1 -ModelPath "D:\Models\model.ninfer" -ModelId "my-qwen-model"
```

默认NVFP4、xhigh、MTP3、逻辑262144、DeviceKV36864 / Retrieve3840 / HostKV7168MiB、sink12288 / recent19456、AnswerPenaltyScope=answer，启用C2.1锚点和batch32。16GB卡选择IQ3_S时同样先保留起步预算再验证，不自动扩大页池。

## 文字与CPU视觉切换

**默认GSQ纯文字，不启用视觉**；第一次安装GSQ时写入 `VisionCPU=false`，dsh只声明text输入。完整包文件存在不代表加载视觉。

等待当前任务结束、运行 `03-stop.cmd` 后：

- `05-use-gsq.cmd`：选择GSQ文字模型，同时关闭视觉。
- `09-enable-cpu-vision.cmd`：切到完整Huihui视觉制品，设置 `VisionCPU=true`；启动器加入 `--vision-cpu --vision-max-merged 256`，dsh声明text,image。前处理和视觉塔完全CPU，文字/MTP仍GPU。
- `09-disable-vision-gsq.cmd`：切回GSQ文字制品并关闭视觉；此操作也会切换模型。

保存后运行 `02-start.cmd`。切模型建议新开会话；旧磁盘会话保留，进程缓存重建。CPU视觉增加RAM和首次图片编码等待，256 merged-token cap可能丢失细小文字/复杂图像细节。[视觉说明](VISION.md)

`06-answer-counts.cmd` / `06-carry-over-counts.cmd`控制回答惩罚计数；`07-*`控制KV格式；`08-*`控制池/检索。全部要求先停止本包，只保存设置，不自动启动。

## 文件、许可与验证

`config/local.json`为私有配置，`runtime/dsh-home`为会话，`runtime/logs`为日志；登录URL不公开。移动已安装目录后先停止，再重新运行安装入口更新路径。升级建议新目录解压，保留旧包作回退；不要把新默认配置覆盖自己的设置或删除会话。

模型来源与许可见[MODELS.md](MODELS.md)、包内 `model-notices/`；Swift部分遵守Swift Open License v1.0及商业收入门槛，不能当作纯Apache模型。

由 `scripts/package-offline.py`从Git公开文件、明确模型、固定哈希归档与锁定npm依赖构建，不复制用户home、cookie、token、缓存或原始日志。归档仅包含manifest内文件及两个校验清单；安装后的配置不会进入分享ZIP。文件校验、引擎帮助与离线配置测试属于安装验收，不能替代实际模型任务；引擎实测范围见[本版更新说明](RELEASE_NOTES.md)。

[安装提示词](INSTALL_PROMPT.md) · [纠错文档](TROUBLESHOOTING.md) · [维护指南](DEPLOYMENT_AGENT_GUIDE.md)
