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

## 打包已验证的引擎

`scripts/package-engine.py` 核对 `SOURCE_PROVENANCE.json` 与下载清单中的 exe 哈希，打包程序、LICENSE、NOTICE 和静态依赖许可，校验 ZIP 内程序哈希及 CRC，更新清单的归档哈希并输出 `dist/SHA256SUMS`。它不编译、不下载依赖，也不上传；先完成构建和资格检查，再更新相应版本及来源记录。

```powershell
python scripts/package-engine.py --engine "D:\Build\ninfer-serve.exe"
python scripts/check-release.py
```

归档不含权重、DLL、SDK、会话或登录信息；发布时使用与源码标签匹配的新 Release，保留旧版本资产。
