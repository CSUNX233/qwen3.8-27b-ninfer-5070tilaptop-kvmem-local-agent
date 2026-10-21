---
base_model: Qwen/Qwen3.8-27B
base_model_relation: quantized
pipeline_tag: image-text-to-text
library_name: gguf
license: apache-2.0
tags:
  - gguf
  - gsq
  - rco
  - quantization
  - mixed-precision
  - ist-daslab
  - multimodal
  - vision
---

<!--
  GSQ-RCO GGUF release card, TEMPLATE (filled with Qwen3.8-27B as the example).
  To publish a new model, copy this folder and change only:
    1. the YAML frontmatter above  (base_model, license)
    2. every field marked  [swap]  (model name, filenames, one-line summaries)
    3. the results: drop tools/results/<model>.json, run
         python tools/make_plots.py tools/results/<model>.json
       then paste the printed Markdown table into "Results".
  The header (banner + badges) is shared across all releases.
  NB: the YAML block must remain the very first bytes of the file (HF requirement).
-->

<div align="center">

<a href="https://github.com/IST-DASLab"><img src="https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/banner.png" alt="GGUF, GSQ-RCO dynamic non-uniform quantization" width="100%"/></a>

<br/>

# Qwen3.8-27B &middot; GSQ-RCO GGUFs

**Non-uniform GGUF quantizations** produced with **GSQ** and **RCO**, with a vision projector for multimodal use.

[![arXiv: GSQ](https://img.shields.io/badge/arXiv-GSQ_2604.18556-b31b1b?logo=arxiv&logoColor=white)](https://arxiv.org/abs/2604.18556)
[![arXiv: RCO](https://img.shields.io/badge/arXiv-RCO_2605.00649-b31b1b?logo=arxiv&logoColor=white)](https://arxiv.org/abs/2605.00649)
[![GSQ code](https://img.shields.io/badge/code-GSQ-181717?logo=github&logoColor=white)](https://github.com/IST-DASLab/GSQ)
[![RCO code](https://img.shields.io/badge/code-RCO-181717?logo=github&logoColor=white)](https://github.com/IST-DASLab/RCO)
[![DASLab](https://img.shields.io/badge/DASLab-GitHub-101048?logo=github&logoColor=white)](https://github.com/IST-DASLab)
[![license](https://img.shields.io/badge/license-apache--2.0-19a34a)](#license)

</div>

![Task average vs bit-width](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/plots/Qwen3.8-27B-task_avg_vs_avg_bit_width.png)

![Speculative decoding with the MTP head](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/plots/Qwen3.8-27B-mtp_speculative_decoding.png)

---

## Overview

This repository provides GGUF quantizations of **Qwen3.8-27B** at four sizes, together with the model's vision projector (`mmproj`) for multimodal use. In contrast to uniform quantization, which applies a single quantization type to all weight tensors, each model here assigns a separate quantization type to every tensor. The assignment is obtained by a gradient-based search that allocates precision according to per-tensor sensitivity, subject to a total size budget. The resulting files are standard GGUF and run unmodified in `llama.cpp`, Ollama, and LM Studio.

> **Method summary.** GSQ provides accurate low-bit scalar quantization of each tensor at a given quantization type; RCO assigns the per-tensor quantization types under a size budget. Together they yield a non-uniform GGUF at the requested size.

| Method | Description |
|---|---|
| **GSQ** (Gumbel-Softmax Quantization, [paper](https://arxiv.org/abs/2604.18556), [code](https://github.com/IST-DASLab/GSQ)) | Post-training scalar quantization that jointly learns the per-coordinate grid assignments and the per-group scales via a Gumbel-Softmax relaxation. GSQ closes most of the gap between scalar and vector quantization at 2 to 3 bits while remaining deployable in standard scalar formats such as GGUF. |
| **RCO** (Riemannian Constrained Optimization, [paper](https://arxiv.org/abs/2605.00649), [code](https://github.com/IST-DASLab/RCO)) | Assigns one of K quantization types to each of N tensors under a total size budget. The budget constraint is reformulated as a smooth Riemannian manifold in logit space, which permits gradient-based optimization directly on the task loss while enforcing the budget exactly, without constraint-specific hyperparameter tuning. |

Both methods were developed at the [Deep Algorithms and Systems Lab (DASLab)](https://github.com/IST-DASLab), Institute of Science and Technology Austria.

---

## Available files

Files follow the convention **`<model>-GSQ-RCO-<type>.gguf`**, where the suffix names the quantization class; the table lists each file's true whole-file average bit-width. The `mmproj` file carries the vision encoder and projector at BF16; one copy serves all quantizations.

<!-- [swap] rows below (filenames, bpw, size, notes). -->

| File | bpw | Size | Notes |
|---|---|---|---|
| `Qwen3.8-27B-GSQ-RCO-IQ2_XS.gguf` | 2.50 | 8.4 GB | Smallest; zero-shot above the BF16 baseline |
| `Qwen3.8-27B-GSQ-RCO-IQ2_S.gguf` | 2.75 | 9.3 GB | Matches the base model on AIME25 |
| `Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf` | 3.00 | 10.1 GB | Strong all-round operating point |
| `Qwen3.8-27B-GSQ-RCO-IQ3_S.gguf` | 3.50 | 11.8 GB | Recommended; task-lossless |
| `mmproj-Qwen3.8-27B-BF16.gguf` | 16 | 0.9 GB | Vision encoder + projector, for multimodal use |

Each quantization also ships an optional **`-mtp`** build (about 0.35 GB larger) that carries the model's Multi-Token Prediction head for speculative decoding in `llama.cpp`. The weights are otherwise identical, so quality is unchanged.

The IQ3_S model is the task-lossless operating point: it matches the base model exactly on AIME25 (100.00) and LiveCodeBench v6 (85.71) and is within 0.51 points on GPQA-Diamond, at just over one fifth of the BF16 size.

---

## Results

All models are evaluated against the **BF16** base model and the Unsloth Dynamic (UD) quantizations of the same base model. We report perplexity on wikitext2, C4, and FineWeb-Edu, the average over five zero-shot tasks (arc_easy, arc_challenge, hellaswag, winogrande, piqa), recovery (zero-shot average relative to BF16), and three reasoning and generation benchmarks: **AIME25**, **GPQA-Diamond**, and **LiveCodeBench v6**. Sizes are those of the files as evaluated.

<!-- [swap] paste the Markdown table printed by tools/make_plots.py -->

| Variant | bpw | GB | wiki↓ | c4↓ | fw↓ | ZS avg↑ | recovery | AIME25↑ | GPQA-D↑ | LCB v6↑ |
|---|---|---|---|---|---|---|---|---|---|---|
| BF16 | 16.00 | 53.8 | 7.05 | 11.45 | 8.14 | 74.34 | 100.0% | 100.00 | 89.90 | 85.71 |
| **GSQ-RCO IQ2_XS** | 2.50 | 8.4 | 7.69 | 12.98 | 9.19 | 74.54 | 100.3% | 96.67 | 84.85 | 76.57 |
| **GSQ-RCO IQ2_S** | 2.75 | 9.3 | 7.39 | 12.40 | 8.80 | **75.70** | **101.8%** | 100.00 | 86.36 | 82.29 |
| **GSQ-RCO IQ3_XXS** | 3.00 | 10.1 | 7.20 | 12.13 | 8.59 | 74.81 | 100.6% | 100.00 | 88.89 | 84.57 |
| **GSQ-RCO IQ3_S** | 3.50 | 11.8 | **7.07** | 11.76 | 8.34 | 74.47 | 100.2% | **100.00** | 89.39 | **85.71** |
| UD-IQ2_S | 2.49 | 8.4 | 8.02 | 12.78 | 9.08 | 73.80 | 99.3% | 86.67 | 76.26 | 72.00 |
| UD-Q2_K_XL | 2.88 | 9.8 | 7.54 | 12.25 | 8.69 | 74.37 | 100.0% | 100.00 | 86.87 | 82.28 |
| UD-IQ3_S | 3.52 | 12.0 | 7.16 | 11.75 | 8.34 | 75.49 | 101.5% | 96.67 | **89.90** | 84.00 |

At 3.50 bpw, IQ3_S is task-lossless: it reproduces the base model exactly on AIME25 (100.00) and LiveCodeBench v6 (85.71) and trails it by 0.51 points on GPQA-Diamond, giving a task average of 91.70 against the base model's 91.87 (99.8%) at 11.8 GB, a 4.6x size reduction. Against UD-IQ3_S it leads by 3.33 points on AIME25 and 1.71 on LiveCodeBench while being 0.2 GB smaller, though UD holds GPQA-Diamond by 0.51. At 3.00 bpw the model already matches the base on AIME25 at 10.1 GB, and at matched file size (8.4 GB) IQ2_XS leads UD-IQ2_S by 10.00 points on AIME25, 8.59 on GPQA-Diamond, and 4.57 on LiveCodeBench v6. <!-- [swap] one-line observation -->

![AIME25 vs bit-width](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/plots/Qwen3.8-27B-aime25_vs_avg_bit_width.png)

![GPQA-Diamond vs bit-width](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/plots/Qwen3.8-27B-gpqa_diamond_vs_avg_bit_width.png)

![LiveCodeBench v6 vs bit-width](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF/resolve/main/assets/plots/Qwen3.8-27B-lcb_vs_avg_bit_width.png)

---

## Usage

### llama.cpp
```bash
# download (requires: pip install -U "huggingface_hub[cli]")
hf download ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf --local-dir .

llama-cli -m Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf -p "Explain mixed-precision quantization." -ngl 99
```

### Vision (multimodal)
```bash
hf download ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF mmproj-Qwen3.8-27B-BF16.gguf --local-dir .

llama-mtmd-cli -m Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf \
  --mmproj mmproj-Qwen3.8-27B-BF16.gguf \
  --image photo.jpg -p "Describe this image."
```
The projector was converted directly from the base checkpoint and verified against these quantizations.

### Ollama
```bash
ollama run hf.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF   # pick the file matching your memory budget
```

### LM Studio
Search the repo name, then pick a `GSQ-RCO-*` build from the file list.

---

## Quantization procedure

1. **Per-tensor database.** Each weight tensor is quantized at every candidate GGUF quantization type with GSQ, yielding a searchable database of quantized tensor variants.
2. **RCO search.** The budget-constrained Riemannian search assigns one quantization type per tensor such that the whole-file average bit-width meets the target.
3. **Assembly.** The selected per-tensor variants are stitched into a single standard GGUF file.

Reference implementations: **GSQ** at [IST-DASLab/GSQ](https://github.com/IST-DASLab/GSQ) and **RCO** at [IST-DASLab/RCO](https://github.com/IST-DASLab/RCO).

### Reproducibility artifacts

Each released GGUF ships the files needed to audit how it was built:

| File | Contents |
|---|---|
| `tensor-allocation/<model>.rco-allocation.txt` | The quantization type assigned to every tensor in that file, with a quant-type histogram and the target bit-width. This is the RCO search result, so the allocation can be inspected without opening the model. |
| `imatrix-qwen3.8-27b.gguf` | The importance matrix used during quantization (1000 chunks of 4096 tokens). |

The `-mtp` builds have their own allocation dumps; they list the same per-tensor assignment as the base model plus the 15 tensors of the MTP head.

---

## Citation

If you use these models or methods, please cite both papers:

```bibtex
@article{gsq2026,
  title  = {GSQ: Highly-Accurate Low-Precision Scalar Quantization for LLMs via Gumbel-Softmax Sampling},
  author = {Dadgarnia, Alireza and Tabesh, Soroush and Nikdan, Mahdi and Helcig, Michael and Kurtic, Eldar and Kleinegger, Maximilian and Alistarh, Dan},
  journal= {arXiv preprint arXiv:2604.18556},
  year   = {2026}
}
@article{rco2026,
  title  = {Model Compression with Exact Budget Constraints via Riemannian Manifolds},
  author = {Helcig, Michael and Alistarh, Dan},
  journal= {arXiv preprint arXiv:2605.00649},
  year   = {2026}
}
```

---

## Acknowledgements

We thank [Verda](https://verda.com/) and Scientific Computing at the Institute of Science and Technology Austria for providing the compute resources used to produce these models.

---

## License

These quantized weights inherit the license of the base model (**Qwen3.8-27B**). The GSQ-RCO tooling is released by the Deep Algorithms and Systems Lab under its repository license.

<div align="center">
<sub>Built with <b>GSQ</b> and <b>RCO</b> at the <a href="https://github.com/IST-DASLab">Deep Algorithms and Systems Lab</a> &middot; Institute of Science and Technology Austria</sub>
</div>
