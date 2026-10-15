# 可选：视觉编码完全放 CPU

默认发布预设为**纯文字 + MTP**，不加载视觉塔。CPU 视觉已经在该机器和较早引擎修复阶段测试过；它不是默认启动器的现成开关，需要转换含视觉组件的独立制品并使用下列服务选项。

下载 [ISTA-DASLab GGUF 仓库](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF) 的 `mmproj-Qwen3.8-27B-BF16.gguf`。在 [文本转换](MODELS.md) 命令中增加：

```text
--source vision=../models/mmproj-Qwen3.8-27B-BF16.gguf
--components text,vision,mtp
--out ../models/qwen3.8-27b-gsq-rco-iq3-xxs-vision-bf16.ninfer
```

视觉权重在制品内仍为 BF16，本机 CPU 路径加载为 FP32 运算；前处理和视觉编码在 CPU，文字与 MTP 仍在 GPU。直接运行 `ninfer-serve.exe` 时使用 **`--vision-cpu --vision-max-merged 256`**，其余参数沿用 `start.ps1`。若自行把该参数加入公开启动器，还需把 dsh 模型 input 改为 `text,image` 并重新验证。

## 本机旧阶段实测

| 每图 merged token cap | CPU 视觉编码时间 |
|---|---:|
| 256 | 约 9.6 秒 |
| 512 | 约 32 秒 |
| 1024 | 约 93 秒 |

测试图为几何色块与简单英文文字；分辨率、长宽比与 CPU 状态影响很大。256 cap 对细小文字和复杂图片会损失细节，不能把这组测试当作通用 OCR 质量保证。

形状/颜色、双图、OCR、重复图片缓存、无效输入恢复和约 45K 图片+工具续接通过。本机测得图片接口的文字侧额外预留约 **1.27 MiB GPU**；视觉塔不驻显存，但不是整个图片请求“绝对零显存增量”，也不是“没有延迟”。RAM 用量和首次图片等待会增加；已缓存图片的后续轮次可以避免重复编码。

证据：[`summary.json`](../benchmarks/evidence/vision-cpu/summary.json)、[`artifact-verification.json`](../benchmarks/evidence/vision-cpu/artifact-verification.json)。这些测量早于最新解码调度；不把它们当作此 Release 重新跑过的视觉性能结果。
