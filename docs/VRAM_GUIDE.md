# 显存不足：按顺序调整

适用于首次部署、CUDA OOM、Graph 捕获失败或 KV 页池不足。先停止本安装的服务，再改模型/页池选项；不要批量关闭所有 Node/Python 进程。

1. **优先开启混合显卡模式（Hybrid / Optimus）**，让桌面、浏览器尽量使用核显，按设备要求重启。没有核显则跳过。
2. **对实际引擎关闭驱动溢出内存**：NVIDIA 控制面板 → 管理 3D 设置 → 程序设置 → 添加 `runtime/engine/ninfer-serve.exe` → `CUDA - Sysmem Fallback Policy` → `Prefer No Sysmem Fallback`。引擎重启后生效，保留全局设置。**保留 KVMem 的 Host KV**，它与驱动自动溢出不同。
3. **关闭无关后台应用**，退出游戏、其他模型、占用独显的视频处理等，再查看任务管理器与 `nvidia-smi`。驱动显示的空闲显存不等于 CUDA 能完整分配的额度。
4. **仍不足，优先换 RK4V4**：停止后运行 `07-kv-rk4v4.cmd`，再用 `02-start.cmd` 启动。回退用 `07-kv-nvfp4.cmd`。同一引擎支持两者，缓存重建，输出也可能变化。
5. **再不足，配套缩小页池与检索窗口**：停止后运行 `08-pool-small.cmd`，设置 DeviceKV=34560、RetrieveTokens=1536；保留 sink=12288、recent=19456、HostKV=7168MiB。用 `08-pool-default.cmd` 恢复 36864/3840。小档可能降低历史召回，尚未追加实模长任务验收；不要只缩页池而保留装不下的窗口。

每次只改一项，记录实际引擎哈希、模型、配置和错误，完成启动、短任务、缓存及工具续接检查后再试长任务。显存不足不应通过降低思考、删除历史或强制结束来掩盖。

发布默认纯文字，不启用视觉。社区反馈 RTX 30 系及以后、12GB 卡可用，16GB 可选更大 IQ3_S；**本次预编译引擎为 sm120a / RTX 50 系，30/40 系必须换匹配架构构建**。

[完整部署说明](OFFLINE_BUNDLE.md) · [排错与纠错提示词](TROUBLESHOOTING.md)
