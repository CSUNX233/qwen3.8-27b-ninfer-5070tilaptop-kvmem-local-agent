---
license: other
license_name: swift-open-license-1.0
license_link: https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/LICENSE
library_name: gguf
pipeline_tag: text-generation
base_model: ukisai/Swift-1.5-Qwen3.8-27b
base_model_relation: quantized
tags:
- gguf
- llama.cpp
- qwen3_8
- gsq
- rco
- reasoning
- efficient-thinking
- token-efficient
- post-training
---

<div align="center">
<a href="https://ukisai.com"><img src="ukisai-banner.png" alt="UkisAI" style="width:100%;height:auto;" /></a>
<p><a href="https://ukisai.com"><b>Website</b></a> &bull; <a href="https://ukisai.com/products/swift"><b>Learn more</b></a> &bull; <a href="https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27b"><b>BF16 model</b></a> &bull; <a href="https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GGUF"><b>Standard GGUFs</b></a> &bull; <a href="#evaluation"><b>Evaluation</b></a> &bull; <a href="#license-and-access"><b>Enterprise licensing</b></a></p>
</div>

# Swift 1.5 Qwen3.8-27B · GSQ-RCO

Compact, mixed-precision GGUF quantizations of [Swift 1.5 Qwen3.8-27B](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27b), with Swift-specific refinement using the per-tensor allocations from [ISTA-DASLab's GSQ-RCO release](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF).

Swift 1.5 builds on Swift 1.0 through post-training focused on long-horizon, agentic and coding tasks, improving overall performance while using fewer thinking tokens. See the [original model card](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27b) for the model's training approach and benchmark results. Those model-level benchmarks are separate from the quantization measurements below.

Swift 1.5 uses **58.5% fewer thinking tokens** while scoring **0.35% higher** than the base, for a **9.18× speed-up** on several tasks.

## Available quantizations

Each tier is a single GGUF file. Sizes are decimal GB; runtime memory also includes the context cache and compute buffers. Tier names denote mixed-precision allocation profiles, rather than a uniform type for every tensor.

| Tier | Standard GGUF | With MTP head | Development KLD ↓ |
| --- | ---: | ---: | ---: |
| IQ2_XS | [8.42 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ2_XS.gguf) | [8.77 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ2_XS-mtp.gguf) | 0.189979 |
| IQ2_S | [9.26 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ2_S.gguf) | [9.61 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ2_S-mtp.gguf) | 0.134751 |
| IQ3_XXS | [10.09 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf) | [10.44 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_XXS-mtp.gguf) | 0.097774 |
| IQ3_S | [11.77 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_S.gguf) | [12.12 GB](Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_S-mtp.gguf) | 0.051265 |

The optional `-mtp` files retain the matching refined model tensors and add the MTP head. They require a runtime with support for this model's MTP implementation. The KLD results here were measured on the **standard files**; MTP decoding speed and quality have not been separately evaluated.

Exact file identities are recorded in [release-manifest.json](release-manifest.json) and [SHA256SUMS](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/SHA256SUMS).

## Evaluation

KLD measures the divergence of the quantized model's next-token distribution from **Swift 1.5 BF16**; lower is better. Development measurements use `wiki.test.raw`, 100 chunks and a **512-token context**. Development data informed refinement and is not independent validation.

The held-out results below use C4 prose, CodeParrot code, GSM8K math text and multilingual mC4 text. Prose/code/math use 100 chunks each; German/French/Spanish/Chinese use 25 chunks each, all at context 512. These are distributional KLD measurements, not task accuracy or math benchmark scores.

| Held-out text | IQ2_XS | IQ2_S | IQ3_XXS | IQ3_S |
| --- | ---: | ---: | ---: | ---: |
| C4 prose | 0.161594 | 0.107438 | 0.080238 | 0.041748 |
| CodeParrot code | 0.120884 | 0.084739 | 0.062546 | 0.035447 |
| GSM8K math text | 0.117467 | 0.096348 | 0.075964 | 0.043916 |
| German | 0.124482 | 0.091422 | 0.074645 | 0.035698 |
| French | 0.166785 | 0.113277 | 0.077791 | 0.046219 |
| Spanish | 0.082313 | 0.056393 | 0.039906 | 0.024186 |
| Chinese | 0.207063 | 0.129485 | 0.103141 | 0.052241 |

All four refined files improve KLD over their matched Swift starting quantizations on all seven reporting domains. Results are not uniformly better than the ISTA comparison quants: math-text KLD is 4.9–7.7% higher, and IQ3_S is higher on five of seven domains. ISTA comparisons measure each quant against its **own corresponding BF16 model**; they are not direct Swift-versus-Qwen task rankings or proof of equivalent capability.

The [full result table](evaluation/heldout-kld.tsv) includes the matched Swift starting quant, ISTA comparison and reported error estimates. [Evaluation metadata](evaluation/report.json) binds results to the exact released identities. A lexical overlap screen was applied against calibration/development text; it does not establish semantic deduplication or prove absence of overfitting. These 512-token tests do not establish quality at 32K or longer contexts.

## Usage

Use a llama.cpp build that supports Qwen3.8. Authenticate with an account granted access while this repository is private.

```bash
hf download ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf --local-dir .

llama-server \
  -m Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_XXS.gguf \
  --jinja -fa on -ngl 99 -c 262144 \
  --temp 1.0 --top-p 0.95 --top-k 20 --min-p 0.0 \
  --presence-penalty 0.0 --repeat-penalty 1.0 --port 8000
```

Set the context size to fit available memory. The example context setting is not a claim that these quants were evaluated at that length.

This release provides the language-model GGUFs. A Swift 27B vision projector has not been verified for this release, so no projector or validated vision example is included.

## Quantization procedure

1. Reuse the published ISTA GSQ-RCO per-tensor allocation for each matching Qwen3.8-27B tier.
2. Quantize Swift 1.5 weights with the Swift V1MIX importance matrix and the selected allocation.
3. Apply Swift-specific GSQ refinement. IQ2_XS and IQ3_XXS use the preserved fixed-objective variants; IQ2_S and IQ3_S use their preserved V1MIX variants.
4. Freeze exact file identities, run the reporting evaluations, and preserve the matching MTP packages.

This release reuses ISTA's allocation search results; it does not claim a new RCO search on Swift. The [archived recipe records](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/RECIPE.txt) document construction settings and historical candidates; the release manifest identifies the selected files. The Swift importance matrix is included as [imatrix-swift15-v1mix.gguf](imatrix-swift15-v1mix.gguf).

Per-tensor assignments for all eight files are included in [tensor-allocation](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/tree/main/tensor-allocation). Each dump records the model SHA256, tensor count and type histogram. MTP dumps contain the 851 model tensors plus 15 head tensors.

## Methods and acknowledgements

GSQ and RCO were developed by the Deep Algorithms and Systems Lab at the Institute of Science and Technology Austria. This Swift adaptation is by UkisAI.

- **GSQ:** [paper](https://arxiv.org/abs/2604.18556) · [code](https://github.com/IST-DASLab/GSQ)
- **RCO:** [paper](https://arxiv.org/abs/2605.00649) · [code](https://github.com/IST-DASLab/RCO)
- **GGUF runtime and conversion:** [llama.cpp](https://github.com/ggml-org/llama.cpp)

We acknowledge the Qwen team for the original model, ISTA-DASLab for the quantization methods and published allocations, and NVIDIA Innovation Lab, AWS and Google Cloud for supporting Swift development.

## License and access

These adapted weights are distributed under the [Swift Open License v1.0](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/LICENSE). The original Qwen components retain their [Apache 2.0 license](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/LICENSE-APACHE-2.0) and [NOTICE](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF/blob/main/NOTICE). See the Swift license for applicable terms and enterprise licensing information.
