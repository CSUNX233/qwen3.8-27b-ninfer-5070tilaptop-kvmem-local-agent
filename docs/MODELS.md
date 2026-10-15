# 模型来源和转换

本项目发布引擎、转换工具及部署代码，**不捆绑权重**。本机验证的是以下组合，不代表所有 Qwen 变体都已验证：

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

视觉转换需要额外 source 和 components，见 [VISION.md](VISION.md)。大于 IQ3_XXS 的模型可能超出本预设的 12GB 预算。
