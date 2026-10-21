# 安装与使用条件

v0.1.4离线两模型包请先读[整合包安装](OFFLINE_BUNDLE.md)和[纠错文档](TROUBLESHOOTING.md)。新部署默认 `AnswerPenaltyScope=answer`（旧配置缺少也采用answer），设为 `carry-over` 后正常重启可关闭；引擎自身缺省仍为carry-over。

显存处理顺序：优先开启混合显卡模式 → 对实际引擎关闭CUDA系统内存回退（保留7GiB KVMem Host KV）→ 关闭无关后台应用 → 仍不足优先切RK4V4 → 再配套适当缩小页池和检索窗口。网盘包提供对应一键配置入口，详见[完整步骤](OFFLINE_BUNDLE.md)。

给部署者或 Agent 的完整操作顺序、版本表、启动设置和踩坑处理，见 [部署与维护交接手册](DEPLOYMENT_AGENT_GUIDE.md)。

验证环境：Windows x64、RTX 5070 Ti Laptop 12GB、i7-14650HX、32GB RAM，驱动 617.14。公开起步配置使用 36K Device KV、3840 检索、7 GiB Host KV 和 256K 逻辑容量；不要把内存容量直接等同于可用上下文或召回质量。

## 先从较小页池部署，再逐档增加

**新设备 / 新部署先用 `DeviceKV=36864`、`RetrieveTokens=3840` 的完整默认组合。** 这是相对进阶档较保守的起点，不是所有 12GB 设备都能无条件启动的保证。先关闭不需要的独显应用，为实际引擎单独禁用 CUDA 系统内存回退；确认权重和 Graph 正常加载，再验收短任务、完全相同请求的缓存重复、工具结果续接和对应目标长度的召回。

维护者的 **`39168 / 6144` 是进阶组合**，不是安装默认值。配套 `SinkTokens=12288`、`RecentTokens=19456`、Host KV 7168 MiB、MTP3、chunk128、xhigh。v0.1.3 当前使用 NVFP4；旧 v0.1.2 的 RK4V4 实测启动 CUDA 可用约 84 MiB，128K 藏针首次/重复 5/5、200K 均 4/5。这是历史结果，不适合作为当前 NVFP4 的显存或质量承诺。8192 检索档长文测试未完成。[历史实测](BENCHMARKS.md#语义历史修复与检索预算2026-10-16) · [当前 NVFP4 实测](NVFP4_SM120.md)

每次只增加一档并记录配置、启动 CUDA 余量、GPU 峰值、缓存续接和召回；出现分配失败或页池耗尽时恢复上一档，再定位原因。**不要只把 DeviceKV 调小而维持装不下的 sink/recent/retrieve/MTP/尾页预算**；若默认组合也放不下，先查实际占用，必须缩窗口时说明召回影响并单独验证，不能称为无损适配。扩大 DeviceKV 也不会自动扩大逻辑上下文上限。

## NVFP4 显存与 RK4V4 切换

v0.1.4 延续 v0.1.3 的默认 `KVDType=nvfp4`。**相对此前 RK4V4 配置，NVFP4 增加了一些显存占用；显存很紧张时可以自行换回 RK4V4。** 两者都由同一个新版 exe 支持，Host 压力回收等通用修复继续保留。NVFP4 专用调度不新增常驻 GPU buffer；显存增量与 KV 格式 / 工作区有关，实际差值随配置变化，不给固定 MiB 承诺。

先等待任务结束，用本安装 `stop.cmd` 关闭，在 `config/local.json` 添加或修改以下字段，再运行 `start.cmd`：

```json
"KVDType": "rk4v4"
```

该行放在已有 JSON 对象中，注意相邻字段的逗号。换回 NVFP4 则设为 `"nvfp4"`。启动器只接受这两种值；**旧配置缺少 `KVDType` 时默认 NVFP4**，希望升级后维持 RK4V4 的使用者应先显式添加该字段。无需换 exe、重新转换权重或删除会话；重启后 GPU / Host KV 缓存重建，切换格式可能改变输出，不能承诺相同质量。

维护者当前 NVFP4 进阶档启动日志约 64 MiB CUDA 余量；旧 RK4V4 的约 84 MiB 是另一轮历史记录，不能相减当作严格显存对照，也不能用整卡驱动空闲代替 CUDA 余量。先保留公开 `36864 / 3840` 起步组合；不要因换回 RK4V4 就跳过缓存、工具续接和目标长度验收。[本版更新与验证边界](RELEASE_NOTES.md)

## 显卡模式与显存准备

本仓库明确实测可用的设备是 **RTX 5070 Ti Laptop 12GB**。建议使用独显 + 核显混合模式，把桌面和浏览器交给核显渲染，为模型保留独显显存；启动脚本不自动修改系统显卡模式。没有核显或混合模式也可尝试，启动前关闭不需要的游戏、GPU 加速应用和后台进程，确认显存足够。

其他 **RTX 50 系、12GB 及以上显存**的显卡欢迎尝试，参数需按实际设备调整。**非 50 系需按对应 CUDA 架构自行编译**，参考 [编译说明](BUILD.md)。欢迎提交其他显卡的编译步骤、稳定配置及实测数据。

## CUDA 系统内存回退：强烈建议禁用

**强烈建议为 NInfer 单独禁用 NVIDIA 驱动的 CUDA 系统内存自动回退**，避免显存紧张时把 CUDA 分配回退到系统 RAM，造成推理速度明显下降。设置由用户手动完成，安装和启动脚本不修改驱动策略。

1. 打开 **NVIDIA 控制面板 → 管理 3D 设置 → 程序设置**，不要修改“全局设置”。
2. 点击“添加 → 浏览”，选择当前安装目录的 **`runtime/engine/ninfer-serve.exe`**。若修改了 `config/local.json` 的 `EnginePath`，选择实际运行的引擎 exe；移动安装位置后需重新检查对应路径。
3. 找到 **CUDA - Sysmem Fallback Policy**（中文名称可能显示为“CUDA - 系统内存回退策略”），选择 **Prefer No Sysmem Fallback**（首选不回退系统内存），点击“应用”。
4. 在方便时用 `stop.cmd` 关闭本安装，再用 `start.cmd` 启动，使新的 CUDA 进程使用该策略。核对该项位于正确 exe 的“程序设置”下；其他程序继续使用各自/全局策略。恢复时将该程序此项改回“使用全局设置”。

**保留本项目的 7 GiB Host KV。** 这是 KVMem 主动管理的 RAM 历史缓存，以及按需传输到 GPU 的机制，与驱动显存不足后的自动回退不同；禁用驱动回退不需要减小 Host KV，也不应关闭 RAM 检索索引。

禁用回退不会释放或增加物理显存；显存仍不足时可能直接报 CUDA 分配失败，应清理独显占用并检查预算。如果控制面板没有此选项，先确认 NVIDIA 驱动版本及控制面板支持情况，不要用修改全局策略来代替。该设置也不修复 `Device KV page pool exhausted`：后者需核对引擎内部页面预算和缓存恢复路径。

## 准备

1. 安装 [Node.js 24+](https://nodejs.org/en/download)；确认 `node --version` 和 `npm.cmd --version` 可执行。
2. 安装 [Microsoft x64 VC++ 运行库](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)。预编译程序不要求安装整套 CUDA Toolkit，需有兼容 CUDA 13 的 NVIDIA 驱动及安装器获取的 cuBLAS DLL。
3. 把源码解压到空间充足的目录，例如 `D:\AI\ninfer-local`。建议短路径；脚本支持空格。文本 `.ninfer` 本机约 10.06 GiB，GGUF 原件约 9.73 GiB；转换会同时占用两份权重。另需依赖、安装缓存和 npm 空间；源码编译还需更多磁盘和 RAM。
4. 按 [模型文档](MODELS.md) 准备 `text,mtp` 的 v3 `.ninfer`。现成 GGUF 不能直接作为服务器参数。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\qwen3.8-27b-gsq-rco-iq3-xxs.ninfer"
.\start.cmd
```

安装器核验 `config/downloads.json` 的归档 SHA-256 与每个运行文件的哈希。引擎从本项目 Release 获取；cuBLAS 来自 NVIDIA；FFmpeg 来自 BtbN 的固定 LGPL shared 构建。不会把当前用户安装中的 GPL DLL、模型、MSVC SDK 或 NVIDIA 工具链复制给下载者。

`config/local.json`、`runtime/`、`node_modules/`、模型及浏览器 profile 均被 Git 忽略。启动后等待权重和 CUDA Graph 加载，再自动打开 dsh 私有登录 URL。不要公开 `runtime/logs/dsh.stdout.log` 中的 token。首次安装需要网络，纯文字推理本身可离线。

## 从 v0.1.0 升级

先等待当前任务结束，用这份安装的 `stop.cmd` 关闭服务；备份自己的 `config/local.json`。Git 安装使用 `git pull --ff-only` 获取源码，ZIP 安装则解压新源码并保留原来的模型路径、私有配置和 `runtime/dsh-home`。不要把旧的 `config/downloads.json` 覆盖到新版本。

在项目目录重新执行原 `setup.ps1 -ModelPath "<现有模型.ninfer>"` 命令（API-only 安装仍可加 `-SkipDsh`），再运行 `start.cmd`。安装器保留本地配置并按哈希替换旧引擎；需要联网下载约 0.8 GiB 的新引擎归档，模型无需重下。当前 v0.1.4 的 exe SHA-256 见 [来源记录](../SOURCE_PROVENANCE.json)。从 v0.1.1 / v0.1.2 / v0.1.3 升级同样执行此流程；旧 Release 保留。只 `git pull` 不会替换已经下载的 exe，也不会重设自己的页池。

用户会话、浏览器 profile 和权重无需清空；原有会话首次换用新插件可能重算变化的前缀。该版本 KVMem 内容检索明确要求单并发与 Legacy 缓存，Hybrid/多并发配置会直接报不支持。引擎 ZIP 和 Source code ZIP 用途不同，不要将整个引擎 ZIP 当作源码或完整安装包。

## 日常启动、关闭和 API

双击 `start.cmd`；关闭时双击 `stop.cmd`。关闭脚本检查记录的 PID、启动时间、可执行路径和命令行标记，只关闭本目录启动的进程及子进程。会话和权重保留。

只使用 API：

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\model.ninfer" -SkipDsh
powershell -ExecutionPolicy Bypass -File .\start.ps1 -EngineOnly
```

```python
import json, urllib.request
request = urllib.request.Request(
    'http://127.0.0.1:1234/v1/chat/completions',
    json.dumps({'model': 'qwen3.8-27b-gsq-rco-iq3-xxs',
                'messages': [{'role': 'user', 'content': '你好，请介绍一下自己。'}],
                'max_tokens': 1024}).encode(),
    {'Content-Type': 'application/json'})
print(json.load(urllib.request.urlopen(request, timeout=300)))
```

端口、模型位置和页预算在 `config/local.json`。修改后重启。调整 Device KV 必须同时考虑 recent/sink/retrieve、MTP 与临时页需求；随意缩小池而保留其他预算可能引发页耗尽。发布预设保留既有生产数值。

## 搜索和浏览器

搜索通过本地 Node 后端访问公开搜索页面/RSS，网页正文在本机提取；不需要付费搜索 API，也不是本地离线网页索引。站点改版、网络限制或验证码会使搜索失败。Edge 仅在使用浏览器工具时启动，空闲约三分钟关闭，使用独立 profile。

如需核显渲染，在 Windows“设置 → 系统 → 屏幕 → 图形”中给 **Microsoft Edge 的实际 exe** 选择“节能 / Intel 核显”，然后关闭并重启本工具浏览器。这个偏好作用于该 Edge 路径；不能保证只影响某一个 Edge 实例。用 `browser_open` 返回的 renderer 核实。公开脚本默认不修改注册表、桌面 MUX 或全局 GPU 策略。

刷新请使用 `browser_action(action="reload")`，之后另一次工具读取页面。不要在一个 `browser_evaluate` 中刷新页面后继续等待旧 JavaScript 上下文。网站登录和验证码可能需要人工处理。

## 常见问题

| 现象 | 检查 |
|---|---|
| 双击退出 | 从 PowerShell 执行脚本，查看 `runtime/logs`；确认 Node、VC++ 运行库、驱动与模型路径 |
| 显存不足 / 突然明显降速 | 关闭实际占用独显的程序，检查 `nvidia-smi`；按上文为引擎单独禁用 CUDA 系统内存回退。12GB 预设接近预算，禁用后不足时仍可能报错 |
| 页池耗尽 | 确认使用本项目修复后的引擎和完整默认预算；保留失败日志、请求顺序与 `/v1/load`，不要只增 Host KV |
| 端口被占用 | 先关闭原本部署，或修改本目录端口；脚本不会替你杀掉占用端口的其他程序 |
| 缓存突然失效 | 比较 token 前缀；换工具表、模板、历史消息或压缩都可能需要重算 |
| dsh 更新后行为变化 | 该版本固定 `0.1.5-rc.3`；补丁有版本检查，不直接覆盖其他版本 |
| 网页打不开 / 代理失败 | 检查自己的网络代理；本项目不修改 Windows 全局代理 |

出现问题时提交 [Issue](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/issues)，附硬件、驱动、配置和删去隐私后的错误信息。
