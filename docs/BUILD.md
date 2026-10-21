# 从源码编译

预编译引擎供 Windows x64 / sm120a 使用。本机开发工具链是 CUDA **13.0.48**、MSVC v143 **14.44.35207**，兼容路径 `NINFER_SM8X_COMPAT`，`NINFER_SM120_NATIVE=OFF`、PDL=OFF、D3D12 residency=ON。显卡调度只对 cc1200、46 SM 指定形状启用。

原分享包中的 Git worktree 与编译缓存指向作者电脑，因此没有把其静态库、MSVC、完整 CUDA SDK 或损坏 `.git` 上传。请在自己的环境配置干净构建；不要使用上游文档的 RTX 3090/sm86 测速来推断本版性能。

## Windows 前置条件

- Visual Studio 2022 Build Tools：C++ x64 工具链、Windows SDK；打开 **x64 Native Tools Command Prompt**，或在 PowerShell 导入对应 vcvars 环境。
- CUDA Toolkit 13.0，选择兼容的 MSVC v143；CMake ≥ 3.28、Ninja。
- [vcpkg](https://github.com/microsoft/vcpkg)，依赖清单在 `engine/vcpkg.json`：FFmpeg、curl、pkgconf。保持 FFmpeg LGPL 配置，勿无意开启 GPL codec 特性。

在已加载 MSVC 与 CUDA 的 PowerShell 中：

```powershell
$vcpkgRoot = 'D:\Tools\vcpkg'
cmake -S engine -B runtime/build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_CUDA_ARCHITECTURES=120a `
  -DNINFER_SM120_NATIVE=OFF -DNINFER_D3D12_RESIDENCY=ON `
  -DNINFER_MEDIA_NATIVE_PNG=ON -DNINFER_NVCC_SPLIT_COMPILE=2 `
  -DNINFER_BUILD_APPS=ON -DBUILD_TESTING=ON
cmake --build runtime/build --target ninfer-serve -j
```

完整 CUDA 模板构建会占用大量 CPU RAM 和磁盘；并行编译触发实际资源不足时降低任务数。源构建得到的 DLL ABI 可能和预编译 Release 不同，使用该构建配套的依赖，不能盲目混合两个 FFmpeg 版本。把 `config/local.json` 的 EnginePath 指向自己构建的程序即可。

## 验证范围

本次公开整理验证了新目录里的原生产引擎、LGPL FFmpeg 替换运行库、API、启动/关闭与 dsh 配置；没有重新做一遍耗时的全量 CUDA 源构建。历史修复的相关测试与数值验证随源保留。

后续原生检索修复在本机使用相同 CUDA 13.0.48 / MSVC 14.44.35207 / sm120a 增量编译：重编全部 `program_impl.h` 消费单元及既有 Host/KV 修复、评分与绝对位置追加 CUDA 单元，并重编 IQ2_S / IQ3_XXS / Q2_K 单元保留已验证的 5070 Ti 调度，链接原构建的其余静态库。通过 Host/Device 索引 FP64 oracle、预取/评分生命周期、页存储及真实模型回归；没有执行全量上游测试或干净 CUDA 全量构建。[v0.1.1 实测及验证边界](BENCHMARKS.md#原生检索上下文修复2026-10-15)。

测试入口在 `engine/tests/README.md`。按变更选择测试；例如 Host 索引、页存储、异步检索及小矩阵数值资格检查。不要在用户正在使用 GPU 时并发运行性能测试。`benchmarks/qualification/qualify_vec.cpp` 是此次实际权重解包、FP64 oracle 的独立资格检查程序，需与构建出的引擎库、CUDA 头文件及模型制品配套使用，不是 CPU-only 单元测试。

v0.1.2继续使用相同工具链增量重编全部Program/查询快照相关Host消费者、norm/RoPE采集CUDA单元及既有mean-K/检索和5070小矩阵单元，复用其余原静态库。`test_kvmem_decode_query`含30次Graph重放、独立FP64归一化、精确Q/K位对照、提交索引与缓存查询oracle；GPU测试通过的原始记录摘要在[语义历史实测](BENCHMARKS.md#语义历史修复与检索预算2026-10-16)。保留增量构建边界，不将源码公开等同于全量干净构建验证。

RTX 50 系、12GB 及以上显存的其他设备欢迎尝试并提交参数与实测；目前明确验证的是 RTX 5070 Ti Laptop 12GB。**非 50 系显卡须自行按实际 CUDA 架构重新编译**，不要原样照搬上面针对本机的 `120a` 参数。按上游对该架构的支持情况配置内核路径，再调整 Device KV / Host KV 等预算，并验证模型正确性和稳定性。欢迎通过 Issue / PR 补齐不同设备的编译步骤、适配修复和测速。

## v0.1.3 NVFP4 调度与独立检查

本轮 [NVFP4 SM120 调度](NVFP4_SM120.md) 复用同一工具链及其余原静态库，额外重编 `prompt_nvfp4.cu` 并保留全部既有 Host/KV、norm/RoPE、检索和小矩阵单元。它是增量构建，没有替代本页的干净全量构建流程；最终含完整 Host 压力修复的同一部署 exe 随 v0.1.3 打包发布。

独立资格程序不需要链接引擎静态库。在已初始化 CUDA 13.0.48 / x64 MSVC 14.44.35207 的开发终端可按等价命令编译（输出目录须先存在）：

```powershell
& "$env:CUDA_PATH\bin\nvcc.exe" benchmarks/qualification/nvfp4_prompt_sm120.cu `
  -std=c++20 '--generate-code=arch=compute_120a,code=[compute_120a,sm_120a]' `
  --use-local-env --extended-lambda --split-compile=2 -lineinfo `
  -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DNINFER_SM8X_COMPAT=1 -DNINFER_SM120_NVFP4=1 `
  -Xcompiler=/MD -Xcompiler=/O2 -Xcompiler=/EHsc -Xcompiler=/utf-8 `
  -Xptxas=-v -Xcudafe=--diag_suppress=177 `
  -I engine/src -I engine/include -I "$env:CUDA_PATH/include/cccl" `
  -o runtime/nvfp4_prompt_sm120.exe
```

本轮实际使用隔离目录里的 portable 工具链与 include 路径；完整编译命令和日志保存在本机维护目录。运行该程序会占用 GPU，应与模型性能测试分开。

## 本地累计缓存压力检查

累计 ring 缓存的 Host 压力回归已注册为 `ninfer_qwen3_5_ring_host_pressure_test`，可运行
`ctest --test-dir runtime/build -R "qwen3_5_(ring_host_pressure|context_store)_test" --output-on-failure`。
本机维护另使用相同工具链，将两个测试单独增量链接到原静态库；没有重配旧构建树或声称完成 CTest 全量构建。[问题与验证范围](KV_HOST_PRESSURE.md)。

资源管理器Host-only LRU检查包含在 `ninfer_resource_manager_test`，可运行
`ctest --test-dir runtime/build -R "^ninfer_resource_manager_test$" --output-on-failure`。本轮独立增量测试运行76项全部通过；完整v2重编全部模型Program单元、runtime engine及paged KV实现，再链接原静态库，仍不是干净全量构建。

## 本地未发布的工具输出修复（2026-10-20）

相同 CUDA 13.0.48 / MSVC 14.44.35207 / sm120a 工具链重建全部 v0.1.3 本地替换对象，并重编输出解析、结果处理和全部 serving 消费者及入口，共 70 个 Host 与 7 个 CUDA 编译单元，复用其余原静态库。spdlog 沿用原编译库模式 `SPDLOG_COMPILED_LIB`。受影响生产源码的 SHA-256 已与构建清单逐一核对；这仍不是干净全量构建。

已注册独立测试入口，在新的正常 CMake 构建树中可运行：

```powershell
cmake --build runtime/build --target ninfer_tool_output_recovery_test ninfer_qwen3_5_tool_recovery_test ninfer_request_log_test
ctest --test-dir runtime/build --output-on-failure -R 'ninfer_(tool_output_recovery|qwen3_5_tool_recovery|request_log)_test$'
python -m pytest engine/tests/test_serve_corpus.py -q
```

本机实际执行等价独立增量编译/链接，没有重配旧 CMake 树。定向解析器、前端、日志与 Python 2 项通过；完整继承组按函数隔离进程前后对照，原有 8 个解析器及 1 个前端失败保留，没有新增失败。真实模型 205124-token 召回和长历史工具续接通过。[实现、部署哈希与验证边界](TOOL_OUTPUT_RECOVERY.md)。公开下载清单继续固定已发布的 v0.1.3，本地候选不应直接按旧版清单打包。

同日第二批[原有失败修复](TOOL_PARSER_EDGE_CASES.md)仅重编工具解析器一个 Host 单元，复用上一批其余对象及静态库。两个完整测试程序和按函数隔离执行均通过：解析器 39/39、前端 54/54，解决原有 9 项失败；另有前后输出对照和真实 49452-token 召回/工具续接、刻意截断及错误后重试验证。没有重配旧 CMake 树、重跑全部 CTest 或重复 205124-token 测试。当前本地 exe 为 `d6c4f860c1d47680c68306aefba969d8c39f8ec792a49d6c743f51898d5fc96f`，与已发布 v0.1.3 下载哈希分别记录。

## 打包已验证的引擎

2026-10-20 的[思考后采样屏障候选](LONG_CONTEXT_TOOL_LOOP.md)变更 `RoundBudget` 契约，重编前轮全部 70 个 Host 替换单元，复用已验证 CUDA 对象和其余原静态库；随后增量修正输出状态及 Scheduler 耗尽预算处理。没有重配旧 CMake 树或完成干净全量构建。最终候选链接的前端 55 组、解析器 39 组和 18 个 Host 程序通过，但实际短测仍有误调用，候选不作为日常安装/发布引擎；确切哈希与结果见对应脱敏证据。

`scripts/package-engine.py` 核对 `SOURCE_PROVENANCE.json` 与下载清单中的 exe 哈希，打包程序、LICENSE、NOTICE 和静态依赖许可，校验 ZIP 内程序哈希及 CRC，更新清单的归档哈希并输出 `dist/SHA256SUMS`。它不编译、不下载依赖，也不上传；先完成构建和资格检查，再更新相应版本及来源记录。

```powershell
python scripts/package-engine.py --engine "D:\Build\ninfer-serve.exe"
python scripts/check-release.py
```

归档不含权重、DLL、SDK、会话或登录信息；发布时使用与源码标签匹配的新 Release，保留旧版本资产。

2026-10-20 的[耦合采样候选](COUPLED_SAMPLING.md)变更 ordinary ingress 布局，重编前轮70个Host替换单元及speculative wrapper，共71个Host，并用sm120a重编sampling、speculative两个CUDA单元；复用此前七个CUDA替换对象和其余原库，非干净全量构建。内部Op改名为coupled，外部自建C++消费者需重编。独立草拟、普通采样FP64分布、MTP状态、DecodeGraph、frontend/admission/parser程序通过；实际短测仍有失败，未部署或打包。

后续 NVFP4 分区候选另重编 `small_t_nvfp4.cu`，合计复用/替换 71 个 Host 与 10 个 CUDA 单元，仍非干净全量构建。该 CUDA 单元包含 producer/reducer 窗口修复及 reducer 循环展开限制；原源码重编也会触发的 129-split 非法访问已由最小探针和完整数值测试分别验证。修改 `.cuh` 后强制重编对应 `.cu`，不能依赖仅检查 `.cu` 时间戳的私有增量缓存。构建哈希及实际验证范围见[候选说明](COUPLED_SAMPLING.md#后续nvfp4-验证列分区)。

本机后续安装：2026-10-20 按用户要求直接安装已验证的最终 `b1f832…` 二进制，没有重新编译；旧日常 `d6c4f8…` 作为唯一回滚备份保留。日常路径下两次冷/热检查通过，恢复入口只读演练通过；未更新公开下载清单或发布资产。详见[安装状态与验证](COUPLED_SAMPLING.md)。

2026-10-21 [FP16 GDN 一致性候选](GDN_FP16_CONSISTENCY.md)在同一清单上强制重编
`linear_attention/gated_delta_net/recurrent.cu`，共71个Host/11个CUDA替换单元，仍复用其余原库。
候选未安装；资格程序链接相同对象。头文件变动必须使该CUDA单元重新编译。

### 2026-10-21 无探针独立计数试用构建

在FP16 GDN候选对象上，仅重编 `program/decode.cpp`、`program/program.cpp`、`runtime/engine/engine.cpp` 三个Host单元，复用其全部CUDA及其余对象，不含诊断探针，非干净全量构建。新增明确思考闭合API，普通sampling更新契约保留；相关六个源码哈希、构建范围及二进制 `8804cb04470214f68e01e38cb07004c90e748507a77134995335d1edbd768472` 见[脱敏安装证据](../benchmarks/evidence/long-context-tool-loop/answer-scope-trial-installation.json)。同一二进制完成29/29开启组、9/10缺省短组，准确对象重新链接的frontend55组/parser39组通过。已安装本机日常试用并保留上一日常回滚；用户选择自行验证真实dsh任务，未更新公开下载清单或发布资产。已清理可重新链接的测试exe和重复候选，构建对象及链接清单保留。


## v0.1.4发布及离线整合包

发布二进制为已安装无探针8804cb…，未重新改变算子；公开源码包含相同正式修复、部署开关和脱敏阶段记录。三Host重编与其GDN/CUDA基底见上文，不能当作干净全量构建。`scripts/package-engine.py`生成独立引擎附件，`scripts/package-offline.py`另从Git可见源码、明确的两模型、固定哈希运行依赖和全新npm ci结果构建网盘包，拒绝已有输出目录和依赖reparse link，不复制用户runtime。归档只允许清单内文件，安装验收后生成的会话/config不进入分享ZIP。依赖许可和Swift衍生模型条款随包保留；详细入口见[离线整合包](OFFLINE_BUNDLE.md)。
