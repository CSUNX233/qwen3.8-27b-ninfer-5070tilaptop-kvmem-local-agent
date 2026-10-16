# 部署与维护交接手册：给使用者和 Agent

适用：本仓库的 Windows / RTX 5070 Ti Laptop 12GB / Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP 部署。版本与参数核对日期：**2026-10-16**。先读本文，再按需查阅专项文档；仓库更新后以当前脚本、清单和 lockfile 为准。

**部署目标：保住已有模型质量、长上下文和缓存续接速度，先完成已验证的默认部署，再逐项优化。** 维护者已在这台 12GB 笔记本上稳定使用，并反馈 200K 上下文日常任务仍稳定、重复任务更快；满 256K 的整模型压力测试尚未完成。其他 RTX 50 系 12GB 以上设备欢迎验证，非 50 系需自行编译对应架构。

**特别感谢 Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)：维护者使用的基础架构来自他分享的版本，此前死循环问题也由他解决。** 本仓库是在该基础上继续适配，完整归属见 [上下游与许可](UPSTREAM_DOWNSTREAM.md)。

## 1. 可直接交给部署 Agent 的提示词

复制以下文本，并填写安装目录、模型路径和硬件。本文中的命令均按“已进入项目根目录”编写。

仓库根目录的 [AGENTS.md](../AGENTS.md) 也指向本文，供支持仓库指令的 Agent 自动发现；它不会把整份手册塞进每一次普通模型请求。

```text
请部署或维护这个仓库。先读 docs/DEPLOYMENT_AGENT_GUIDE.md、docs/INSTALL.md，
以及任务目录内适用的 AGENTS.md；核对实际源码，不把文档里的推测当成实测。

项目目录：<例如 D:\AI\ninfer-local>
模型路径：<v3 .ninfer 路径；没有则按 docs/MODELS.md 下载并转换>
机器：<显卡完整名称/显存、CPU、RAM、Windows、驱动版本>
当前任务：<首次部署/启动失败/缓存变慢/工具失败/优化>

先做只读检查：版本、GPU 占用、端口、已有服务、模型及依赖文件。
下载、venv、缓存、日志尽量放项目所在 D 盘；沿用隔离的 runtime/dsh-home。
新部署按固定版本与 SHA-256 获取依赖，使用 npm ci，不擅自升级到 latest。
保留 xhigh、采样、量化、检索预算、MTP 和缓存策略，先建立可复现基线。
新部署先从完整保守组合 DeviceKV=36864 / RetrieveTokens=3840 起步，
配套 sink=12288、recent=19456、HostKV=7168MiB、rk4v4、MTP3、chunk128。
不要直接复制维护者进阶档39168/6144，更不要只缩页池而保持装不下的窗口。
先清理实际独显占用，通过启动、短任务、缓存和工具续接，记录CUDA余量；
之后逐档加大，并验证相应长度的召回与峰值，失败恢复上一档。
CUDA 驱动系统内存回退强烈建议仅对实际引擎 exe 禁用；保留 7 GiB Host KV。
核显/独显混合模式和 Edge 核显偏好由用户配置；公开脚本不自动改驱动或显卡模式。
不要批量杀掉 node/Edge/Python，不修改全局代理，不清空模型或会话。
涉及停止正在工作的服务、驱动/显卡设置或资源压力测试时，先说明实际影响并遵循授权。
女娲及人物技能只在用户明确点名时启用；普通部署和 debug 不自动接入。

下一步明确时，用读取、最小实现或测试取得新证据，再继续深入思考。
复杂问题保留充分分析；不用缩短思考、删历史或强制结束来伪装提速。
每次只改变一个相关变量，记录前后版本、参数、指标和回滚方法。
分别验证引擎 API、缓存守卫、dsh、工具；网页打开不代表整套 Agent 验收成功。
交付实际通过/失败的检查、相关文件和未验证项；按真实结果更新待办。
公开材料必须脱敏，不能上传登录 URL/token、用户会话或原始私有日志。
```

## 2. 版本清单与实际来源

**源码 `main` 与引擎 Release 是两层版本。** 当前安装器下载包含检索上下文、decode 历史索引和缓存问句保护的 `v0.1.2` 引擎；后续 dsh 模块、脚本和文档可由新的源码提交提供。报告问题时同时记录 `git rev-parse HEAD` 与引擎 SHA-256，不能只说“最新版”。

| 组件 | 当前固定值 / 验证值 | 核对位置与说明 |
|---|---|---|
| 发布引擎 | `v0.1.2`，Windows x64 / `sm120a` | [下载清单](../config/downloads.json)；实际 exe 哈希见下方 |
| 原源码链 | 分享包报告 `v0.11.0-rtx3090`，KVMem 参考 `65dbd4f2` | [来源记录](../SOURCE_PROVENANCE.json)；缺少原完整 Git 历史，不当作当前二进制版本 |
| CUDA 构建工具链 | `13.0.48`；MSVC v143 `14.44.35207` | [源码编译](BUILD.md)；运行预编译包无需完整 CUDA Toolkit |
| cuBLAS | `13.0.0.19`，`cublas64_13.dll` / `cublasLt64_13.dll` | NVIDIA 固定归档，安装器逐文件校验 |
| FFmpeg | `N-127527-g045dea2a34`，BtbN `autobuild-2026-10-14-13-03`，LGPL shared | `avcodec-63`、`avformat-63`、`avutil-61`、`swresample-7`、`swscale-10`；不混用其他 ABI |
| NVIDIA 驱动 | 本机实测 `617.14` | 这是实测版本，不是声称唯一可用或最低驱动；其他版本须兼容 CUDA 13 |
| Node.js | 要求 `24+`；本机此次检查 `24.14.1` | npm 随 Node，安装程序从 PATH 查找 `node.exe` / `npm.cmd` |
| dsh | `@deepseek-ai/dsh@0.1.5-rc.3` | [包定义](../integrations/dsh/package.json)、[完整依赖锁](../integrations/dsh/package-lock.json) |
| dsh 本地 compaction 补丁 | `dsh-compaction-basic`、`dsh-compaction-tool-result-pruner` 均只接受 `0.1.5-rc.3` | 配置生成器有版本检查，不覆盖其他版本 |
| MCP SDK / DOM 解析 | `@modelcontextprotocol/sdk@1.32.1` / `linkedom@0.18.13` | [联网工具依赖](../integrations/dsh/local-web/package.json) |
| 浏览器驱动 / HTTP | `playwright-core@1.64.0` / `undici@8.11.0` | [联网工具完整依赖锁](../integrations/dsh/local-web/package-lock.json) |
| 浏览器 | 本机 Microsoft Edge，版本未固定 | Playwright 使用 `msedge` channel，不下载整套 Chromium；网站/Edge 更新后仍需验证 |
| VC++ 运行库 | Microsoft x64 VC++ Redistributable | 从 Microsoft 安装，未固定独立安装包版本 |
| Python | 模型转换文档使用 `3.11`；开发规则测试使用 `3.10.11` | 纯文字服务/网页运行不要求 Python；基准脚本只用标准库 |
| 转换依赖 | CPU torch、numpy、safetensors、huggingface_hub、gguf | [转换流程](MODELS.md)；该流程未完整锁定 Python 包版本，成功转换时记录实际版本 |

本版引擎 `ninfer-serve.exe` 的 SHA-256：

```text
275ea27ba01b57ca3f3a14361e5f7a4d8c4b04575475f1fe536ca5dedb6c8bd1
```

模型精确文件：`ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF` 的 **`Qwen3.8-27B-GSQ-RCO-IQ3_XXS-mtp.gguf`**，转换成含 `text,mtp` 的 **v3 `.ninfer`**。模型 revision、tokenizer 来源与转换命令见 [MODELS.md](MODELS.md)。公开包不包含权重；不能把 GGUF 路径直接传给服务器，也不能将不带 MTP 的文件套用这套启动参数。

## 3. 启动前：先留够显存和 RAM

1. 接电，确认机器散热、功耗模式和其他 GPU 工作负载；对比速度时保持这些条件一致。明确 GPU 是 **Laptop** 型号，桌面同名显卡不是同一硬件。
2. 先识别已有引擎和任务。本目录已有实例用 `stop.cmd` 关闭；其他目录启动的实例先确认归属。不能用 `taskkill /IM node.exe /F` 一类批量清理方式。
3. 退出不需要的游戏、独显加速应用、视频处理和其他模型实例；任务管理器查看“GPU 引擎/专用 GPU 内存”，同时检查 `nvidia-smi`。WDDM 下进程级统计可能不完整，不能只查 CUDA compute 进程。
4. 推荐混合显卡模式，让桌面和浏览器使用核显。公开脚本**不切换 MUX、不修改 DWM、不自动切换桌面渲染 GPU**。无核显也能尝试，需额外留出显示占用。
5. **强烈建议针对引擎 exe 设置 `CUDA - Sysmem Fallback Policy → Prefer No Sysmem Fallback`**，在“程序设置”中操作，保留全局设置；[具体步骤](INSTALL.md#cuda-系统内存回退强烈建议禁用)。不要把这项设置误当成关闭 KVMem Host KV。
6. 为 **7 GiB Host KV、模型映射、索引、dsh/浏览器和系统**留 RAM；本机为 32GB。Windows 内存压力和分页同样会拖慢服务。SSD 空间也要覆盖下载、GGUF、转换产物和缓存；不能按“模型文件约 10GB”计算全部需求。

只读检查：

```powershell
node --version
npm.cmd --version
nvidia-smi --query-gpu=name,memory.total,memory.used,memory.free,utilization.gpu,power.draw,temperature.gpu --format=csv
Get-NetTCPConnection -State Listen -LocalPort 1234,1236,3080 -ErrorAction SilentlyContinue |
    Select-Object LocalAddress,LocalPort,OwningProcess
```

默认长任务曾采样到约 **11,122 MiB** GPU 峰值，但该数字来自特定任务且每 0.5 秒采样，不是启动显存的统一门槛，也不能排除瞬时峰值。预填充、Graph 捕获、尾页复制和 MTP 都需要余量。**显存不够先找实际占用；需要缩减参数时，要说明对上下文、召回和速度的影响并重新验收。**

## 4. 首次部署：路径与顺序

推荐将整个仓库解压/克隆在 `D:\AI\ninfer-local` 等短路径；修改前确认当前目录。以下步骤在项目根目录执行。

若需下载/转换模型，可先将当前终端的缓存指向本项目；仅对当前进程及其子进程生效：

```powershell
$deploymentRoot = (Get-Location).Path
New-Item -ItemType Directory -Path '.\runtime\temp','.\runtime\cache\pip','.\runtime\cache\huggingface' -Force | Out-Null
$env:TEMP = Join-Path $deploymentRoot 'runtime\temp'
$env:TMP = $env:TEMP
$env:PIP_CACHE_DIR = Join-Path $deploymentRoot 'runtime\cache\pip'
$env:HF_HOME = Join-Path $deploymentRoot 'runtime\cache\huggingface'
```

按 [模型转换文档](MODELS.md) 准备制品；已有正确 `.ninfer` 可直接安装：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\qwen3.8-27b-gsq-rco-iq3-xxs.ninfer"
.\start.cmd
```

安装器从发布方获取固定依赖并校验 SHA-256；下载、npm 缓存、临时文件、会话和日志放本项目。运行环境仍依赖系统已安装的驱动、VC++、Node 和 Edge，脚本不迁移这些系统组件。

**不要跳过校验来解决下载问题，也不要用不明 DLL 补齐启动错误。** 下载失败先查网络/具体归档，损坏缓存只清理已确认损坏的那一份，再重新下载。完整干净源码编译不是默认部署前提；非 50 系及自建引擎按 [BUILD.md](BUILD.md) 操作。

只用 API：安装加 `-SkipDsh`，启动使用 `powershell -NoProfile -ExecutionPolicy Bypass -File .\start.ps1 -EngineOnly`。以后要加网页，再执行不带 `-SkipDsh` 的安装。

## 5. 启动设置：哪些值在哪里生效

**先用较小页池的完整默认组合 `36864 / 3840` 建立基线，清理实际显存占用并先验收；通过后再逐档增加。** 维护者 `39168 / 6144` 是单机进阶档，CUDA 启动余量约 84 MiB；不是所有 12GB 设备的起点。只改小页池却保留过大的窗口会引发内部页耗尽。[预算与验证顺序](INSTALL.md#先从较小页池部署再逐档增加)

`config/local.json` 为本安装的配置，Git 忽略；样例在 [config/example.json](../config/example.json)。默认使用这个标准位置：目前 dsh 配置生成器读取的就是它，不能假设 `start.ps1 -Config` 会让 dsh 同步读取另一个 JSON。

| 设置 | 默认值 | 实际含义 / 修改位置 |
|---|---|---|
| `ModelPath` / `EnginePath` | 模型路径 / `runtime/engine/ninfer-serve.exe` | JSON；改引擎时核对 ABI、哈希与驱动程序设置路径 |
| `ModelId` | `qwen3.8-27b-gsq-rco-iq3-xxs` | 服务别名，客户端请求须与之相同 |
| `EnginePort` / `GuardPort` / `WebPort` | `1234` / `1236` / `3080` | 都是本机回环服务；改 JSON 后重启 |
| `MaxContext` | `262144` | 逻辑容量；需要同时容纳输入、输出及开销 |
| `DeviceKV` | `36864` tokens | GPU KV 页池容量，与逻辑上下文上限不同 |
| `HostKVMiB` | `7168` MiB | RAM 历史 KV 预算，不等于 GPU 可用页数 |
| `RecentTokens` / `SinkTokens` / `RetrieveTokens` | `19456` / `12288` / `3840` | 近期、固定头部、检索窗口；共同影响页预算和召回 |
| KV dtype / 并发 | `rk4v4` / `1` | [start.ps1](../start.ps1)；改变它们需重新测质量、显存及稳定性 |
| prefill chunk / Graph allowance | `128` / `64 MiB` | `start.ps1`；不能只凭更大数字断言更快 |
| 推测解码 | `--spec mtp --draft-tokens 3` | `start.ps1`；动态 MTP 未在本预设启用或验证 |
| GDN / device state slots | `--gdn-state-fp16` / `0` | 当前已验证的引擎配置，见启动脚本 |
| `ReasoningEffort` | `xhigh` | 引擎默认与初次生成的 dsh 默认路由；已有 dsh 设置/会话可能覆盖，需检查实际请求 |
| `ToolGroundedExecution` | `true`；旧 JSON 缺省也启用 | 新执行规则开关；改为 `false` 后重新生成配置，见 [接入说明](../integrations/README.md) |
| dsh 最大输出 | `73728` tokens | [configure-dsh.mjs](../scripts/configure-dsh.mjs) 的模型路由；输入很长时仍受总上下文约束 |
| 压缩配置 | threshold `0.703125`，约 `184320`；retain `30146`；summary max `32768` | [preset 模板](../templates/dsh-preset/agent.cordis.yml)；安装后复制到隔离 home，已有 preset 不自动覆盖 |
| 缓存守卫 | strict `1`；恢复预算估计上限 `210000` | `start.ps1` / [cache_guard.mjs](../integrations/cache_guard.mjs)；估计值不是精确 tokenizer 计数 |

采样要区分模型家族默认、本分享包预设和请求覆盖：

| 阶段 | 当前常规预设（客户端未另行覆盖时） |
|---|---|
| 思考阶段 | temperature `1.0`、top_p `0.95`、top_k `20`、min_p `0`；启动覆盖 presence `1.0` |
| 思考结束后的回答 | 启动指定 `temp=0.7,top_p=0.95,presence=1.0`；未覆盖字段由引擎相应默认解析 |

presence `1.0` 是当前部署的存在惩罚；它与乘法式 `repetition_penalty` 不是同一个参数。该引擎 Chat API 对 `repetition_penalty` 只接受中性值 `1`，不能直接套别的引擎的 `1.1`。当前 presence 与思考后采样来自基础分享包配置，**不能把整套预设都称为 GSQ-RCO / Qwen 官方推荐**。请求、dsh 会话设置可能覆盖默认，调试时以实际请求为准；保留生产采样建立基线后再做独立对照。

启动脚本还设置以下进程环境；保留它们作为一组已验证条件，不要只复制 exe 或一条精简命令就宣称复现了本项目。

| 环境变量 / 选项 | 启动值 |
|---|---|
| `CUDA_MODULE_LOADING` | `LAZY` |
| `NINFER_KVMEM_INDEX_HOST` | `1`，检索索引放 Host RAM |
| `NINFER_KVMEM_INDEX_DECODE` / `NINFER_KVMEM_CACHE_QUERY` | `1` / `1`；补已提交 decode 历史索引、对合法缓存问句恢复重评分；显式 `0` 可作诊断对照 |
| `NINFER_KV_RING` / `NINFER_KV_REUSE_HOSTBACKED` | `1` / `1` |
| `NINFER_KV_WINDOW` / `NINFER_KV_SINK` / `NINFER_KV_RETRIEVE` | 分别取 JSON 的 recent / sink / retrieve |
| `NINFER_HOST_PAGEABLE` / `NINFER_KV_LEASE_RING` / `NINFER_KVMEM_HARVEST_TEXT` | 都为 `1` |
| `--kv-lease-growth` / `--recover-invariant-failures` | 启用 |
| `--context-cache-policy` / `--max-shared-prefixes` | `rolling` / `0` |
| `--release-diverged-checkpoints` / `--recency-eviction` | 启用 |
| `GUARD_STRICT` / `GUARD_MAX_TOKENS` | `1` / `210000` |
| `DSH_PRUNE_AT_OCCURRENCE` | `1` |

此前基准若设置过 `NINFER_VEC5070_DISABLE=1`，会关闭本机小矩阵调度；启动脚本不会替你清掉终端继承的这个开关。恢复默认时，仅在相应终端移除该环境变量后重启引擎；已经捕获的 CUDA Graph 不能热切换。不开启 profiling 探针作为日常测速条件。

## 6. 日常启动、关闭和更新

| 操作 | 方法 |
|---|---|
| 完整 Agent | `start.cmd`：引擎 ready 后启动缓存守卫与 dsh，再打开私有登录 URL |
| 不自动打开网页 | `start.cmd -NoOpen`；在本机查看 dsh 日志中的登录 URL |
| 仅引擎 | `start.cmd -EngineOnly` |
| 关闭本安装 | `stop.cmd`；按记录的 PID、启动时间、路径及标记核对归属 |
| 已经启动再双击 | 会拒绝重复启动；不要因此杀掉不相干进程 |
| 插件配置重新生成 | `node .\scripts\configure-dsh.mjs`；需要已完成 dsh 安装 |

关闭会释放该引擎的 GPU 缓存，**模型文件和磁盘会话保留；再次打开会话不代表 RAM/GPU KV 跨进程保留**。公开关闭脚本保留应用设置，用户手动设置的 NVIDIA 程序策略和 Edge 偏好也不会自动恢复。不要照搬私人旧脚本的“自动切显卡/关闭后恢复”行为描述。

更新先记录当前 commit，备份本机 `config/local.json` 和需要保留的 `runtime/dsh-home`。确认工作任务结束后关闭本安装，检查本地改动，再更新源码、按 lockfile 执行安装；不要对用户目录做 `git reset --hard` 或 `git clean -fdx`。`setup.ps1` 保留已有配置；生成器保留无关 patch；**已存在的 preset 不会被模板自动覆盖**，需要人工对比合并。

不要只升级 dsh 依赖后继续覆盖旧补丁；版本检查报错时先恢复项目固定版本或单独完成适配验证。回滚需同时考虑源码、配置和依赖锁；模型/会话作为用户数据保留，勿用它们做试验性回滚。

## 7. 分层验收：先知道坏在哪一层

调用链：**dsh `3080` → 严格缓存守卫 `1236` → 引擎 `1234` → GPU / Host KV**。搜索通过 Node 访问网络；浏览器为按需启动的独立 Edge。

在本机读取状态，端口以实际配置为准：

```powershell
$localCfg = Get-Content -LiteralPath '.\config\local.json' -Raw | ConvertFrom-Json
$engineBase = 'http://127.0.0.1:' + $localCfg.EnginePort
Invoke-RestMethod -Uri ($engineBase + '/health')
Invoke-RestMethod -Uri ($engineBase + '/v1/models')
Invoke-RestMethod -Uri ($engineBase + '/v1/load')
```

引擎空闲且可以占用 GPU 做检查时，先做短验收；以下程序会发起真实生成：

```powershell
python .\benchmarks\run.py --mode smoke --url $engineBase --model $localCfg.ModelId --label deploy-engine
$guardBase = 'http://127.0.0.1:' + $localCfg.GuardPort
python .\benchmarks\run.py --mode smoke --url $guardBase --model $localCfg.ModelId --label deploy-guard
```

仅引擎模式跳过第二条。smoke 验证数学、结构化工具调用与工具结果；该 API 夹具不会证明 dsh 已正确注册工具，也不会验证 xhigh 下所有真实任务。基准代码显式使用 temperature `0` / seed `1234` / reasoning `none`，只影响测试请求，不要据此改变日常设置。

网页再单独验证：选择 `ninfer-local` 和 xhigh，完成一个临时目录内的“读文件 → 写最小实现 → 执行测试 → 更新待办 → 正常结束”任务；用一个公开网页检查搜索/读取，并通过浏览器打开页面、读取快照、执行一次动作。检查真实工具目录，浏览器工具可显示为 `mcp__browser__browser_open` 等名称。工具没有注册时，模型无法靠提示词凭空获得它。

长任务改动再按 [基准说明](BENCHMARKS.md) 验证冷输入、重复请求，以及**长输出 → 工具调用 → 多轮续接**，连同召回正确性一起检查。只看“能生成”或短请求 token/s，不能证明长 Agent 不会越用越慢。

## 8. 调试：先取证，再改一个变量

| 位置 | 能判断什么 |
|---|---|
| `runtime/logs/engine.stderr.log` / `engine.stdout.log` | 加载、CUDA、页池、prefill/decode、缓存与 MTP 等引擎信息 |
| `runtime/logs/cache_guard.log` 与 guard 的 stdout/stderr | 请求是否经守卫、是否恢复或因变化/上限拒绝恢复 |
| `runtime/logs/dsh.stderr.log` / `dsh.stdout.log` | 插件、配置、工具连接、网页登录公告；stdout 含私有 token |
| `runtime/processes.json` | 本安装记录的进程身份；不是所有占用 GPU 的进程列表 |
| `runtime/dsh-home/settings.yaml` | 实际 provider/model/default；旧设置可能被保留 |
| `runtime/dsh-home/profiles/web/cordis.patch.yml` | 实际模块 URL、开关、搜索与浏览器注册 |
| `runtime/dsh-home/.agent-presets/ninfer-local/` | 已安装 preset 的压缩与工具配置 |
| `runtime/dsh-home/compaction-dumps/`（如生成） | 压缩前的消息备份，**含会话内容，私有保管** |
| `runtime/benchmarks/` | 自己运行的夹具结果，默认不进 Git |

日志仅在本机查看，先取相关末尾，避免把整轮思考/网页/文件原文贴到公开 Issue：

```powershell
Get-Content -LiteralPath '.\runtime\logs\engine.stderr.log' -Tail 80
Get-Content -LiteralPath '.\runtime\logs\dsh.stderr.log' -Tail 80
Get-Content -LiteralPath '.\runtime\logs\cache_guard.log' -Tail 40
```

判断速度时分开记录：冷 prefill、实际新算 tokens、缓存命中 tokens、恢复/TTFT、decode token/s、输出长度、MTP 接受率、GPU 温度/功耗/占用。**不能用全部历史 token 数除以缓存恢复的几百毫秒来宣传预填充吞吐**。同一 token 前缀、相同请求顺序、相同启动/缓存状态下对照，避免并发测试和热状态差异。

## 9. 已踩过的坑与处理方式

### 9.1 双击没反应、网页没有自动打开

先在 PowerShell 手动运行 `start.ps1` 保留错误输出，核对路径、Node、VC++、DLL、驱动和端口。引擎加载与 Graph 捕获期间要等 ready，不能把加载期当成 dsh 故障。`-NoOpen` 本来就不打开网页；API-only 模式也不启动 dsh。

旧脚本曾在 dsh 日志尚为空时直接将结果交给 `[regex]::Match`，触发“值不能为 null”。当前 `start.ps1` 先检查日志非空，再匹配当前端口的公告并验证登录 URL。遇到相同堆栈，先确认运行的是当前仓库脚本，不要给模型参数乱改一遍。登录 URL 只在本机打开，不能公开分享。

### 9.2 CUDA OOM、速度忽然很低

检查真实 GPU 占用、是否启动两份模型、浏览器是否在独显上、CUDA 程序回退策略及 RAM 压力。驱动回退可能使分配暂时继续，却带来很大延迟；为正确 exe 禁用回退并预留显存。禁用后仍不足会报错，不能靠这项设置增加物理显存。

**Host KV、RAM 检索索引与驱动自动回退是不同机制。** 本项目主动把历史和索引放 RAM 以节省显存；关掉它们会破坏原预算。GPU scratch、活动页、MTP、状态和临时空间仍需显存，不能把所有内存都随意卸载。

### 9.3 `Device KV page pool exhausted ... host-backed ... claimed on demand`

这是引擎页面分配/恢复路径的容量问题，不能直接解释为“系统 RAM 不够”或“Host KV 太小”。旧问题出现在**大缓存接管**，涉及主模型/MTP/临时恢复页的统一预算、尾页 COW、按需恢复以及异步释放生命周期。

本版包含相关修复，先核对 exe 哈希并恢复完整默认预算；保存失败前后的输入长度、输出长度、工具顺序、`/v1/load` 和引擎错误。验收须重现长生成后的工具续接。**仅增 Host KV、仅关 MTP或压低检索窗口，不足以证明根因修复**；若仍复现，应作为新问题定位，而不是承诺已经覆盖所有路径。

### 9.4 上下文变长后 prefill 慢，Agent 越用越慢

先分清冷输入和缓存恢复。历史、system prompt、工具表、模板、思考内容、工具结果或压缩发生变化时，变化位置之后可能重算；重启引擎也会丢失内存缓存。核对 dsh 实际走 `1236`，不是直接绕过守卫。

严格守卫只恢复符合已定义等价条件或特定裁剪标记的原始内容，超过预算会拒绝恢复。不要把 `GUARD_STRICT` 改为 `0`，也不要用“文字前缀看起来差不多”强行复用。KVMem 召回是选择性注意力；扩大逻辑容量不等于免费读取所有历史，省略历史也不能当成等价提速。

新执行规则是固定文本，首次换用可能导致前缀重算，之后保持稳定。频繁更换系统规则/技能/工具会影响缓存；调试时先固定这些条件。

### 9.5 搜索失败、模型说没有浏览器

网页 API 本身不替模型联网；dsh 必须注册搜索 provider 和浏览器 MCP。检查 `cordis.patch.yml` 中 `web-local`、`mcp-browser`，实际工具列表以及 dsh stderr；确认本机装了 Edge。无付费搜索 API 也能通过本地后端访问公开搜索，但仍要真实网络，网站限流、验证码和结构变化会导致失败。

本项目使用 **Bing 页面/RSS + 本地正文提取**，没有部署 SearXNG。搜索无结果先缩短关键词、读取已知 URL；需 JavaScript/交互的页面使用浏览器。HTTP provider 使用固定的 Undici `fetch`，不要随意改为 Node 内置 fetch 混用 dispatcher：此前组合会丢响应头，影响重定向和解压。

### 9.6 `ERR_PROXY_CONNECTION_FAILED`

这说明浏览器无法连接配置的代理，并不能证明模型或 KV 出错。核对实际代理进程/端口、Windows/浏览器代理及当前进程的代理环境；本地 `127.0.0.1` 不应被误送到外部代理。代理应按用户的网络设置恢复，不要为修复 dsh 擅自关闭系统全局代理或写死维护者电脑的代理端口。

### 9.7 `page.evaluate: Execution context was destroyed`

导航/刷新会销毁旧页面 JavaScript 上下文。用 `browser_open` 或 `browser_action(action="reload")` 导航，再在**下一次调用**读取快照/evaluate；每次动作后使用最新元素 ref。不要在一个 evaluate 中 `location.reload()` 后继续等待旧上下文，也不要自动重放可能有写入/提交副作用的脚本。本版只为自有 DOM/GPU 读取做有限导航重试。

### 9.8 `sandbox escalation ... danger-full-access ... not strictly wider`

当前已是该调用的 `danger-full-access`，再请求同一模式并没有扩大权限，因此被 dsh 的升级校验拒绝。让模型使用当前权限正常执行，移除多余的 `sandbox_permissions`/升级请求；先检查真实会话权限和工具参数。这个错误本身不表示 GPU、Windows 管理员权限或模型损坏，也不需要关闭整个权限机制。

### 9.9 思考很久、输出被截断，是否死循环

查看实际 finish reason、输出额度、思考是否重复、是否有新证据和工具调用。长思考、输出额度用完、工具等待和真正重复循环有不同原因，不能只凭耗时判断。保持 xhigh 及必要分析，优先使明确的下一步落到文件检查、最小实现和测试，再由新结果推进。

`tool-grounded-execution.mjs` 提供固定引导，不设思考上限，不改采样/最大输出，也不硬截流；实测只覆盖一个开发夹具的两轮对照，不能保证所有任务收敛。当前存在惩罚也不能代替框架调试。不要默认用强制关闭思考、降低 xhigh 或任意提高惩罚解决问题；需要这类实验时独立对照成功率与交付质量。

实际排查还遇到过：截图成功后，约 177K 历史仍在请求中、缓存几乎全部命中，模型却重新规划整个任务；压缩发生在之后，不能把该次现象归因于当时压缩。纯文字路由收到的截图结果只有路径，不能据此看图。后续接入在截图结果后追加验收进度提示，保留旧前缀与思考额度；它缓解进度偏离，尚不能证明底层 KV 召回没有问题。

进一步只读审计发现检索索引的生命周期缺口：某次约 11K 输入的冷请求，`kvmem_score` 仍报告 `n_blocks=2313`（约 148K）；后续压缩到约 61K 时仍报告 `3207`（约 205K）。源码中的 `mean_k_index_for()` 使用进程级单例，只在首次创建时调用 `zero()`，append 累加 sum/count；没有随 continuation 绑定或恢复索引。多轮工具续接若最后一条 user 消息已在缓存内，也可能不产生新一轮语义打分而沿用旧选择。`oracle_ok=1` 只检查分数归一化，不能证明索引属于当前上下文或选页正确。它们是需要隔离验证的检索问题，不能据此断言物理 KV 页面损坏，或认定就是截图后重新规划的唯一原因。

`v0.1.1` 原生修复已将索引、评分及采集工作区归 Program 所有，检索索引与选页排名随 endpoint / rewrite / long-anchor / shared-prefix 检查点一起保存和恢复。索引快照在普通 RAM 中按 16 页分片共享，修改片段使用 COW；新根上下文重置，缓存续接恢复对应版本，不能每次请求无条件清空。追加接口现在接收绝对 token offset，跨 64-token 页边界拆分，重复前缀不重复累加；评分使用每层真实 count，缺失页不再当成满页。评分版本变化也会刷新 ring 选择，避免页数未变时继续用旧选择。49K→新短会话→49K 与累计 43,775-token 长生成后的工具/三轮续接已通过，详见 [实测](BENCHMARKS.md#原生检索上下文修复2026-10-15)。这仍不能证明原 177K 截图事件的模型行为已完全解决。

该原生补丁随 `v0.1.1` 源码和引擎 ZIP 发布，`config/downloads.json` 固定新归档及 exe 的真实哈希；`v0.1.0` 资产保留原样，供对照和回滚。[源码归属记录](../SOURCE_PROVENANCE.json) 同时记录两个版本。升级时先用本安装的 `stop.cmd` 退出，再更新源码并按 [安装说明](INSTALL.md#从-v010-升级) 重跑安装器，不能只更新 JS 文件就认为原生索引已修复。新增快照消耗 RAM，256K 完整索引约 256.25 MiB；共享前缀复用分片，互不相关的缓存上下文各占自己的片段，另需原活动索引空间。GPU 工作区预算保持原样。KVMem 内容检索目前明确仅支持单并发、Legacy 缓存；不支持的 Hybrid/多并发会在启动时报错，避免静默串用状态。

已知边界：decode 新生成的 K 尚未全部进入 mean-K 语义索引，原始 KV 仍由原页存储保留；工具续接的最后一条 user span 若已全部缓存，仍可能继承同一上下文之前的选择，不保证每轮重新打分。磁盘/跨进程缓存不携带本次内存索引快照；尚未验证这一路的语义索引恢复。不要把稀疏 count、零新增 SELECT 与物理页丢失混为一谈，也不要用修改采样、缩减上下文或淘汰 Host 历史掩盖问题。

若停止后下一轮仍排队，检查取消链路：旧缓存代理缺少下游断开到上游的传播，曾出现界面已停止、引擎仍继续生成数分钟。本版守卫已补传播；更新守卫代码后需要重新启动该代理进程才生效。不要把磁盘文件已更新误认为旧进程已加载新代码。

### 9.10 交付正确，但待办停在 1/3

普通回复不会自动改变 `todo_write` 面板，这与真正的 plan mode 是不同状态。现有收尾插件在特定条件下给模型一次核对提醒，由模型按真实结果更新；不直接将任务改成成功，取消/失败/截断/计划模式也不强行结束。验收时查看测试结果、最终交付和待办是否一致；仍未完成的事项保留原状态。

### 9.11 加了 mmproj，CPU 视觉仍慢或显存有变化

默认启动器是纯文字 + MTP，不能把 mmproj 放进目录就当视觉已启用。需要独立的 `text,vision,mtp` 制品、`--vision-cpu --vision-max-merged 256`，并把 dsh 模型 input 配成 `text,image`；[具体步骤和实测](VISION.md)。

视觉塔可放 CPU，但图片仍经过 GPU 上的文字推理；旧阶段该机器测得文字侧额外预留约 **1.27 MiB**，不是所有图片请求的通用上限。cap 256 / 512 / 1024 的旧阶段编码约 9.6 / 32 / 93 秒，分辨率和 CPU 影响很大，cap 还影响细节质量。不能承诺零显存增量或无延迟，先用小图单独验证。

### 9.12 非 50 系不能直接用、混入新上游版本后出错

本 Release 为 `sm120a`；非 50 系要按实际架构自编译，不能仅改文件名。其他 50 系也不自动获得本机 cc1200 / 46 SM 小矩阵专用调度；它只匹配指定形状。新上游 KV/检索改进可参考，但不能未经适配就混用源码、二进制、DLL 或参数。动态 MTP 目前仍是未验证的优化方向。

## 10. 清理、保留与发布边界

显存清理优先关闭归属明确的 GPU 工作负载；删除磁盘缓存不会直接腾出正在运行程序的显存。磁盘清理前确认相关服务已停止，先列出路径、用途和大小，再处理已确认可重建的文件。

| 数据 | 处理原则 |
|---|---|
| 模型 GGUF、mmproj、`.ninfer` | 用户资产；验证转换成功并确认无需重转后，才考虑清理重复源文件 |
| `runtime/dsh-home`、preset、会话、compaction dumps | 私有配置与任务记录；先备份，不当作“无用文件”批删 |
| `runtime/logs`、`runtime/benchmarks` | 先保留诊断和关键结果，脱敏后可归档；故障未定位时不要先清空 |
| `runtime/cache` 中已确认的安装归档/解压副本 | 安装文件哈希通过后可按需清理，之后可能需要重下；避开正在写入的 `.part` |
| `runtime/engine`、本安装 `node_modules` | 运行依赖；不要在服务工作时清理，移除后需重装 |
| `integrations/dsh/local-web/edge-profile` | 含浏览器状态/登录；关闭工具浏览器后按需备份、重建 |
| `integrations/dsh/output` / `downloads` | 截图和下载物可能是实际交付，先检查内容 |

公开仓库应保留源码、脚本、锁文件、归属和脱敏证据；不上传模型、用户配置、会话、压缩备份、凭据或原始私有日志。出现故障时提交：硬件、驱动、commit、exe 哈希、相关参数、最短复现步骤、错误文本与脱敏指标；**不要提交带 `?token=` 的 dsh 登录地址**。

## 11. 交付给下一位维护者时应写什么

- 当前运行目录、commit、引擎/依赖版本、模型来源与制品组件；是否仍为默认文本预设。
- 实际启动参数、dsh route、xhigh、输入/输出预算；修改过哪些值、原因及回滚方式。
- 显卡模式、CUDA 程序回退策略、启动前/运行时显存、RAM、功耗与温度条件。
- API、守卫、dsh、联网、浏览器、工具续接分别通过哪些检查；提供可复现结果并注明未验证项。
- 冷/热 prefill、缓存命中、新算 tokens、TTFT、decode、接受率与正确性，分开报告。
- 服务是否仍运行、如何关闭、保留了哪些备份；下一步最小行动是什么。

专项入口：[安装与驱动设置](INSTALL.md) · [模型与转换](MODELS.md) · [源码编译](BUILD.md) · [dsh 集成](../integrations/README.md) · [实测边界](BENCHMARKS.md) · [CPU 视觉](VISION.md) · [修改范围](MODIFICATIONS.md)。
