// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#include "ops/kvmem/kvmem_score.h"
#include "core/device.h"
#include "core/paged_kv_cache.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>

using namespace ninfer;
using namespace ninfer::ops;
using namespace ninfer::ops::detail;

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

// Independent membership oracle: a candidate is selected iff fewer than budget in-range
// candidates precede it under the ring's score-descending / page-ascending total order.
std::vector<std::uint32_t> oracle(std::span<const std::int32_t> kept,
                                  std::span<const float> scores, int scored,
                                  std::uint32_t begin, std::uint32_t end, std::uint32_t budget) {
    std::vector<std::uint32_t> out;
    auto valid = [&](int b) {
        return b >= 0 && b < scored && static_cast<std::size_t>(b) < scores.size() &&
               static_cast<std::uint32_t>(b) >= begin && static_cast<std::uint32_t>(b) < end;
    };
    if (budget == 0 || end <= begin) { return out; }
    for (int b : kept) {
        if (!valid(b)) { continue; }
        std::uint32_t preceding = 0;
        for (int other : kept) {
            if (valid(other) && (scores[other] > scores[b] ||
                                  (scores[other] == scores[b] && other < b))) { ++preceding; }
        }
        if (preceding < budget) { out.push_back(static_cast<std::uint32_t>(b)); }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void test_ranking() {
    KvMemPageRanking ranking;
    std::mt19937 rng(741);
    std::size_t checks = 0;
    for (int size : {0, 1, 7, 64, 512, 4096}) {
        std::vector<float> scores(size);
        std::vector<int> kept;
        for (int round = 0; round < 5; ++round) {
            kept.clear();
            for (int b = 0; b < size; ++b) {
                scores[b] = round == 0 ? 0.0f : float(int(rng() % 23) - 11);
                if (round == 2 && b % 67 == 0) { scores[b] = -INFINITY; }
                if (round == 3 && b % 71 == 0) { scores[b] = INFINITY; }
                if (b % 3 != 1 || round == 0) { kept.push_back(b); }
            }
            kept.insert(kept.begin(), -1);
            kept.push_back(size + 11);
            ranking.update(kept, scores, size);
            for (std::uint32_t begin : {0U, std::uint32_t(size / 4), std::uint32_t(size)}) {
                for (std::uint32_t end : {0U, std::uint32_t(size * 3 / 4), std::uint32_t(size + 5)}) {
                    for (std::uint32_t budget : {0U, 1U, 60U, 512U, 5000U}) {
                        require(ranking.select(begin, end, budget) == oracle(kept, scores, size, begin, end, budget),
                                "cached ranking disagrees with independent membership oracle");
                        ++checks;
                    }
                }
            }
        }
    }
    // Explicit opposing tie rules: first stage keeps the NEWER middle page; ring picks OLDER.
    std::vector<float> flat(8, 0);
    auto first = kvmem_select_blocks({3, 1, 0, 8}, flat.data(), 0);
    require(first.kept == std::vector<int>({0, 6, 7}), "first-stage tie direction changed");
    ranking.update(first.kept, flat, 8);
    require(ranking.select(0, 8, 1) == std::vector<std::uint32_t>({0}), "ring tie direction changed");
    // Invalid/absent score slots must not enter the range or displace a scored page.
    ranking.update(std::vector<int>{0, 2, 7, -1, 20}, std::vector<float>{4, 8, 2}, 8);
    require(ranking.select(0, 8, 8) == std::vector<std::uint32_t>({0, 2}), "absent score handling changed");
    ranking.update({}, {}, 0);
    require(ranking.select(0, 4096, 60).empty(), "new empty snapshot retained stale ranking");
    std::printf("ranking: %zu independent oracle cases, tie/range/missing-score boundaries OK\n", checks);
}

struct Delay {
    std::atomic<bool> release{false};
    std::atomic<bool> timed_out{false};
};
void CUDART_CB delayed_append(void* opaque) {
    auto& delay = *static_cast<Delay*>(opaque);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!delay.release.load()) {
        if (std::chrono::steady_clock::now() >= deadline) { delay.timed_out = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void test_schedule() {
#ifdef _WIN32
    _putenv_s("NINFER_TERNARY_KVMEM_SCORE", "1");
#else
    setenv("NINFER_TERNARY_KVMEM_SCORE", "1", 1);
#endif
    DeviceContext device;
    auto& p = kvmem_score_probe();
    std::vector<float> scores(4096);
    for (int i = 0; i < 4096; ++i) { scores[i] = float(i % 101) / 101.0f; }
    const double mass = std::accumulate(scores.begin(), scores.end(), 0.0);
    for (float& score : scores) { score *= static_cast<float>(3.0 / mass); }
    kvmem_score_begin(4096, 4096, 17, kPagedKVPageSize, 16, 24, 4, 256, device.stream);
    cudaPointerAttributes attributes{};
    CUDA_CHECK(cudaPointerGetAttributes(&attributes, p.score_readback->host_data()));
    require(attributes.type == cudaMemoryTypeHost, "readback is not pinned Host storage");
    CUDA_CHECK(cudaMemcpyAsync(p.score, scores.data(), scores.size() * sizeof(float),
                               cudaMemcpyHostToDevice, device.stream));
    p.scored_this_chunk = true;
    p.query_span_tokens = 3;
    kvmem_score_submit_readback(device.stream);
    Delay delay;
    CUDA_CHECK(cudaLaunchHostFunc(device.stream, delayed_append, &delay));
    const auto started = std::chrono::steady_clock::now();
    kvmem_score_finish("schedule_test");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    delay.release = true;
    CUDA_CHECK(cudaStreamSynchronize(device.stream));
    require(!delay.timed_out, "score consumption waited for work AFTER its readback event");
    require(p.score_host == scores, "score publication read incomplete or wrong snapshot");
    const auto expected = kvmem_select_blocks({512, 64, 0, 4096}, scores.data(), 0);
    require(p.kept == expected.kept, "first selector changed during score publication");
    require(kvmem_score_retrieval_pages(192, 3000, 60) == oracle(p.kept, scores, 4096, 192, 3000, 60),
            "second selector changed during score publication");
    const auto inherited = p.kept;
    const auto inherited_ring = kvmem_score_retrieval_pages(192, 3000, 60);
    // Unscored chunks must preserve the previously published snapshot.
    kvmem_score_begin(4096, 3000, 17, kPagedKVPageSize, 16, 24, 4, 256, device.stream);
    kvmem_score_submit_readback(device.stream);
    kvmem_score_finish("unscored_test");
    require(p.kept == inherited && kvmem_score_retrieval_pages(192, 3000, 60) == inherited_ring,
            "unscored chunk lost inherited retrieval selection");
    // A cancelled round is drained before staging reuse, and is never published as a fresh query.
    kvmem_score_begin(4096, 3000, 17, kPagedKVPageSize, 16, 24, 4, 256, device.stream);
    p.scored_this_chunk = true;
    kvmem_score_submit_readback(device.stream);
    require(p.readback_pending, "test did not enqueue cancelled snapshot");
    kvmem_score_begin(4096, 2000, 31, kPagedKVPageSize, 16, 24, 4, 256, device.stream);
    require(!p.readback_pending && p.kept == inherited &&
            kvmem_score_retrieval_pages(192, 3000, 60) == inherited_ring,
            "cancelled snapshot contaminated published selection");
    CUDA_CHECK(cudaStreamSynchronize(device.stream));
    std::printf("schedule: pinned RAM, event-before-append, inherited/cancelled snapshots OK (consume %.3f ms)\n", ms);
}
}

int main() {
    try {
        test_ranking();
        test_schedule();
        std::puts("OK");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
