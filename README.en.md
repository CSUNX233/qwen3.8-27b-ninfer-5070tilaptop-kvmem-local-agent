# NInfer + KVMem: a local 27B Agent on 12GB GPUs

Run a **Qwen3.8-27B local Agent** for coding, files and web tools on consumer GPUs. NInfer handles inference; KVMem stores historical KV in RAM and retrieves pages into GPU memory. MTP speculative decoding, a cache guard and an optional dsh web UI support long task continuations. The logical context is configured to 256K; this does not imply attending to all history every step or completed full-256K validation. [中文](README.md)

## RTX 5070 Ti Laptop 12GB historical measurements

Hardware: **RTX 5070 Ti Laptop 12GB / i7-14650HX / 32GB RAM / Windows**. Model: Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP3. These retained records used the older RK4V4 setup; **they are not v0.1.5 / NVFP4 remeasurements**.

| Workload | Measured speed | Conditions |
|---|---:|---|
| Short writing decode | 71.81 tokens/s | Small-matrix dispatch enabled; mean of two runs |
| Short code decode | 84.49 tokens/s | Same short-task group; mean of two runs |
| Cached repeated code decode | 85.00 tokens/s | Same group, cached requests; mean of two runs |
| Cold prefill, 110,019 tokens | 620.48 tokens/s, about 177.31 seconds | Earlier single measurement; subsequent decode 69.28 tokens/s |

Short throughput fixtures explicitly used temperature=0, seed=1234 and reasoning=none; daily use defaults to xhigh. Power, workload, KV format and cache hits affect speed. Records from different stages are not a single combined benchmark. [Full conditions](docs/BENCHMARKS.md) · [Demo](docs/demo/README.md)

## v1.0.0 update

**The model, NVFP4, 39168/6144 page pool, MTP3, xhigh, sampling and answer-penalty settings are unchanged from v0.1.5. v1.0.0 adds a query layer and an agent execution layer.**

- **Query layer (engine switches, legacy by default; the upgrade launcher enables them):** `related-tool-head`, `suffix refresh=tail`, and cross-chunk `QUERY_ASSEMBLE=1`. On the real 116,907-token fault history, complete target path plus a valid read/edit improved from **1/4 to 4/4** (latest-tool-head family); the 8-seed holdout improved from **4/8 to 8/8**. Causal four-class remains **8/8**, delayed-stress first pass remains **2/4**, and six-class short continuation remains **10/12 path and 7/12 strict**.
- **Agent layer:** `edit-guard` validates the target, action and exact old/new before a single-file replacement and does not silently rewrite arguments; `task-ledger` reminds the model of remaining actions on multi-action tasks. One structured retry recovered **10/10** recorded failures (generic feedback 0/10); two complex tasks improved from **1/2 to 2/2** (move + edit + write, zero unintended changes).
- **Long output:** 500-line and 1500-line numbered outputs were complete and contiguous with sufficient `max_tokens`; the 500-line/12288 failure was reasoning-budget exhaustion, not a sequence bug.
- **Boundaries:** no claim of universal superiority. Causal/short tasks are flat, delayed-stress first pass is unchanged, and real browser/game multi-step closure, 256K and other GPUs/models are not validated. H7 dual-query, H8 forced anchors and H11 tool-head expansion are rejected and not enabled. Private raw requests/logs stay local; public aggregates are in [evidence](benchmarks/evidence/path-query-v1.0.0/verification.json) and [the experiment record](docs/PATH_QUERY_EXPERIMENT.md).
- **Install/rollback:** engine SHA-256 `71259a21f7b525ca7477193a292e4d3623cc58b9efd785005ee934cc29673507`; the v0.1.5 engine `38bc4d8c6be07afd6c74b902cc33403bf2ee139a2c50129d363d68d74a7609d6` remains the rollback. Source tag `v1.0.0`; no 1.35GB engine asset was uploaded in this pass. See [Release Notes](docs/RELEASE_NOTES.md).

## v0.1.5 history


**C2.1 is ported onto the explicit v0.1.4 baseline**, improving long-history cache restoration and Host KV demotion. It retains v0.1.4 parsing, sampling, NVFP4 reducer, FP16 GDN and answer-only penalty fixes. Text+MTP and xhigh remain the defaults; vision is off. [Release](https://github.com/CSUNX233/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/releases/tag/v0.1.5) · [Release notes](docs/RELEASE_NOTES.md)

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
| `NInfer-v0.1.5-Windows-sm120a-full.zip` | GSQ-RCO IQ3_XXS text+MTP and complete Huihui text+vision+MTP; use `01-setup-gsq.cmd`. No separate Huihui text-only copy is included. |

Both default to text-only; the full bundle defaults to GSQ. After stopping the full bundle, `09-enable-cpu-vision.cmd` switches to Huihui with CPU preprocessing/vision encoding; text/MTP still run on GPU. This adds RAM use and first-image latency. `09-disable-vision-gsq.cmd` returns to GSQ text-only. Only one model is loaded at a time. Start with `02-start.cmd`, stop with `03-stop.cmd`. A compatible NVIDIA driver and Microsoft Edge remain system prerequisites. [Offline guide](docs/OFFLINE_BUNDLE.md)

Both include deployment instructions, Agent installation/troubleshooting prompts and the VRAM adjustment order. **Installation integrity checks and `04-verify.cmd` require no models.** Missing unselected models do not fail verification; setup/start require the selected model. Optional `-CheckModels` additionally hashes bundled models that are still present. The installer revision is available on `main` and in rebuilt offline bundles; the published engine and source tag are unchanged.

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
