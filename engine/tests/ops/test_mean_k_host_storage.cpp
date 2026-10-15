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
    for (int b = 0; b < (tokens + 63) / 64; ++b) {
        const int n = std::min(64, tokens - b * 64);
        for (int cell = 0; cell < width; ++cell) {
            double sum = 0;
            for (int t = 0; t < n; ++t) { sum += decode(data[(b * 64 + t) * width + cell]); }
            expected[base + static_cast<std::size_t>(first + b) * width + cell] += static_cast<float>(sum);
        }
        expected[base + static_cast<std::size_t>(blocks) * width + first + b] += static_cast<float>(n);
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
                    // The scorer consumes separately supplied positive page fills.
                    // Untouched zero-sum pages are represented here as one-token
                    // zero vectors, so all 4096 page addresses participate.
                    const float count = std::max(1.0f, expected[base + static_cast<std::size_t>(blocks) * width + b]);
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
            append(47, l, 2, 1); // complete that tail, retaining earlier terms
            append(64, l, 2048, 2);
            append(19, l, blocks - 1, 3); // last legal block, short fill
        }
        judge(index, expected, stream);
        auto data = input(1, layers - 1, 4);
        raw.copy_from_host(data.data(), data.size() * sizeof(std::uint16_t));
        Tensor one(raw.p, DType::BF16, {width, 1});
        cudaGraph_t graph; cudaGraphExec_t executable;
        CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
        index.append_layer(one, layers - 1, 1, 2, stream);
        CUDA_CHECK(cudaStreamEndCapture(stream, &graph));
        CUDA_CHECK(cudaGraphInstantiate(&executable, graph, 0));
        for (int replay = 0; replay < 3; ++replay) {
            CUDA_CHECK(cudaGraphLaunch(executable, stream));
            oracle_append(expected, data, 1, layers - 1, 2);
        }
        judge(index, expected, stream);
        CUDA_CHECK(cudaGraphExecDestroy(executable)); CUDA_CHECK(cudaGraphDestroy(graph));
        bool rejected = false;
        try { index.append_layer(one, 0, 1, blocks, stream); } catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "out-of-capacity write accepted");
        judge_scores(index, expected, stream);
        if (prefetch) { judge_scores(index, expected, stream, false); }
        CUDA_CHECK(index.zero(stream));
        std::fill(expected.begin(), expected.end(), 0.0f);
        judge(index, expected, stream);
        require(index.blocks_written() == 0 && index.tail_fill() == 0, "reset kept old cursor");
        append(65, 7, 0, 5);
        judge(index, expected, stream);
        std::printf("storage=%s PASS: full capacity, partial append, graph replay, bounds, reset and reuse\n", index.host_backed() ? "host" : "device");
    }
    CUDA_CHECK(cudaStreamDestroy(stream));
}
} // namespace

int main() {
    try {
        qualify(MeanKIndexStorage::Device);
        qualify(MeanKIndexStorage::HostMapped);
        qualify(MeanKIndexStorage::HostMapped, true);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
