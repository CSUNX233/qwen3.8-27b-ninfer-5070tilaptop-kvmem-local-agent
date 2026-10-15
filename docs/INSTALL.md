# 安装与使用条件

给部署者或 Agent 的完整操作顺序、版本表、启动设置和踩坑处理，见 [部署与维护交接手册](DEPLOYMENT_AGENT_GUIDE.md)。

验证环境：Windows x64、RTX 5070 Ti Laptop 12GB、i7-14650HX、32GB RAM，驱动 617.14。现有配置使用 36K Device KV、7 GiB Host KV 和 256K 逻辑容量；不要把内存容量直接等同于可用上下文或召回质量。

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

在项目目录重新执行原 `setup.ps1 -ModelPath "<现有模型.ninfer>"` 命令（API-only 安装仍可加 `-SkipDsh`），再运行 `start.cmd`。安装器保留本地配置并按哈希替换旧引擎；需要联网下载约 0.8 GiB 的新引擎归档，模型无需重下。v0.1.1 的 exe SHA-256 见 [来源记录](../SOURCE_PROVENANCE.json)。只 `git pull` 不会替换已经下载的 exe。

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
