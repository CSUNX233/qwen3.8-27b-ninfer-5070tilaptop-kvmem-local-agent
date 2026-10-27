# v1.0.0 path-query and agent-guard evidence

This directory contains **aggregate, sanitized** evidence for v1.0.0. The
private replay requests, responses, per-seed logs and local maintenance paths
remain on the maintainer machine and are not uploaded.

- Base: v0.1.5 / `b79bb9b6eafe9b81c36358adeac44dd51cd36d5d`
- Verified engine SHA-256: `71259a21f7b525ca7477193a292e4d3623cc58b9efd785005ee934cc29673507`
- v0.1.5 rollback engine SHA-256: `38bc4d8c6be07afd6c74b902cc33403bf2ee139a2c50129d363d68d74a7609d6`
- Engine switches used by the upgrade launcher: `NINFER_KVMEM_QUERY_MODE=related-tool-head`,
  `NINFER_KVMEM_SUFFIX_REFRESH=tail`, `NINFER_KVMEM_QUERY_ASSEMBLE=1`
- Agent plugins: `integrations/dsh/local-agent/edit-guard.mjs`,
  `integrations/dsh/local-agent/task-ledger.mjs`

The numbers below are counts from controlled runs with the same model, NVFP4,
39168/6144 page pool, MTP3, xhigh and sampling. They are not a single universal
benchmark and do not cover real browser/game multi-step closure, 256K full
context, or other GPUs/models.

| Check | Baseline v0.1.5 | v1.0.0 |
| --- | --- | --- |
| 116,907-token fault history, complete path + valid read/edit | 1/4 | 4/4 |
| Same history, held-out 8 seeds | 4/8 | 8/8 (latest-tool-head family) |
| Causal four-class strict | 8/8 | 8/8 |
| Delayed-stress first-pass strict | 2/4 | 2/4 |
| Six-class short continuation path/strict | 10/12, 7/12 | 10/12, 7/12 |
| Structured one-retry recovery | no such mechanism (generic 0/10) | 10/10 |
| Real-task pilot, 4 tasks | 4/4, 11 steps | 4/4, 7 steps |
| Two complex tasks | 1/2, 8 steps | 2/2, 5 steps (with task ledger) |
| 500 numbered lines | — | pass with 32768 max_tokens; 12288 is reasoning-bound |
| 1500 numbered lines | — | pass with 65536 max_tokens |

Rejected experiments are not enabled: H7 equal dual query (holdout regression),
H8 forced transaction anchors (no stable gain), H11 128/256 tool-head expansion
(strict completion regressed; the selected tool messages were already fully
covered by 128 tokens).

H12 and the agent-layer tests are not an engine quality claim. On the recorded
failure turns, structured feedback recovered 10/10 while generic feedback
recovered 0/10; this tests recovery after one retry, not first-pass success.