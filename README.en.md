# NInfer for RTX 5070 Ti Laptop 12GB — Qwen3.8-27B, KVMem & Local AI Agent

[中文说明与完整快速开始](README.md)

[Deployment and maintenance handoff guide (Chinese)](docs/DEPLOYMENT_AGENT_GUIDE.md): copyable instructions for deployment agents, version pins, VRAM preparation, launch settings, layered checks, known failures and fixes.

A Windows source distribution and deployment kit maintained by **CSUNX233**, built on NInfer-all, NInfer and the shensanshu KVMem integration. Upstream engine and model authors retain their credit and licenses. See [provenance](docs/UPSTREAM_DOWNSTREAM.md).

**Special thanks to Bilibili creator 0xFFFF7:** the foundation used here was downloaded from his shared distribution, and he also resolved the reported infinite-loop issue. This downstream project builds on that work.

**Recommended reasoning: xhigh** (configured by default). The maintainer reports stable everyday use at **200K context** and faster repeated tasks. A subsequent synthetic test at retrieval6144 / pool39168 measured200K cold prefill424.62s and an identical cached repeat112ms; cold prefill still slows with length. The [pelican bicycle HTML demo](docs/demo/tihuriding.html) is a maintainer-supplied actual model output with a reported generation time of about **7 minutes**. Adaptive/dynamic MTP is a possible future decode improvement, **not yet benchmarked or enabled** in this preset.

**[v0.1.4](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.4)** publishes the current engine with tool-parser recovery, committed MTP post-thinking boundaries, coupled sampling, NVFP4 reduction partitioning, FP16 GDN rounding and optional answer-only penalty counts. The launcher defaults to answer scope; set `AnswerPenaltyScope` to `carry-over` to disable it. On the final binary, one short ten-seed fixture improved9/10→10/10, functional cases12/12 and the98K continuation group7/7. These are fixed fixtures, not general quality or speed claims. See [release notes](docs/RELEASE_NOTES.md), [offline bundle](docs/OFFLINE_BUNDLE.md) and [troubleshooting](docs/TROUBLESHOOTING.md). GitHub assets contain no weights; the separately shared model bundle retains Swift-specific licensing.

**Historical [v0.1.3](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.3)** packages the maintainer's current engine: NVFP4 KV, a prompt-attention schedule for cc12.0 / 46-SM Laptop GPUs, and Host-pressure reclamation of duplicate replicas and idle Host-only cache owners. The public page preset remains36864/3840. **This NVFP4 configuration uses some additional VRAM versus the previous RK4V4 preset. If VRAM is very tight, set `"KVDType": "rk4v4"` in `config/local.json` and restart; the same engine supports both.** Existing configs without this field now default to NVFP4. Cold128K/200K prompt time fell18.86%/26.66% in one NVFP4 schedule comparison; this is not a decode-speed claim. The fixed cumulative sequence passed512 lines, six tools and18/18 recall after repair. Idle-cache eviction can require recomputation on revisit. New measurements use the maintainer's Huihui Swift-1.5 IQ3_XXS+MTP variant; they do not establish a quality advantage over RK4V4 or the old GSQ-RCO measurements. [Release notes](docs/RELEASE_NOTES.md) · [Evidence](docs/NVFP4_SM120.md) · [KV switch](docs/INSTALL.md#nvfp4-显存与-rk4v4-切换).

**Historical [v0.1.2](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.2)** packaged the then-qualified RK4V4 engine: index only committed target-model decode history, refresh cached questions after new history, retain the original ranking at the exact question-end boundary, and fuse norm/RoPE with pre-RoPE key capture outside scoring chunks. **Start deployment with the complete smaller preset: DeviceKV36864 / retrieval3840.** Clear unnecessary discrete-GPU workloads, validate short tasks, cache and tool continuation, then increase gradually. The maintainer's39168/6144 is an advanced preset with only about84MiB CUDA startup headroom. It scored128K5/5 cold and warm,200K4/5 both;64K Agent tool continuations took396–679ms and weighted decode84.11tok/s. No universal speed or full-recall claim. [Evidence and limits](docs/BENCHMARKS.md#语义历史修复与检索预算2026-10-16) · [Deployment guide and Agent prompt](docs/DEPLOYMENT_AGENT_GUIDE.md).

**v0.1.1** publishes the qualified retrieval-context repair and matching engine binary: per-Program ownership, RAM COW checkpoints for index/ranking, absolute-position append and real per-layer counts, plus screenshot progress continuation and downstream cancellation. A synthetic 62K input → 43,775 generated tokens → tools → three resumes reused the history, with 52 new tokens / 915 ms TTFT for the tool result and 382–411 ms for subsequent resumes. No new GPU buffer or production sampling/reasoning/budget change. Snapshots add RAM usage; one 49K cold-input run was about 3% slower, so no overall speed gain is claimed. The original private 177K task has not been replayed. [Release and downloads](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.1) · [Evidence and limits](docs/BENCHMARKS.md#原生检索上下文修复2026-10-15).

- Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP on a 12GB laptop GPU.
- 262,144-token **configured logical capacity** with a 36,864-token GPU KV pool and 7 GiB Host KV. KVMem selectively attends to historical pages; this is not full dense attention over every stored token.
- Verified 110K-token input and a 62K input → about 44K generated tokens → tool call → multi-turn continuation workflow. **Full 256K whole-model stress has not been run.**
- Two fixed short-task A/B runs measured roughly 65–85 token/s and a +2.44% aggregate decode gain for the card-specific launch scheduling. Results are workload dependent; prefill/TTFT did not universally improve.
- A synthetic 84,925-token tool continuation reused 84,873 tokens: 52 newly evaluated tokens, 813.5 ms prefill and 1,234 ms TTFT. Repetitive-text draft throughput is not advertised as normal Agent throughput.
- Optional DeepSeek Harness web UI, local search/fetch, lazy Edge browser tools and CPU-only vision encoding. Web search needs Internet access, but no paid search API key.
- Static tool-grounded execution guidance for the local Agent: inspect, implement and test once the next useful action is clear, retaining xhigh and necessary deep reasoning. No reasoning cap, sampling change or extra model request; [configuration and limits](integrations/README.md).

Validated hardware: RTX 5070 Ti Laptop 12GB, 46 SMs, i7-14650HX, 32GB RAM, Windows x64, driver 617.14. Prebuilt target: sm120a, CUDA 13.0.48, MSVC 14.44.35207. Other hardware requires independent qualification.

Hybrid integrated/discrete graphics is recommended: let the iGPU render the desktop and browser, preserving discrete VRAM for inference. The launcher does not change the system graphics mode. Systems without hybrid graphics can also try it after closing unnecessary GPU workloads and reserving sufficient VRAM. Other RTX 50-series GPUs with 12GB or more VRAM are welcome to try this preset; they have not all been tested. Non-50-series GPUs require a source build for their CUDA architecture. Contributions with build instructions, configurations and measured results are welcome.

> **Strongly recommended: disable CUDA system-memory fallback for NInfer.** In NVIDIA Control Panel → Manage 3D settings → Program Settings, add this checkout's `runtime/engine/ninfer-serve.exe` and set **CUDA - Sysmem Fallback Policy → Prefer No Sysmem Fallback**. Keep Global Settings unchanged. Driver fallback to RAM can severely slow inference. This does not disable the engine-managed **7 GiB Host KV**, which should remain enabled, or create more VRAM; insufficient VRAM can still cause allocation failures. [Setup details](docs/INSTALL.md#cuda-系统内存回退强烈建议禁用).

Download this repository, prepare a v3 `.ninfer` artifact with MTP following [MODELS.md](docs/MODELS.md), and install Node.js 24+, the x64 VC++ runtime and a CUDA 13 compatible NVIDIA driver:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1 -ModelPath "D:\Models\qwen3.8-27b-gsq-rco-iq3-xxs.ninfer"
.\start.cmd
# Stop this checkout:
.\stop.cmd
```

Setup downloads a checksum-pinned engine Release and third-party runtime archives directly from their publishers. Models, private sessions, credentials and proprietary SDKs are not redistributed. Generated state stays inside `runtime/`; no global GPU or proxy settings are changed.

Code: Apache-2.0, with component-specific third-party licenses retained. Model and external runtime terms apply separately. [Installation](docs/INSTALL.md) · [Benchmarks](docs/BENCHMARKS.md) · [Source build](docs/BUILD.md) · [Limitations](README.md#使用条件与边界).
