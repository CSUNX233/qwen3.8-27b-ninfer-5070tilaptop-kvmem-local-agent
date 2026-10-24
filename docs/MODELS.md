# 模型来源和转换

GitHub发布引擎、转换工具及部署代码，**GitHub不捆绑权重**；维护者提供无模型包及完整版；完整版包含下面两份已转换文字模型与额外完整CPU视觉制品。本机验证的是以下组合，不代表所有 Qwen 变体都已验证：

| 组件 | 来源 | 发布检查时的许可 |
|---|---|---|
| 基础架构、config、tokenizer | [Qwen/Qwen3.8-27B](https://huggingface.co/Qwen/Qwen3.8-27B) | Apache-2.0 |
| GSQ-RCO GGUF + MTP | [ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF) | Apache-2.0 |
| 精确 GGUF 文件 | `Qwen3.8-27B-GSQ-RCO-IQ3_XXS-mtp.gguf` | 随模型仓库；不要误选不含 MTP 的文件 |
| 可选视觉塔 | 同 GGUF 仓库的 `mmproj-Qwen3.8-27B-BF16.gguf` | 随模型仓库 |
| NInfer 转换器 | 本仓库 `engine/tools/convert`，上游作者及贡献者 | Apache-2.0 与保留的第三方条款 |

发布检查的仓库 revision：Qwen `1d4bf0f2ff6012fd82039f2fa52739d0dd7c60c0`；ISTA-DASLab `d562806dbafae37109975e970aae91b43e73b440`。模型下载、镜像和再次分发应保留相应模型卡与许可证；使用其他微调模型时重新核对其许可。

## 下载和转换文本 + MTP

可以在 Hugging Face 网页下载 GGUF，基础目录只需 config、tokenizer 等元数据，不需要再下载完整 BF16 语言权重。以下命令使用 Python 3.11 环境和 CPU torch，所有路径可放在 D 盘：

```powershell
py -3.11 -m venv .venv-convert
.\.venv-convert\Scripts\python.exe -m pip install torch --index-url https://download.pytorch.org/whl/cpu
.\.venv-convert\Scripts\python.exe -m pip install numpy safetensors huggingface_hub gguf
.\.venv-convert\Scripts\hf.exe download Qwen/Qwen3.8-27B --revision 1d4bf0f2ff6012fd82039f2fa52739d0dd7c60c0 --include "*.json" "*.jinja" "*.txt" "*.model" --local-dir models/base-qwen3.8-27b
.\.venv-convert\Scripts\hf.exe download ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF Qwen3.8-27B-GSQ-RCO-IQ3_XXS-mtp.gguf --revision d562806dbafae37109975e970aae91b43e73b440 --local-dir models
```

在项目根目录执行下面的转换；`--proposal` 使用随源码保留的频率排名表，不改变源 GGUF 的权重精度：

```powershell
$projectPython = (Resolve-Path .\.venv-convert\Scripts\python.exe).Path
Push-Location engine
& $projectPython -m tools.convert `
  --model ../models/base-qwen3.8-27b `
  --recipe qwen3_8_27b_gguf `
  --source gguf=../models/Qwen3.8-27B-GSQ-RCO-IQ3_XXS-mtp.gguf `
  --components text,mtp `
  --resource chat_template.jinja=../templates/chat_template_qwen3.8-enhanced.jinja `
  --proposal --name qwen3.8-27b-gsq-rco-iq3-xxs --device cpu `
  --out ../models/qwen3.8-27b-gsq-rco-iq3-xxs.ninfer
Pop-Location
```

该 GGUF recipe 搬运既有量化张量，不对主权重重新量化。本机文本制品为 **10,796,660,224 bytes**；输出大小仍受转换器版本、资源和 recipe 影响。成功后把 `.ninfer` 路径交给 `setup.ps1`。如需自检，转换器用法见 [原转换指南](../engine/docs/weight-conversion.md)。

视觉转换需要额外 source 和 components，见 [VISION.md](VISION.md)。16GB卡可选择更大的IQ3_S含MTP模型，通常质量更好，但需另行下载/转换并验证显存预算；本次附带文字模型仍是IQ3_XXS。


## 网盘整合包的已转换模型（v0.1.5）

| 选择 | 文件 | 来源和适用许可 |
|---|---|---|
| huihui | Huihui-Qwen3.8-27B-abliterated-Swift-1.5-GSQ-RCO-IQ3_XXS-mtp.ninfer | [Huihui GGUF](https://huggingface.co/huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF)，其模型卡明确Swift系列来自[UkisAI Swift-1.5 GSQ-RCO](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF)；Swift贡献适用Swift Open License v1.0，基础Qwen部分Apache-2.0 |
| gsq | qwen3.8-27b-gsq-rco-iq3-xxs.ninfer | 上述ISTA-DASLab GSQ-RCO IQ3_XXS+MTP，Apache-2.0 |

两份均为ninfer-v3文本+MTP，分别10,796,660,224字节；不包含视觉塔，也不同时加载。包内SHA-256清单记录实际制品身份。转换器使用qwen3_8_27b_gguf recipe，在CPU搬运现有量化张量，未重新训练或量化；Huihui制品使用基础Qwen元数据和随仓库保留的模板/频率proposal资源。来源GGUF完整revision未在原转换记录中固定，不能伪称现已补验历史来源commit；保留文件名、实际模型hash和模型卡供核对。

Huihui汇总模型卡的Apache标签不能覆盖Swift贡献的单独条款。Swift Open License v1.0第5条规定商业使用收入门槛（同一控制集团最近财年总收入US$1,000,000门槛，超过时需单独商业许可）；以原文为准。本包保留[Swift许可](../licenses/models/ukisai--Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/LICENSE)、[Huihui模型卡](../licenses/models/huihui-ai--Huihui-Qwen3.8-27B-abliterated-GGUF/README.md)、[Swift模型卡](../licenses/models/ukisai--Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/README.md)、[GSQ模型卡](../licenses/models/ISTA-DASLab--Qwen3.8-27B-GSQ-RCO-GGUF/README.md)和[基础Apache许可](../licenses/models/Qwen--Qwen3.8-27B/LICENSE)。模型卡为2026-10-21来源页面快照，不等同于历史GGUF字节的远端hash验证。

转换和整合：CSUNX233。Copyright2026 UkisAI，Swift贡献遵守Swift Open License v1.0；衍生自Qwen3.8-27B，Copyright2026 Alibaba Cloud，Apache-2.0。Huihui、ISTA-DASLab及其他贡献者保留各自归属。

## 完整包的可选CPU视觉制品

`Huihui-Qwen3.8-27B-abliterated-Swift-1.5-GSQ-RCO-IQ3_XXS-mtp-vision-bf16.ninfer`，11,718,230,528字节，包含完整text,vision,mtp，视觉塔来自本机Qwen BF16 mmproj（原GGUF 931,145,888字节）。完整包清单记录实际制品SHA-256，不宣称已补验历史GGUF远程revision。文字部分沿用上述Huihui/Swift许可，Qwen视觉部分按基础模型许可；一次只加载一个制品，默认仍选择文字模型且不加载视觉。见[视觉配置](VISION.md)。
