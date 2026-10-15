// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#include "core/device.h"
#include "ops/kvmem/mean_k_index.h"
#include "ops/kvmem/kvmem_retrieve.h"
#include "ops/kvmem/kvmem_retrieve_launch.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace ninfer;
using namespace ninfer::ops;

namespace {
constexpr int layers = 16, heads = 4, dim = 256, blocks = 4096;
constexpr int width = heads * dim;
constexpr std::size_t stride = static_cast<std::size_t>(blocks) * (width + 1);

void require(bool ok, const char* message) {
    if (!ok) { throw std::runtime_error(message); }
}

// Independent represented-input decode: every generated value is exactly BF16.
float decode(std::uint16_t value) { return std::bit_cast<float>(std::uint32_t(value) << 16); }
std::uint16_t encode(float value) { return static_cast<std::uint16_t>(std::bit_cast<std::uint32_t>(value) >> 16); }

std::vector<std::uint16_t> input(int tokens, int layer, int phase) {
    std::vector<std::uint16_t> out(static_cast<std::size_t>(tokens) * width);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float v = static_cast<float>((i * 17 + layer * 11 + phase * 23) % 61) / 64.0f - 0.46875f;
        out[i] = encode(v);
    }
    return out;
}

void oracle_append(std::vector<float>& expected, const std::vector<std::uint16_t>& data,
                   int tokens, int layer, int first) {
    const std::size_t base = static_cast<std::size_t>(layer) * stride;
    // Independent token-by-token FP32 oracle; no reuse of the kernel's page grouping.
    for (int t = 0; t < tokens; ++t) {
        const int page = (first + t) / 64;
        for (int cell = 0; cell < width; ++cell) {
            expected[base + static_cast<size_t>(page) * width + cell] += decode(data[t * width + cell]);
        }
        expected[base + static_cast<size_t>(blocks) * width + page] += 1.0f;
    }
}

void judge(MeanKIndex& index, const std::vector<float>& expected, cudaStream_t stream) {
    CUDA_CHECK(cudaStreamSynchronize(stream));
    std::vector<float> got(expected.size());
    for (int l = 0; l < layers; ++l) {
        CUDA_CHECK(cudaMemcpy(got.data() + l * stride, index.layer_sums(l).data,
                              stride * sizeof(float), cudaMemcpyDefault));
    }
    require(got == expected, "index differs from independent ordered-sum/count oracle");
}

std::vector<double> score_oracle(const std::vector<float>& expected, const std::vector<std::uint16_t>& query) {
    constexpr int query_heads = 24, query_tokens = 3;
    std::vector<double> score(blocks, 0), logits(blocks), masses(blocks);
    for (int l = 0; l < layers; ++l) {
        const std::size_t base = l * stride;
        for (int t = 0; t < query_tokens; ++t) {
            for (int h = 0; h < query_heads; ++h) {
                double maximum = -INFINITY;
                for (int b = 0; b < blocks; ++b) {
                    // Only represented tokens contribute to the mean or probability mass.
                    const float count = expected[base + static_cast<std::size_t>(blocks) * width + b];
                    logits[b] = -INFINITY;
                    if (count <= 0) { continue; }
                    double dot = 0;
                    for (int d = 0; d < dim; ++d) {
                        dot += decode(query[(t * query_heads + h) * dim + d]) *
                               static_cast<double>(expected[base + (static_cast<std::size_t>(b) * heads + h / 6) * dim + d]);
                    }
                    logits[b] = dot / (16.0 * count);
                    maximum = std::max(maximum, logits[b]);
                }
                double total = 0;
                for (int b = 0; b < blocks; ++b) { masses[b] = std::exp(logits[b] - maximum); total += masses[b]; }
                for (int b = 0; b < blocks; ++b) { score[b] += masses[b] / total / (layers * query_heads); }
            }
        }
    }
    return score;
}

void judge_scores(MeanKIndex& index, const std::vector<float>& expected, cudaStream_t stream,
                  bool captured = true) {
    constexpr int query_heads = 24, query_tokens = 3;
    std::vector<std::uint16_t> query(query_heads * query_tokens * dim);
    for (std::size_t i = 0; i < query.size(); ++i) { query[i] = encode(static_cast<float>(int(i * 13 % 43) - 21) / 32.0f); }
    DeviceBuffer device_query(query.size() * sizeof(std::uint16_t));
    device_query.copy_from_host(query.data(), device_query.bytes);
    DeviceBuffer result(blocks * sizeof(float)), counts(layers * blocks * sizeof(std::int32_t));
    std::vector<std::int32_t> host_counts(layers * blocks);
    for (int l = 0; l < layers; ++l) {
        for (int b = 0; b < blocks; ++b) {
            host_counts[l * blocks + b] = std::max(1, static_cast<int>(expected[l * stride + static_cast<std::size_t>(blocks) * width + b]));
        }
    }
    counts.copy_from_host(host_counts.data(), counts.bytes);
    cudaGraph_t graph; cudaGraphExec_t executable;
    if (captured) { CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal)); }
    index.begin_scoring_round(blocks, stream);
    CUDA_CHECK(cudaMemsetAsync(result.p, 0, result.bytes, stream));
    for (int l = 0; l < layers; ++l) {
        KvMemRetrieveConfig cfg;
        cfg.f32_block_counts = static_cast<const float*>(index.layer_counts(l).data);
        cfg.n_layers = 1; cfg.n_layers_total = layers;
        cfg.n_query_tokens = query_tokens; cfg.n_heads = query_heads;
        cfg.n_kv_heads = heads; cfg.head_dim = dim; cfg.n_blocks = blocks;
        cfg.q_layer_stride = query_tokens; cfg.kbar_layer_stride = blocks;
        cfg.mask_mode = KvMemRetrieveMaskMode::Never;
        cfg.dtype = KvMemRetrieveDtype::BF16;
        const Tensor& staged = index.layer_sums_for_scoring(l, blocks, stream);
        CUDA_CHECK(kvmem_retrieve_scores(cfg, static_cast<float*>(result.p), device_query.p,
                    static_cast<const float*>(staged.data),
                    static_cast<const std::int32_t*>(counts.p) + l * blocks, stream));
        index.release_scoring_layer(l, stream);
    }
    CUDA_CHECK(index.end_scoring_round(stream));
    if (captured) {
        CUDA_CHECK(cudaStreamEndCapture(stream, &graph));
        CUDA_CHECK(cudaGraphInstantiate(&executable, graph, 0));
        for (int replay = 0; replay < 3; ++replay) { CUDA_CHECK(cudaGraphLaunch(executable, stream)); }
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));
    if (captured) { CUDA_CHECK(cudaGraphExecDestroy(executable)); CUDA_CHECK(cudaGraphDestroy(graph)); }
    std::vector<float> actual(blocks);
    result.copy_to_host(actual.data(), result.bytes);
    const auto wanted = score_oracle(expected, query);
    double maximum = 0, sum = 0;
    for (int b = 0; b < blocks; ++b) {
        maximum = std::max(maximum, std::abs(actual[b] - wanted[b]));
        if (std::abs(actual[b] - wanted[b]) > 2e-5 + 2e-4 * std::abs(wanted[b])) {
            std::fprintf(stderr, "score block=%d actual=%.9g wanted=%.9g\n", b, actual[b], wanted[b]);
            require(false, "retrieval scores differ from FP64 softmax oracle");
        }
        sum += actual[b];
    }
    require(std::abs(sum - query_tokens) < 1e-4, "retrieval lost probability mass");
    std::printf("storage=%s scratch=%zu capture=%d score_max_absolute_error=%.9g mass=%.9g\n",
                index.host_backed() ? "host" : "device", index.scoring_scratch_bytes(),
                captured ? 1 : 0, maximum, sum);
}

void qualify(MeanKIndexStorage storage, bool prefetch = false) {
    cudaStream_t stream;
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    {
        MeanKIndex index(layers, heads, dim, blocks, storage, prefetch);
        require(index.bytes_total() == 268697600, "real-model capacity bytes mismatch");
        cudaPointerAttributes attributes;
        CUDA_CHECK(cudaPointerGetAttributes(&attributes, index.layer_sums(0).data));
        require(attributes.type == (storage == MeanKIndexStorage::HostMapped
                                       ? cudaMemoryTypeHost : cudaMemoryTypeDevice),
                "index is not in the requested physical storage");
        std::vector<float> expected(layers * stride, 0);
        CUDA_CHECK(index.zero(stream));
        DeviceBuffer raw(145 * width * sizeof(std::uint16_t));
        auto append = [&](int tokens, int layer, int first, int phase) {
            auto data = input(tokens, layer, phase);
            raw.copy_from_host(data.data(), data.size() * sizeof(std::uint16_t));
            Tensor tensor(raw.p, DType::BF16, {width, tokens});
            index.append_layer(tensor, layer, tokens, first, stream);
            oracle_append(expected, data, tokens, layer, first);
            CUDA_CHECK(cudaStreamSynchronize(stream));
        };
        for (int l = 0; l < layers; ++l) {
            append(145, l, 0, 0); // two full blocks and a 17-token tail
            append(47, l, 145, 1); // complete that tail, retaining earlier terms
            append(64, l, 2048 * 64, 2);
            append(19, l, (blocks - 1) * 64, 3); // last legal block, short fill
        }
        judge(index, expected, stream);
        const auto context_a = index.snapshot(stream);
        const auto expected_a = expected;
        for (int l = 0; l < layers; ++l) { append(1, l, (blocks - 1) * 64 + 19, 4); }
        const auto context_b = index.snapshot(stream);
        require(context_a->slabs.front() == context_b->slabs.front(), "unchanged history was copied");
        require(context_a->slabs.back() != context_b->slabs.back(), "mutated tail aliases checkpoint");
        const auto expected_b = expected;
        index.restore(context_a, stream); judge(index, expected_a, stream);
        // Replay an identical indexed prefix: counts and sums must remain unchanged.
        auto replay = input(145, 0, 0); raw.copy_from_host(replay.data(), replay.size() * 2);
        index.append_layer(Tensor(raw.p, DType::BF16, {width, 145}), 0, 145, 0, stream);
        judge(index, expected_a, stream);
        CUDA_CHECK(index.zero(stream));
        std::fill(expected.begin(), expected.end(), 0.0f);
        // An unrelated short context must have exactly two pages, with an unaligned cross-page append.
        for (int l = 0; l < layers; ++l) { append(63, l, 0, 7); append(2, l, 63, 8); }
        require(index.blocks_written() == 2 && index.tail_fill() == 1, "foreign history survived context reset");
        judge(index, expected, stream);
        judge_scores(index, expected, stream);
        index.restore(context_b, stream); expected = expected_b; judge(index, expected, stream);
        index.restore(context_a, stream); expected = expected_a; judge(index, expected, stream);
        auto data = input(1, 0, 9); raw.copy_from_host(data.data(), data.size() * 2);
        Tensor one(raw.p, DType::BF16, {width, 1});
        bool rejected = false;
        try { index.append_layer(one, 0, 1, blocks * 64, stream); } catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "out-of-capacity write accepted");
        judge_scores(index, expected, stream);
        if (prefetch) { judge_scores(index, expected, stream, false); }
        CUDA_CHECK(index.zero(stream));
        std::fill(expected.begin(), expected.end(), 0.0f);
        judge(index, expected, stream);
        require(index.blocks_written() == 0 && index.tail_fill() == 0, "reset kept old cursor");
        append(65, 7, 0, 5);
        judge(index, expected, stream);
        std::printf("storage=%s PASS: full capacity, absolute/partial append, COW snapshots, replay, sparse counts, bounds and reuse\n", index.host_backed() ? "host" : "device");
    }
    CUDA_CHECK(cudaStreamDestroy(stream));
}

void qualify_program_ownership() {
#ifdef _WIN32
    _putenv_s("NINFER_TERNARY_KVMEM", "1");
    _putenv_s("NINFER_KVMEM_INDEX_HOST", "1");
#else
    setenv("NINFER_TERNARY_KVMEM", "1", 1);
    setenv("NINFER_KVMEM_INDEX_HOST", "1", 1);
#endif
    std::unique_ptr<MeanKIndex> a, b;
    std::unique_ptr<RawKShadowHarvest> harvest_a, harvest_b;
    mean_k_index_bind(&a); raw_k_shadow_harvest_bind(&harvest_a);
    auto* index_a = mean_k_index_for(2, heads, dim, 16);
    auto* staging_a = raw_k_shadow_harvest_for(128, width, 2);
    auto data = input(63, 0, 5);
    DeviceBuffer raw(data.size() * 2);
    raw.copy_from_host(data.data(), data.size() * 2);
    index_a->append_layer(Tensor(raw.p, DType::BF16, {width, 63}), 0, 63, 0, nullptr);
    mean_k_index_bind(&b); raw_k_shadow_harvest_bind(&harvest_b);
    auto* index_b = mean_k_index_for(2, heads, dim, 16);
    auto* staging_b = raw_k_shadow_harvest_for(128, width, 2);
    require(index_a != index_b && staging_a != staging_b, "Programs share mutable index/staging");
    index_b->append_layer(Tensor(raw.p, DType::BF16, {width, 2}), 0, 2, 0, nullptr);
    CUDA_CHECK(cudaDeviceSynchronize());
    float count_a = 0, count_b = 0;
    CUDA_CHECK(cudaMemcpy(&count_a, index_a->layer_counts(0).data, 4, cudaMemcpyDefault));
    CUDA_CHECK(cudaMemcpy(&count_b, index_b->layer_counts(0).data, 4, cudaMemcpyDefault));
    require(count_a == 63 && count_b == 2, "another Program changed the first context's counts");
    mean_k_index_bind(&a); raw_k_shadow_harvest_bind(&harvest_a);
    require(mean_k_index_for(2, heads, dim, 16) == index_a &&
            raw_k_shadow_harvest_for(128, width, 2) == staging_a,
            "switching Programs lost the original workspace");
    mean_k_index_bind(nullptr); raw_k_shadow_harvest_bind(nullptr);
    std::puts("PASS: Program workspace isolation and context switching");
}
} // namespace

int main() {
    try {
        qualify(MeanKIndexStorage::Device);
        qualify(MeanKIndexStorage::HostMapped);
        qualify(MeanKIndexStorage::HostMapped, true);
        qualify_program_ownership();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
