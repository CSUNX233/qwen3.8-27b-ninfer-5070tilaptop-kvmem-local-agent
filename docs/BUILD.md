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

测试入口在 `engine/tests/README.md`。按变更选择测试；例如 Host 索引、页存储、异步检索及小矩阵数值资格检查。不要在用户正在使用 GPU 时并发运行性能测试。`benchmarks/qualification/qualify_vec.cpp` 是此次实际权重解包、FP64 oracle 的独立资格检查程序，需与构建出的引擎库、CUDA 头文件及模型制品配套使用，不是 CPU-only 单元测试。

RTX 50 系、12GB 及以上显存的其他设备欢迎尝试并提交参数与实测；目前明确验证的是 RTX 5070 Ti Laptop 12GB。**非 50 系显卡须自行按实际 CUDA 架构重新编译**，不要原样照搬上面针对本机的 `120a` 参数。按上游对该架构的支持情况配置内核路径，再调整 Device KV / Host KV 等预算，并验证模型正确性和稳定性。欢迎通过 Issue / PR 补齐不同设备的编译步骤、适配修复和测速。
