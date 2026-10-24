# NInfer + KVMem: a local 27B Agent on 12GB GPUs

**v0.1.5 ports C2.1 onto the explicit v0.1.4 baseline**, improving long-history cache restoration and Host KV demotion. It retains v0.1.4 parsing, sampling, NVFP4 reducer, FP16 GDN and answer-only penalty fixes. Text+MTP and xhigh remain the defaults; vision is off. [Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) · [中文](README.md)

Community users report stable operation on **RTX 30 series and later with at least 12GB VRAM**, using architecture-matched engines and suitable budgets. **16GB cards can use larger IQ3_S models**, usually with better quality. This is community feedback, not qualification of every GPU/workload. The included models are IQ3_XXS; IQ3_S must be obtained separately.

**This release's binary is Windows x64 / sm120a for RTX 50 series. RTX 30/40 series require compatible architecture builds.** The maintainer directly tested an RTX 5070 Ti Laptop 12GB with 32GB system RAM. A CUDA 13 compatible driver is required; renaming the executable does not change GPU support. See [build guide](docs/BUILD.md).

## Changes and evidence

- Rolling long anchors at message boundaries reduce repeated prefill on compatible continuations. Same-lineage restoration may consume the old owner while retaining immutable KV/state; named-session and readonly isolation remain enforced.
- Eligible cold pages are demoted in batches of up to 32. Host extent allocation failures reduce batch size to a single page, allowing partial progress while retaining pin/order/tail constraints.
- No new attention formula, quantization format or retrieval window. Anchors consume bounded Host memory and may be evicted under pressure.

12 targeted regressions passed. Two greedy, thinking-off long-context fixtures each ran seven requests at about 68K input: needle fields 7/7 before/after compaction; continuation fields and Node code checks 6/6 before/after. Final requests reused 58,330/58,459 tokens in 1.93/2.77 seconds; entire fixtures took about 153/156 seconds. All 14 output token sequences matched the prior C2.1 fixture. Seven synthetic API smoke requests also passed, including short xhigh, tool continuation and CPU vision.

GPU usage sampled every five seconds peaked at 11,176/11,174 MiB (whole GPU). There is no matched original-v0.1.4 VRAM comparison. These results do not establish general speed/quality improvements, full-256K behavior, all-GPU compatibility or long-running real Agent reliability. [Detailed scope](docs/C2P1_V014_PORT.md) · [Evidence](benchmarks/evidence/c2p1-v014-port/verification.json)

## Downloads and installation

[GitHub Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) provides the engine archive, checksums and source. The engine asset contains no models, Node or runtime DLLs; use the source installer.

Quark links will be supplied by the maintainer in the video description/pinned comment. Both offline packages include the engine, pinned user-space dependencies, portable Node, dsh, VC++ installer, licenses and installation/troubleshooting documents. No GitHub download is needed to deploy them.

| Offline archive | Contents / entry |
|---|---|
| `NInfer-v0.1.5-Windows-sm120a-no-models.zip` | No weights; `01-setup-your-model.cmd` selects your text+MTP v3 `.ninfer` |
| `NInfer-v0.1.5-Windows-sm120a-full.zip` | Huihui Swift-1.5 and original GSQ-RCO IQ3_XXS text models, plus a complete Huihui text+vision+MTP artifact; use `01-setup-huihui.cmd` or `01-setup-gsq.cmd` |

Both default to text-only. After stopping the full bundle, `09-enable-cpu-vision.cmd` opts into CPU preprocessing/vision encoding; text/MTP still run on GPU. This adds RAM use and first-image latency. Start with `02-start.cmd`, stop with `03-stop.cmd`. A compatible NVIDIA driver and Microsoft Edge remain system prerequisites. [Offline guide](docs/OFFLINE_BUNDLE.md)

For the online source installer, install Node.js 24+, VC++ x64 runtime and prepare a text+MTP model:

```powershell
git clone https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent.git
cd qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\model.ninfer"
.\start.cmd
```

Defaults: NVFP4, xhigh, MTP3, logical context 262144, DeviceKV 36864, retrieval 3840, HostKV 7168 MiB, sink 12288 and recent 19456. Prefer hybrid graphics; disable CUDA Sysmem Fallback for the actual engine; close background GPU apps; try RK4V4 if still short of VRAM; only then reduce the pool and retrieval together. Keep KVMem Host KV enabled. Smaller retrieval may reduce recall; changing KV formats can change output.

The engine API uses `127.0.0.1:1234/v1`, cache guard `:1236/v1`, dsh UI `:3080`. Login URLs are private. [Install/upgrade](docs/INSTALL.md) · [Maintenance](docs/DEPLOYMENT_AGENT_GUIDE.md) · [Release notes](docs/RELEASE_NOTES.md)

## Attribution and limits

Special thanks to Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7), whose shared distribution supplied the foundation and earlier loop fix. See [upstream attribution](docs/UPSTREAM_DOWNSTREAM.md) and [NOTICE](NOTICE). [Pelican demo](docs/demo/README.md) and historical benchmarks are examples, not this release's controlled quality measurements.

Code is Apache-2.0; third-party and model licenses apply separately. Huihui Swift contributions use **Swift Open License v1.0**, including a commercial revenue threshold. [Model licenses](docs/MODELS.md)

The binary is incremental: 71 Host units rebuilt on v0.1.4, 11 verified CUDA objects and remaining original archives reused. A clean full CUDA build and full CTest were not completed. Stored history does not mean every token is attended on each step. Full 256K and every GPU are not qualified. Single concurrency and loopback binding remain the defaults.
