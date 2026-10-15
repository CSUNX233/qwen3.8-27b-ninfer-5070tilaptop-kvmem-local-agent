# NInfer for RTX 5070 Ti Laptop 12GB — Qwen3.8-27B, KVMem & Local AI Agent

[中文说明与完整快速开始](README.md)

A Windows source distribution and deployment kit maintained by **CSUNX233**, built on NInfer-all, NInfer and the shensanshu KVMem integration. Upstream engine and model authors retain their credit and licenses. See [provenance](docs/UPSTREAM_DOWNSTREAM.md).

**Special thanks to Bilibili creator 0xFFFF7:** the foundation used here was downloaded from his shared distribution, and he also resolved the reported infinite-loop issue. This downstream project builds on that work.

**Recommended reasoning: xhigh** (configured by default). The maintainer reports stable everyday use at **200K context**, no noticeable slowdown, and faster repeated tasks; no controlled per-stage timing logs were supplied for that observation. The [pelican bicycle HTML demo](docs/demo/tihuriding.html) is a maintainer-supplied actual model output with a reported generation time of about **7 minutes**. Adaptive/dynamic MTP is a possible future decode improvement, **not yet benchmarked or enabled** in this preset.

- Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP on a 12GB laptop GPU.
- 262,144-token **configured logical capacity** with a 36,864-token GPU KV pool and 7 GiB Host KV. KVMem selectively attends to historical pages; this is not full dense attention over every stored token.
- Verified 110K-token input and a 62K input → 22K generated tokens → tool call → multi-turn continuation workflow. **Full 256K whole-model stress has not been run.**
- Two fixed short-task A/B runs measured roughly 65–85 token/s and a +2.44% aggregate decode gain for the card-specific launch scheduling. Results are workload dependent; prefill/TTFT did not universally improve.
- A synthetic 84,925-token tool continuation reused 84,873 tokens: 52 newly evaluated tokens, 813.5 ms prefill and 1,234 ms TTFT. Repetitive-text draft throughput is not advertised as normal Agent throughput.
- Optional DeepSeek Harness web UI, local search/fetch, lazy Edge browser tools and CPU-only vision encoding. Web search needs Internet access, but no paid search API key.

Validated hardware: RTX 5070 Ti Laptop 12GB, 46 SMs, i7-14650HX, 32GB RAM, Windows x64, driver 617.14. Prebuilt target: sm120a, CUDA 13.0.48, MSVC 14.44.35207. Other hardware requires independent qualification.

Hybrid integrated/discrete graphics is recommended: let the iGPU render the desktop and browser, preserving discrete VRAM for inference. The launcher does not change the system graphics mode. Systems without hybrid graphics can also try it after closing unnecessary GPU workloads and reserving sufficient VRAM. Other RTX 50-series GPUs with 12GB or more VRAM are welcome to try this preset; they have not all been tested. Non-50-series GPUs require a source build for their CUDA architecture. Contributions with build instructions, configurations and measured results are welcome.

Download this repository, prepare a v3 `.ninfer` artifact with MTP following [MODELS.md](docs/MODELS.md), and install Node.js 24+, the x64 VC++ runtime and a CUDA 13 compatible NVIDIA driver:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\qwen3.8-27b-gsq-rco-iq3-xxs.ninfer"
.\start.cmd
# Stop this checkout:
.\stop.cmd
```

Setup downloads a checksum-pinned engine Release and third-party runtime archives directly from their publishers. Models, private sessions, credentials and proprietary SDKs are not redistributed. Generated state stays inside `runtime/`; no global GPU or proxy settings are changed.

Code: Apache-2.0, with component-specific third-party licenses retained. Model and external runtime terms apply separately. [Installation](docs/INSTALL.md) · [Benchmarks](docs/BENCHMARKS.md) · [Source build](docs/BUILD.md) · [Limitations](README.md#使用条件与边界).
