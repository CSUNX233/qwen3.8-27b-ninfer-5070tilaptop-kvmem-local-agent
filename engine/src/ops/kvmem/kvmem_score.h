// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#pragma once

// KVMem 内容检索：按 query 打分，发布两级选页所需的同一份历史分数与候选排名。
//
// 【它是什么】把已经在库里的三件套接上主路径的**第一刀**：
//     kvmem_retrieve（官方 KVMem 论文 Eq.10 的 query-conditioned 打分器）
//   → kvmem_select  （host：sink/recent 恒留 + 中段按分降序）
//   → 一行 fprintf（对齐官方实现的三口径：selected 非连续 / skip>0 / window≈budget）
//
// 页表和 KV 恢复仍由 Program 管理；这里发布的排名由 Program 的 ring 路径消费，会决定历史可见页。
// 分数回传在索引追加前提交，只等待该份回传的事件；未打分的 chunk 保留上次发布的选择。
//
// 【开关】NINFER_TERNARY_KVMEM_SCORE=1（默认关）。OFF 臂：不分配、不启动、不读索引、不写日志。
//   ⚠️ 开关与 env 只在这个头文件里读（与 kvmem_shadow.h 同一条纪律：开关的读取点必须唯一）。
//
// 【为什么只有打分器需要新代码】索引（mean_k_index，prefill 时按 chunk 喂）、raw-K 收割
//   （raw_k_harvest）、窗口载体（kvmem_window）**都已经接在引擎里**；缺的正是中间那句"按分选块"。
//
// 【两处必须记住的接缝】
//   1. 索引存的是每块 key 的 **和**（不是 mean）⇒ 打分器要用 block_tokens 自己除；而索引里的计数是
//      **F32**，打分器要 **int32** ⇒ 这里在 host 侧按 blocks_written/tail_fill 造 int32 数组
//      （尾部不满的块必须带**真实填充**，不能带名义块大小 —— mean_k_index.h:127-131）。
//   2. query 必须是**内容帧**（de-RoPE 之后的位置无关帧），与索引同帧 ⇒ 引擎里那个可用的点只有
//      `qn`（RMS 归一化后、`ops::rope` 覆盖之前）。这就是为什么 accumulate() 的调用点在 rope 之前。
//
// 【已知边界（写死在代码里，别当没看见）】
//   * **多 lane 不安全**：mean-K 索引是**全进程一份**（mean_k_index.h:248-251 自陈），本探针的
//     score 缓冲同样是全进程一份 ⇒ 只在单路（--max-concurrency 1）下有意义。多路时必须 per-lane。
//   * 只对"短 query span"打分（≤ NINFER_TERNARY_KVMEM_SCORE_MAXQ，默认 256 个 token）：文档
//     ingest 那种几万 token 的 chunk 打分代价是 O(块数×query长)，会拖死 prefill；而且那种 chunk
//     的"query"本来也不是检索 query。
//   * ★ 2026-09-26（语义档）：NINFER_TERNARY_KVMEM_SCORE_QUERY_TAIL=N ⇒ 只把 chunk 的最后 N 个
//     token 当检索 query 打分（官方 kvmem_set_query_span 的等价物：query 是"问"，不是整段
//     ingest），MAXQ 的门限看 **span 长度**、不看 chunk 长度 —— 解掉"探针只在短 query chunk
//     上打分（skip long chunk tokens=1024 > MAXQ=256）"那块拦路石。默认 0 = 旧行为（逐位不变）。
//   * 第一层同分优先新页，ring 第二层同分优先旧页；缓存排名不得混淆这两个规则。

#include "ops/kvmem/kvmem_retrieve_launch.h"
#include "ops/kvmem/kvmem_select.h"
#include "ops/kvmem/mean_k_index.h"
#include "core/mapped_host_buffer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <cuda_runtime.h>

namespace ninfer::ops::detail {

// ---- 开关（唯一读取点；env 只读一次，与 kvmem_shadow_enabled() 同形）---------------------------
//
// ★ 2026-10-02 **默认翻转**（用户裁决："我们肯定是要开着的"）。理由不是口味，是实测：
//   内容打分是低显存环这条路上"修中段丢针"的那一刀，把它默认关着 = 把修挡在门外。
//   出厂形态（启动器只设五个 ring env、不设 KVMem 那三行）实测：池 4000 多针 n=6 **hit=2/6**，
//   两轮的错答一律是题面 filler 里的 `300`（静默错），而打开打分后是 turn1 6/6 + turn2 6/6。
//
//   三态规则（**显式优先**）：
//     NINFER_TERNARY_KVMEM_SCORE=0        ⇒ 关（负控：只有显式要求才关）
//     NINFER_TERNARY_KVMEM_SCORE=1        ⇒ 开（显式）
//     NINFER_TERNARY_KVMEM=0（大开关）    ⇒ 关（"不要 KVMem"这句话仍然算数）
//     都没表态 且 NINFER_KV_WINDOW > 0    ⇒ **开**（环被请求 = 低显存配置 = 这条修真正有用的场景）
//     都没表态 且 环没开                  ⇒ 关（全池档没有可回捞的页，开着只是白花力气）
//   ⇒ **修跟着二进制走**，不再依赖交付启动器是否写那三行 env。
inline bool kvmem_score_enabled() noexcept {
    static const bool on = [] {
        const auto explicit_zero = [](const char* name) {
            const char* v = std::getenv(name);
            return v != nullptr && v[0] != '\0' && v[0] == '0';
        };
        const char* v = std::getenv("NINFER_TERNARY_KVMEM_SCORE");
        if (v != nullptr && v[0] != '\0') { return v[0] != '0'; }   // 子开关表态优先
        if (explicit_zero("NINFER_TERNARY_KVMEM")) { return false; } // 大开关 =0 也算关
        const char* w = std::getenv("NINFER_KV_WINDOW");
        if (w == nullptr || w[0] == '\0') { return false; }
        return std::strtol(w, nullptr, 10) > 0;                      // 环开着 ⇒ 默认开
    }();
    return on;
}

inline std::int32_t kvmem_score_env_i32(const char* name, std::int32_t fallback) noexcept {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') { return fallback; }
    const long parsed = std::strtol(v, nullptr, 10);
    return parsed > 0 ? static_cast<std::int32_t>(parsed) : fallback;
}

// ---- 探针状态（进程级单例，与 mean-K 索引同寿命）---------------------------------------------
struct KvMemScoreProbe {
    // device 缓冲
    float*        score            = nullptr;  // [capacity_blocks]，每 chunk 前清零（打分器只 ADD）
    std::int32_t* block_tokens_dev = nullptr;  // [capacity_blocks]，尾块带真实填充（device 侧）
    // host staging
    std::vector<float>        score_host;
    std::vector<std::int32_t> tokens_host;
    // One pinned RAM staging allocation; no additional Device score allocation. The event is
    // recorded BEFORE index append, so consuming scores need not wait for that independent work.
    std::unique_ptr<MappedHostBuffer> score_readback;
    cudaEvent_t score_ready = nullptr;
    bool readback_pending = false;
    KvMemPageRanking page_ranking;
    // LOCAL (2026-10-02, the caller step): the block ids the last finish() selected, ASCENDING.
    // This is what the ring consumes as its `preferred` set. It exists because this file used to be a
    // probe that only LOGGED the selection ("本探针不改可见集"); a caller that reads it is the
    // difference between "we can see the ranking" and "the ranking decides what the model reads".
    // EMPTY means "no fresh selection" (feature off, not armed, or nothing scored this chunk) and the
    // caller must fall back to its previous behaviour -- never read empty as "keep nothing".
    std::vector<std::int32_t> kept;

    std::int32_t capacity_blocks = 0;
    std::int32_t block_tokens    = 0;      // == kPagedKVPageSize，由调用方传入（house rule：不许写死）
    std::int32_t chunk_tokens    = 0;      // live 平面的 token 行数（= q_layer_stride 口径）
    std::int32_t n_blocks        = 0;      // 本 chunk 打分的"历史"块数（= 打分开始时的 blocks_written）
    // ---- ★ 分数的**出处**（2026-09-26 语义档要用：静默用错查询/错时刻 = 静默错答案）------------
    std::int32_t query_span_begin  = 0;    // 本次 query span 在 live 平面里的起点（QUERY_TAIL 档）
    std::int32_t query_span_tokens = 0;    // 本次真正打分的 query token 数（= span 长度）
    // ★ D-12（2026-10-02）：**绝对** query span（= 最后一轮 role=user 的 token 区间）+ 本 chunk 的绝对
    //   起点。两者都已知时优先于尾窗规则（见 kvmem_score_accumulate）。-1 = 未知 ⇒ 回落尾窗。
    std::int32_t abs_query_begin = -1;
    std::int32_t abs_query_end   = -1;
    std::int32_t abs_chunk_begin = -1;
    bool logged_span_miss        = false;   // 空交集只喊一次（见 accumulate 的回落说明）
    std::int32_t scored_n_blocks   = 0;    // 这份分数描述块 [0, scored_n_blocks)（finish 时登记）
    bool scored_last_chunk = false;        // 最近一个 prefill chunk 真打过分（语义档的准入闸）
    std::int32_t layers_total    = 0;
    std::int32_t n_heads         = 0;
    std::int32_t n_kv_heads      = 0;
    std::int32_t head_dim        = 0;
    bool armed    = false;                 // 本 chunk 正在打分
    bool scored_this_chunk = false;        // 本 chunk 真的累加过（否则 finish 打出来的是全零假日志）
    bool failed   = false;                 // 出过致命几何/分配错 ⇒ 之后一律不打分（但引擎继续跑）
    bool logged_once = false;

    // Drain a cancelled/failed round before reusing its single staging allocation. Normal rounds
    // consume at their tail, before any query/history metadata can be changed by the next chunk.
    void drain_readback() noexcept {
        if (readback_pending) {
            (void)cudaEventSynchronize(score_ready);
            readback_pending = false;
        }
    }
    ~KvMemScoreProbe() {
        drain_readback();
        if (score_ready != nullptr) { (void)cudaEventDestroy(score_ready); }
        if (score != nullptr) { (void)cudaFree(score); }
        if (block_tokens_dev != nullptr) { (void)cudaFree(block_tokens_dev); }
    }
};

inline KvMemScoreProbe& kvmem_score_probe() {
    static KvMemScoreProbe probe;
    return probe;
}

// Read the last published ranking without re-sorting the same scores at every ring mapping update.
// Empty means the content path has no candidates in this range; Program retains its lexical fallback.
inline std::vector<std::uint32_t> kvmem_score_retrieval_pages(std::uint32_t begin,
                                                            std::uint32_t end,
                                                            std::uint32_t budget) {
    return kvmem_score_probe().page_ranking.select(begin, end, budget);
}

// ---- ①b 绝对 query span（D-12, 2026-10-02）------------------------------------------------------
//
// 官方口径：检索 query 是**最后一轮 role=user**（服务层捕获，捕获与打分分两步；出处
// E:\infer-docs\02-交接与状态\KVMem-移植-交接-20260921.md:308 "官方捕获整个被标记的 query span
// （服务层口径：取最后一轮 role=user）"）。frontend 在准备 prompt 时把该轮的 token 区间（**绝对下标**）
// 交到这里，打分时再与当前 chunk 求交 —— 这就是那份"两步"的落地，不再靠"尾 N token"去猜问句在哪。
//
// 与整个探针一样，这份状态是**进程级**的 ⇒ 只在 --max-concurrency 1 下有意义（多路必须 per-lane）。
// begin < 0 = 未知 ⇒ 回落 QUERY_TAIL 尾窗规则：那既是既有行为，也是这条改动的**负控**。
inline void kvmem_score_set_query_span(std::int32_t begin, std::int32_t end) noexcept {
    KvMemScoreProbe& p = kvmem_score_probe();
    p.abs_query_begin = begin;
    p.abs_query_end   = end;
}

// 本 chunk 的**绝对** token 起点（= 该序列此刻已写入 KV 的 token 数，即 text_kv_base）。只有它已知，
// 绝对 span 才能落到 chunk 内的局部下标上；未知则同样回落尾窗。
inline void kvmem_score_set_chunk_origin(std::int32_t abs_begin) noexcept {
    kvmem_score_probe().abs_chunk_begin = abs_begin;
}

inline void kvmem_score_fail(const char* why) noexcept {
    KvMemScoreProbe& p = kvmem_score_probe();
    p.failed = true;
    p.armed  = false;
    std::fprintf(stderr, "kvmem_score: DISABLED (%s)\n", why);
}

// ---- ① chunk 开始：确保容量 + 清零 + 记下"历史块数"--------------------------------------------
//
// history_blocks 取**打分开始那一刻**的 blocks_written()：本 chunk 自己还没进索引（索引在 chunk
// 之后才 append_round），所以它天然是"历史"——这正是检索该对的东西。
inline void kvmem_score_begin(std::int32_t requested_capacity, std::int32_t history_blocks,
                              std::int32_t tail_fill, std::int32_t block_tokens,
                              std::int32_t layers_total, std::int32_t n_heads,
                              std::int32_t n_kv_heads, std::int32_t head_dim,
                              cudaStream_t stream) noexcept {
    if (!kvmem_score_enabled()) { return; }
    KvMemScoreProbe& p = kvmem_score_probe();
    if (p.failed) { return; }
    p.drain_readback();

    // LOCAL (D-12, 2026-10-02): a round that ends up NOT scoring -- because its chunk does not contain
    // the query span (see kvmem_score_accumulate) -- must not leave the PREVIOUS round's selection
    // behind. That selection answers another query (possibly another request's), and the ring would
    // restore the pages THAT query asked for. "Empty = no fresh selection" is already the contract the
    // caller implements (context.cpp then falls back to the lexical ranking), so clearing here is what
    // makes that fallback honest instead of accidental. Decode never calls this, so the last prefill
    // chunk's selection stays valid for the whole decode phase (the official `step` semantics).
    // ★ 2026-10-02 实测撤回（曾在此处 `p.kept.clear()`）：**不能每轮清空上一个选择**。
    //   池 4000 多针臂的日志直读：**一个请求只有 turn1 的 ~42 条 SELECT，turn2 一条都没有** ——
    //   带缓存前缀的那一轮走"后缀预填"，其 chunk 不产生打分轮（采集钩子只在 `Phase::Prefill` 武装），
    //   所以 turn2 的整段解码**靠继承 turn1 的选择**（内容打分挑出来的那批页）。
    //   一旦在这里清空，继承被抹掉 ⇒ `select_retrieval_pages` 退回词法 IDF ⇒ 词面不相交的问句必丢
    //   （实测把 turn2 由 6/6 打到 3/6）。"陈旧选择"在这里是**承重**的，不是需要清理的脏状态。
    //   契约不变：**只有 finish() 真的打了分才会覆盖 kept**；本轮没打分 ⇒ 保留上一次的选择。
    if (requested_capacity <= 0 || history_blocks <= 0 || block_tokens <= 0) { p.armed = false; return; }

    if (p.score == nullptr || p.capacity_blocks < requested_capacity) {
        // 只在**首个 chunk** 分配；prefill 不在 CUDA graph 捕获期（捕获期禁 cudaMalloc，坑表 §3.7）
        if (p.score != nullptr) { (void)cudaFree(p.score); p.score = nullptr; }
        if (p.block_tokens_dev != nullptr) { (void)cudaFree(p.block_tokens_dev); p.block_tokens_dev = nullptr; }
        const std::size_t bytes = static_cast<std::size_t>(requested_capacity) * sizeof(float);
        if (cudaMalloc(reinterpret_cast<void**>(&p.score), bytes) != cudaSuccess ||
            cudaMalloc(reinterpret_cast<void**>(&p.block_tokens_dev),
                       static_cast<std::size_t>(requested_capacity) * sizeof(std::int32_t)) !=
                cudaSuccess) {
            kvmem_score_fail("cudaMalloc");
            return;
        }
        p.capacity_blocks = requested_capacity;
        p.score_host.assign(static_cast<std::size_t>(requested_capacity), 0.0F);
        p.tokens_host.assign(static_cast<std::size_t>(requested_capacity), 0);
        try {
            p.score_readback = std::make_unique<MappedHostBuffer>(bytes);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "kvmem_score: pinned readback allocation failed: %s\n", error.what());
            kvmem_score_fail("pinned readback");
            return;
        }
        if (p.score_ready == nullptr &&
            cudaEventCreateWithFlags(&p.score_ready, cudaEventDisableTiming) != cudaSuccess) {
            kvmem_score_fail("readback event");
            return;
        }
        std::fprintf(stderr, "kvmem_score: ARMED capacity_blocks=%d layers_total=%d heads=%d kv_heads=%d "
                             "head_dim=%d\n", requested_capacity, layers_total, n_heads, n_kv_heads, head_dim);
    }

    p.n_blocks     = history_blocks;
    p.block_tokens = block_tokens;
    p.layers_total = layers_total;
    p.n_heads      = n_heads;
    p.n_kv_heads   = n_kv_heads;
    p.head_dim     = head_dim;
    p.armed        = true;
    p.scored_this_chunk = false;
    p.scored_last_chunk = false;

    // block_tokens：尾块（blocks_written-1）用真实填充，其余用名义块大小
    for (std::int32_t i = 0; i < history_blocks; ++i) {
        p.tokens_host[static_cast<std::size_t>(i)] =
            (i == history_blocks - 1) ? (tail_fill > 0 ? tail_fill : block_tokens) : block_tokens;
    }
    if (cudaMemcpyAsync(p.block_tokens_dev, p.tokens_host.data(),
                        static_cast<std::size_t>(history_blocks) * sizeof(std::int32_t),
                        cudaMemcpyHostToDevice, stream) != cudaSuccess) {
        kvmem_score_fail("block_tokens H2D");
        return;
    }
    if (cudaMemsetAsync(p.score, 0, static_cast<std::size_t>(history_blocks) * sizeof(float), stream) !=
        cudaSuccess) {
        kvmem_score_fail("score memset");
        return;
    }
}

// ---- ② 每层一次：把该层的 query 行累加到 score 上 ----------------------------------------------
//
// 调用点必须在 `ops::rope(qn, ...)` **之前**：rope 会原地覆盖 qn，之后就没有内容帧的 query 了。
inline void kvmem_score_accumulate(std::int32_t fidx, const void* q, std::int32_t n_query_tokens,
                                   std::int32_t chunk_tokens, cudaStream_t stream) noexcept {
    if (!kvmem_score_enabled()) { return; }
    KvMemScoreProbe& p = kvmem_score_probe();
    if (!p.armed || p.failed) { return; }
    if (n_query_tokens <= 0 || p.n_blocks <= 0) { return; }

    // ★ query span（2026-09-26 语义档）：NINFER_TERNARY_KVMEM_SCORE_QUERY_TAIL=N ⇒ 只把本 chunk 的
    //   **最后 N 个 token** 当检索 query（官方 kvmem_set_query_span 的等价物）。MAXQ 的门限看
    //   **span 长度**、不看 chunk 长度。
    //
    //   ★ 默认值 2026-10-02 从 0（整 chunk）改成 64（缺陷 D-9）。依据是低池多针臂上的**单旋钮**
    //   剂量-响应 —— 池 4000（63 页，回捞预算 31 页）、同一支二进制、只改这一个 env、n=6：
    //       TAIL=256 -> turn1 6/6，turn2 **2/6**（第二个针静默丢；GATE RED）
    //       TAIL= 64 -> turn1 6/6，turn2 6/6
    //       TAIL= 32 -> turn1 6/6，turn2 6/6
    //   机理：末 chunk（1024 token）的"最后 256 行"里约 200 行是题面 filler + **上一轮的答案**
    //   （答案文本正好是第一个针页的强匹配），而 score 是对 query 行**求和**（契约
    //   `sum_b score[b] == n_query_tokens`，见 finish 的内置 oracle）⇒ 泛化页/旧目标页压过当前问句
    //   的目标页；池小（预算薄）时就直接表现为丢针。跨度收回问句长度即恢复。
    //   旧行为（整 chunk）现在只能显式给一个 >= chunk 的 N 拿到；但 chunk=1024 时整 chunk 会被
    //   MAXQ=256 的门限**直接 skip**（打分器一声不吭地什么都不做），所以"未设/0 ⇒ 64"更安全。
    const std::int32_t query_tail = kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_QUERY_TAIL", 64);
    const std::int32_t maxq = kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_MAXQ", 256);
    std::int32_t span_begin  = 0;
    std::int32_t span_tokens = (chunk_tokens > 0 && chunk_tokens < n_query_tokens) ? chunk_tokens
                                                                                  : n_query_tokens;
    // ★ D-12：**绝对 query span（最后一轮 user）与本 chunk 求交**，只在它"问句大小"时才生效。
    //   · 交集为空 ⇒ 本 chunk 里没有问句（span 陈旧，或这块在 span 之外）⇒ **回落尾窗规则**
    //     （= 修前行为；绝不比"什么都不打分"差，见下面的报错行）。
    //   · 交集非空但**比 MAXQ 长** ⇒ 也**回落尾窗规则**，不截成"span 的尾 256"。
    //     依据（2026-10-02 实测）：题面把 42k 正文与问句放在**同一条** user 消息里时 span = 整条消息，
    //     截尾 256 等价于把 QUERY_TAIL 从 64 放宽到 256 —— 而那正是低池丢针的那一档（256⇒turn2 2/6）。
    //     "span 比 MAXQ 长"意味着**我们没能定位到一个问句**，此时老老实实用已验证的尾窗更安全。
    //   · 只有 span 本身 ≤ MAXQ（真正的"问句跨度"）才旁路尾窗规则 —— 那才是这条改动的用武之地。
    const bool abs_span_known = p.abs_query_begin >= 0 && p.abs_query_end > p.abs_query_begin &&
                                p.abs_chunk_begin >= 0 && chunk_tokens > 0 &&
                                kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_SPAN_OFF", 0) == 0;
    bool span_applied = false;
    if (abs_span_known) {
        const std::int32_t local_begin = p.abs_query_begin - p.abs_chunk_begin;
        const std::int32_t local_end   = p.abs_query_end - p.abs_chunk_begin;
        const std::int32_t first       = local_begin > 0 ? local_begin : 0;
        const std::int32_t last        = local_end < chunk_tokens ? local_end : chunk_tokens;
        if (last > first && (last - first) <= maxq) {
            span_begin   = first;
            span_tokens  = last - first;
            span_applied = true;
        } else if (!p.logged_span_miss) {
            // LOCAL FIX (2026-10-02, found the hard way): an EMPTY intersection must NOT disable the
            // scorer for the rest of the request. The first version simply returned here -- and because
            // the span is a process-global published per request, a STALE span (turn 1's prompt-wide
            // span, left behind when the suffix-only prefill of a cached-prefix turn 2 never came
            // through the publishing path) made every turn-2 chunk "not contain the query" => nothing
            // scored => the ring fell back to the lexical ranking => the very lever this change exists
            // for was switched off silently. Measured cost: turn2 6/6 -> 3/6 at pool 4000, with ZERO
            // SELECT lines for turn 2 in the log. Falling back to the tail rule is the honest failure
            // mode: that IS the pre-D-12 behaviour, and it is never worse than scoring nothing.
            p.logged_span_miss = true;
            std::fprintf(stderr,
                         "kvmem_score: query span [%d,%d) not usable for chunk [%d,%d) (empty overlap "
                         "or longer than MAXQ=%d) -- falling back to the tail rule (QUERY_TAIL)\n",
                         p.abs_query_begin, p.abs_query_end, p.abs_chunk_begin,
                         p.abs_chunk_begin + chunk_tokens, maxq);
        }
    }
    // LOCAL FIX (2026-10-10): a chunk that provably does not intersect the KNOWN absolute query
    // span must not score at all. The tail fallback below exists for the case where the span is
    // UNKNOWN (stale/absent frontend span); applying it to chunks that sit elsewhere makes every
    // chunk of a long context score its own tail against all history -- an O(chunks x blocks) scan
    // whose only effect is to make the ring restore pages the current question never asked for.
    // "No fresh selection" is already a supported state (the ring keeps the previous selection, or
    // falls back to the lexical ranking), and the chunk that DOES contain the span still scores
    // normally. NINFER_TERNARY_KVMEM_SCORE_SPAN_TAIL_FALLBACK=1 restores the old per-chunk tail
    // scoring. (Note: kvmem_score_env_i32 treats 0 as "unset", hence the inverted name.)
    const bool span_tail_fallback =
        kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_SPAN_TAIL_FALLBACK", 0) != 0;
    const bool chunk_outside_known_span =
        !span_tail_fallback && abs_span_known &&
        (p.abs_query_end <= p.abs_chunk_begin ||
         p.abs_query_begin >= p.abs_chunk_begin + chunk_tokens);
    if (chunk_outside_known_span) {
        static bool logged_span_skip = false;
        if (!logged_span_skip) {
            logged_span_skip = true;
            std::fprintf(stderr,
                         "kvmem_score: chunk [%d,%d) does not intersect the known query span "
                         "[%d,%d) -- not scoring this chunk (ring keeps the previous selection). "
                         "Set NINFER_TERNARY_KVMEM_SCORE_SPAN_TAIL_FALLBACK=1 for the old tail rule\n",
                         p.abs_chunk_begin, p.abs_chunk_begin + chunk_tokens,
                         p.abs_query_begin, p.abs_query_end);
        }
        return;
    }
    if (!span_applied && query_tail > 0 && chunk_tokens > 0) {
        const std::int32_t tail = query_tail < chunk_tokens ? query_tail : chunk_tokens;
        span_begin  = chunk_tokens - tail;
        span_tokens = tail;
    }
    if (span_tokens > maxq) {   // query 太长：不打分（它也不是检索 query）
        if (!p.logged_once) {
            p.logged_once = true;
            std::fprintf(stderr, "kvmem_score: skip long query span tokens=%d (> MAXQ=%d); chunk=%d begin=%d\n",
                         span_tokens, maxq, chunk_tokens, span_begin);
        }
        return;
    }
    p.chunk_tokens      = chunk_tokens;   // live 平面的行数（= q_layer_stride）
    p.query_span_begin  = span_begin;
    p.query_span_tokens = span_tokens;    // 供 finish 的 oracle 自检用（正确口径 = span 的 token 数）

    ops::MeanKIndex* index = ops::mean_k_index_for(p.layers_total, p.n_kv_heads, p.head_dim,
                                                  p.capacity_blocks);
    if (index == nullptr || index->blocks_written() <= 0) { return; }
    if (fidx < 0 || fidx >= index->layers()) { return; }

    const Tensor* prepared = nullptr;
    try {
        if (!p.scored_this_chunk) { index->begin_scoring_round(p.n_blocks, stream); }
        prepared = &index->layer_sums_for_scoring(fidx, p.n_blocks, stream);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "kvmem_score: index preparation failed: %s\n", error.what());
        kvmem_score_fail("index preparation");
        return;
    }
    const Tensor& sums = *prepared;
    if (sums.data == nullptr) { return; }

    KvMemRetrieveConfig cfg;
    cfg.n_layers          = 1;                      // 逐层调用
    cfg.n_layers_total    = p.layers_total;         // head_w = 1/(总层数×query 头数)
    cfg.n_query_tokens    = span_tokens;
    cfg.n_heads           = p.n_heads;
    cfg.n_kv_heads        = p.n_kv_heads;
    cfg.head_dim          = p.head_dim;
    cfg.n_blocks          = p.n_blocks;
    cfg.q_layer_stride    = chunk_tokens;           // live plane 的 token 数（>= n_query_tokens）
    cfg.kbar_layer_stride = p.capacity_blocks;      // >= n_blocks
    cfg.q_token_begin     = span_begin;
    cfg.budget_blocks     = 0;                      // 掩码只用在这里；探针不设带（Official 规则见下）
    cfg.sink_blocks       = 0;
    cfg.recent_blocks     = 0;
    cfg.mask_mode         = KvMemRetrieveMaskMode::Never;   // 探针要"全历史"的原始分数，不套带掩码
    cfg.dtype             = KvMemRetrieveDtype::BF16;

    const cudaError_t status =
        ops::kvmem_retrieve_scores(cfg, p.score, q, static_cast<const float*>(sums.data),
                                   p.block_tokens_dev, stream);
    try {
        index->release_scoring_layer(fidx, stream);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "kvmem_score: index release failed: %s\n", error.what());
        kvmem_score_fail("index release");
        return;
    }
    if (status != cudaSuccess) {
        std::fprintf(stderr, "kvmem_score: launch failed layer=%d: %s\n", fidx, cudaGetErrorString(status));
        kvmem_score_fail("launch");
        return;
    }
    p.scored_this_chunk = true;
}

// ---- ③ chunk 收尾：D2H + 选块 + 一行证据日志 ---------------------------------------------------
//
// 在 prefill chunk 尾部、CUDA Graph 捕获之外提交。先记录回传完成事件，再提交同 stream 的索引追加。
//
// 免 oracle 自检：契约保证 `sum_b score[b] == n_query_tokens`（与带掩码与否无关）⇒ sum_score 就是
// 内置 oracle。对不上说明打分器没跑、跑了半截、或几何配错 —— 三种都得当场喊出来。
inline void kvmem_score_submit_readback(cudaStream_t stream) noexcept {
    if (!kvmem_score_enabled()) { return; }
    KvMemScoreProbe& p = kvmem_score_probe();
    if (!p.armed || p.failed || p.n_blocks <= 0) { return; }
    p.armed = false;
    if (!p.scored_this_chunk) { return; }   // 本 chunk 没打过（如长 ingest）⇒ 不打印全零假日志
    // DIAGNOSTIC ONLY (NINFER_KVMEM_PROF, never set in production): stop before the per-chunk
    // D2H + cudaStreamSynchronize + host-side selection so that tail can be timed in isolation.
    if (std::getenv("NINFER_KVMEM_PROF") != nullptr) { return; }
    const std::size_t bytes = static_cast<std::size_t>(p.n_blocks) * sizeof(float);
    if (cudaMemcpyAsync(p.score_readback->host_data(), p.score, bytes, cudaMemcpyDeviceToHost, stream) !=
        cudaSuccess) {
        kvmem_score_fail("score D2H");
        return;
    }
    if (cudaEventRecord(p.score_ready, stream) != cudaSuccess) {
        // The copy was submitted but has no usable fence. Drain it before any storage is released.
        (void)cudaStreamSynchronize(stream);
        kvmem_score_fail("readback event record");
        return;
    }
    p.readback_pending = true;
}

// Consumed within the same round, after index work has been submitted. Nothing can overwrite the
// score buffers or query metadata between submit and consumption. Only this result's event is waited
// on; index append remains ordered on the original stream for its next GPU consumer.
inline void kvmem_score_finish(const char* label) noexcept {
    KvMemScoreProbe& p = kvmem_score_probe();
    if (!p.readback_pending || p.failed) { return; }
    if (cudaEventSynchronize(p.score_ready) != cudaSuccess) {
        p.readback_pending = false;
        kvmem_score_fail("readback event wait");
        return;
    }
    p.readback_pending = false;
    std::memcpy(p.score_host.data(), p.score_readback->host_data(),
                static_cast<std::size_t>(p.n_blocks) * sizeof(float));
    const std::int32_t q_used = p.query_span_tokens > 0 ? p.query_span_tokens : p.chunk_tokens;

    // 第一层的原有预算；ring 使用更小的恢复预算时从同一份候选排名筛选。
    // 默认取"小预算"以便**逼出选择**：budget 32768 / sink 4096 / recent 0（官方那台仪器用的就是这组）。
    const std::int32_t budget_tokens = kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_BUDGET", 32768);
    const std::int32_t sink_tokens   = kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_SINK", 4096);
    const std::int32_t recent_tokens = std::getenv("NINFER_TERNARY_KVMEM_SCORE_RECENT") != nullptr
                                           ? kvmem_score_env_i32("NINFER_TERNARY_KVMEM_SCORE_RECENT", 0)
                                           : 0;

    double sum_score = 0.0;
    for (std::int32_t i = 0; i < p.n_blocks; ++i) { sum_score += static_cast<double>(p.score_host[i]); }

    KvMemSelectConfig scfg;
    scfg.total_blocks  = p.n_blocks;
    scfg.budget_blocks = budget_tokens / p.block_tokens;
    scfg.sink_blocks   = sink_tokens / p.block_tokens;
    scfg.recent_blocks = recent_tokens / p.block_tokens;
    const KvMemSelectResult sel = ops::kvmem_select_blocks(scfg, p.score_host.data(), 0);
    // LOCAL (2026-10-02, the caller step): publish the selection for the ring. Written AFTER the
    // selector returns and only when this chunk really scored, so a failed/skipped round leaves the
    // previous selection intact instead of emptying it.
    p.kept = sel.kept;
    p.page_ranking.update(p.kept, p.score_host, p.n_blocks);
    p.scored_n_blocks   = p.n_blocks;
    p.scored_last_chunk = true;

    // 选择结构的形状（这是判据的核心：非连续 ⇒ 真在选择）
    std::int32_t runs = 0;
    for (std::size_t i = 0; i < sel.kept.size(); ++i) {
        if (i == 0 || sel.kept[i] != sel.kept[i - 1] + 1) { ++runs; }
    }
    const std::int32_t skip = p.n_blocks - static_cast<std::int32_t>(sel.kept.size());
    const std::int32_t kept_first = sel.kept.empty() ? -1 : sel.kept.front();
    const std::int32_t kept_last  = sel.kept.empty() ? -1 : sel.kept.back();

    // ★ 把**选中的块号本身**打出来：没有它就无法核对"含针的块有没有被选中"（聚合量 runs/skip 做不到）。
    //   与官方实现同形（它也是逐行 `KVMEM_TRACE selected <ids...>`）。只在短 query chunk 上出现。
    {
        std::fprintf(stderr, "kvmem_score: KEPT");
        for (std::size_t i = 0; i < sel.kept.size(); ++i) {
            std::fprintf(stderr, " %d", static_cast<int>(sel.kept[i]));
        }
        std::fprintf(stderr, "\n");
    }

    std::fprintf(stderr,
                 "kvmem_score: SELECT label=%s n_blocks=%d budget_blocks=%d sink_blocks=%d "
                 "recent_blocks=%d kept=%zu runs=%d skip=%d window_tokens=%zu "
                 "sink_kept=%d recent_kept=%d scored_kept=%d candidates=%d kept_range=[%d,%d] "
                 "sum_score=%.3f query_tokens=%d span_mode=%s span_abs=[%d,%d) chunk_abs=%d "
                 "span_local=[%d,%d) oracle_ok=%d scale_floor=%.6f\n",
                 label, p.n_blocks, scfg.budget_blocks, scfg.sink_blocks, scfg.recent_blocks,
                 sel.kept.size(), runs, skip,
                 sel.kept.size() * static_cast<std::size_t>(p.block_tokens), sel.sink_kept,
                 sel.recent_kept, sel.scored_kept, sel.candidates, kept_first, kept_last, sum_score,
                 q_used, p.abs_query_begin >= 0 ? "abs" : "tail", p.abs_query_begin, p.abs_query_end,
                 p.abs_chunk_begin, p.query_span_begin, p.query_span_begin + p.query_span_tokens,
                 (q_used > 0 && sum_score > 0.5 * q_used && sum_score < 2.0 * q_used) ? 1 : 0,
                 static_cast<double>(ops::kvmem_retrieve_scale(p.head_dim)));
}

// 逐块分数导出（给离线判据用：核对"含针的块"有没有被选中）。只在 switch 打开且本 chunk 打过时写。
inline void kvmem_score_dump_if_requested(const char* label) noexcept {
    if (!kvmem_score_enabled()) { return; }
    const char* path = std::getenv("NINFER_TERNARY_KVMEM_SCORE_DUMP");
    if (path == nullptr || path[0] == '\0') { return; }
    KvMemScoreProbe& p = kvmem_score_probe();
    if (p.n_blocks <= 0) { return; }
    FILE* f = std::fopen(path, "ab");
    if (f == nullptr) { return; }
    std::fprintf(f, "# %s n_blocks=%d\n", label, p.n_blocks);
    for (std::int32_t i = 0; i < p.n_blocks; ++i) {
        std::fprintf(f, "%d %.6f\n", i, static_cast<double>(p.score_host[static_cast<std::size_t>(i)]));
    }
    std::fclose(f);
}

} // namespace ninfer::ops::detail
