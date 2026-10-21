---
library_name: transformers
license: apache-2.0
pipeline_tag: image-text-to-text
base_model:
- Qwen/Qwen3.8-27B
tags:
- abliterated
- uncensored
- huihui
- qwen3
- unsloth
- ternary
- 2-bit
- llama-cpp
- cuda
- metal
- on-device
- hybrid-attention
- prismml
- bonsai
---

# huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF


This is an uncensored version of [Qwen/Qwen3.8-27B](https://huggingface.co/Qwen/Qwen3.8-27B) created with abliteration (see [remove-refusals-with-transformers](https://github.com/Sumandora/remove-refusals-with-transformers) to know more about it).
This is a crude, proof-of-concept implementation to remove refusals from an LLM model without using TransformerLens.

## Latest update 8

The newly added **Huihui-Qwen3.8-27B-abliterated-Underdog-Saluki** series come from [ConwayResearch/Underdog-Saluki-27B-1.0](https://huggingface.co/ConwayResearch/Underdog-Saluki-27B-1.0) and [ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF). 
Only layers **22 to 52 (0-based indexing)** have been ablated, while the other layers remain unablated. It may come with a small disclaimer warning.

Due to ConwayResearch/Underdog-Saluki-27B-1.0 not providing an imatrix file, we will attempt to perform ablation by mixing the GGUF weights of Swift-1.5 and ConwayResearch/Underdog-Saluki-27B-1.0.

ConwayResearch/Underdog-Saluki-27B-1.0/Underdog-Saluki-27B-1.0-IQ2-mix.gguf only ablated 62 weights.

This is just a test/validation.

## Latest update 7

The newly added **Huihui-Qwen3.8-27B-abliterated-Swift** series come from [ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF](https://huggingface.co/ukisai/Swift-1.5-Qwen3.8-27B-GSQ-RCO-GGUF). 
Only layers **22 to 52 (0-based indexing)** have been ablated, while the other layers remain unablated. It may come with a small disclaimer warning.
This is just a test/validation.

## Latest update 6

The newly added **Huihui-Qwen3.8-27B-abliterated-Ternary** series come from [prism-ml/Ternary-Bonsai-2-27B-gguf](https://huggingface.co/prism-ml/Ternary-Bonsai-2-27B-gguf
). 
Only layers **22 to 52 (0-based indexing)** have been ablated, while the other layers remain unablated. It may come with a small disclaimer warning.
The size after conversion may differ from the original GGUF (Some of the weights are converted from PTQ1 to Q2_K or Q3_K.).
This is just a test/validation.

The ternary hybrid-attention kernels live in the
[PrismML-Eng/llama.cpp](https://github.com/PrismML-Eng/llama.cpp) fork. **Stock llama.cpp will not run these files.**

## Latest update 5

The newly added **Huihui-Qwen3.8-27B-abliterated-GSQ-RCO** series come from [ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF](https://huggingface.co/ISTA-DASLab/Qwen3.8-27B-GSQ-RCO-GGUF
). 
Only layers **22 to 52 (0-based indexing)** have been ablated, while the other layers remain unablated. It may come with a small disclaimer warning.
The size after conversion may differ from the original GGUF.

## Latest update 4

The newly added **Huihui-Qwen3.8-27B-abliterated-UD-DW** series come from [unsloth/Qwen3.8-27B-GGUF](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF). 
Only layers **22 to 52 (0-based indexing)** have been ablated, while the other layers remain unablated. It may come with a small disclaimer warning.
The size after conversion may differ from the original GGUF.

## Latest update

The newly added **Huihui-Qwen3.8-27B-abliterated-UD** series come from [unsloth/Qwen3.8-27B-GGUF](https://huggingface.co/unsloth/Qwen3.8-27B-GGUF). 
Only layers **17 to 52 (0-based indexing)** have been ablated(Previously, The first 15 layers were retained without ablation), while the other layers remain unablated. 
The size after conversion may differ from the original GGUF.

Huihui-Qwen3.8-27B-abliterated-bf16.gguf has also been updated.

This helps retain more of the original model’s performance. MTP and visual has not been modified.

## Note
The first 15 layers were retained without ablation. MTP and visual has not been modified.

We have already converted the weights (token_embd,output,ffn_down,ssm_out,attn_output) that need to be ablated in the versions below Q8_0 from Q2_K, Q3_K, Q4_K, Q5_K, and Q6_K to Q8_0 to improve response quality, and changed the filename to K_L.

In the Q8_0 quantized version, we changed the Q8_0 weights (token_embd,output,ffn_down,ssm_out,attn_output) targeted for ablation to BF16 and renamed the file to Q8_0_L.

This is not a standard quantization, so you might find that Q2_K_L is larger than Q3_K and Q4_K.

## Specific Quantification Method 
Some people may misunderstand. The specific quantification method is as follows.

### Q2_K_L - Q6_K_L 

[Qwen3.8-27B-tensor_types-Q6_K_L.txt](Qwen3.8-27B-tensor_types-Q6_K_L.txt)
```
llama-quantize \
  --allow-requantize \
  --tensor-type-file huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Qwen3.8-27B-tensor_types-Q6_K_L.txt \
  huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Huihui-Qwen3.8-27B-abliterated-bf16.gguf \
  huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Huihui-Qwen3.8-27B-abliterated-Q6_K_L.gguf Q6_K
```

### Q8_0_L  

[Qwen3.8-27B-tensor_types-Q8_0_L.txt](Qwen3.8-27B-tensor_types-Q8_0_L.txt)

```
llama-quantize \ 
  --allow-requantize \
  --tensor-type-file huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Qwen3.8-27B-tensor_types-Q8_0_L.txt \
  huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Huihui-Qwen3.8-27B-abliterated-bf16.gguf \ 
  huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Huihui-Qwen3.8-27B-abliterated-Q8_0_L.gguf Q8_0

```


## ollama

Please use the latest version of [ollama](https://github.com/ollama/ollama/releases)

You can use [huihui_ai/Qwen3.8-abliterated](https://ollama.com/huihui_ai/Qwen3.8-abliterated) directly, 
```
ollama run huihui_ai/Qwen3.8-abliterated
```


## llama.cpp
Use the latest [llama.cpp](https://github.com/ggml-org/llama.cpp),
```
llama-cli -m huihui-ai/Huihui-Qwen3.8-27B-abliterated-GGUF/Huihui-Qwen3.8-27B-abliterated-Q4_K.gguf -c 262144
```

### Usage Warnings


 - **Risk of Sensitive or Controversial Outputs**: This model’s safety filtering has been significantly reduced, potentially generating sensitive, controversial, or inappropriate content. Users should exercise caution and rigorously review generated outputs.

 - **Not Suitable for All Audiences**: Due to limited content filtering, the model’s outputs may be inappropriate for public settings, underage users, or applications requiring high security.

 - **Legal and Ethical Responsibilities**: Users must ensure their usage complies with local laws and ethical standards. Generated content may carry legal or ethical risks, and users are solely responsible for any consequences.

 - **Research and Experimental Use**: It is recommended to use this model for research, testing, or controlled environments, avoiding direct use in production or public-facing commercial applications.

 - **Monitoring and Review Recommendations**: Users are strongly advised to monitor model outputs in real-time and conduct manual reviews when necessary to prevent the dissemination of inappropriate content.

 - **No Default Safety Guarantees**: Unlike standard models, this model has not undergone rigorous safety optimization. huihui.ai bears no responsibility for any consequences arising from its use.


### Donation
##### Your donation helps us continue our further development and improvement, a cup of coffee can do it.
- bitcoin:
```
  bc1qqnkhuchxw0zqjh2ku3lu4hq45hc6gy84uk70ge
```
- Support our work on [Ko-fi](https://ko-fi.com/huihuiai)!

