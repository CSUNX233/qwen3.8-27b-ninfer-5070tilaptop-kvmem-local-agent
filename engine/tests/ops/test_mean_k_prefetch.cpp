// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#include "core/device.h"
#include "core/mapped_host_buffer.h"
#include "ops/kvmem/mean_k_index.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ninfer;
using namespace ninfer::ops;

namespace {
constexpr int layers = 16, heads = 4, dim = 256, capacity = 4096;
constexpr std::size_t width = heads * dim;
void require(bool ok, const char* message) {
    if (!ok) { throw std::runtime_error(message); }
}

struct Gate { std::atomic<bool> open{false}; };
void CUDART_CB gated_consumer(void* data) {
    auto& gate = *static_cast<Gate*>(data);
    while (!gate.open.load(std::memory_order_acquire)) { std::this_thread::yield(); }
}

// Exact represented-input oracle: every cell includes the source layer and address. Checking all
// bytes detects wrong versions, wrong slots, short copies and early scratch overwrites directly.
float value(int layer, std::size_t cell, int version) {
    return static_cast<float>((cell * 17 + layer * 271 + version * 4093) % 65521) / 64.0f;
}
void fill(MeanKIndex& index, int version) {
    for (int l = 0; l < layers; ++l) {
        cudaPointerAttributes attributes{};
        CUDA_CHECK(cudaPointerGetAttributes(&attributes, index.layer_sums(l).data));
        auto* sums = static_cast<float*>(attributes.hostPointer);
        require(sums != nullptr, "test index must be physically host-backed");
        for (std::size_t i = 0; i < capacity * width; ++i) { sums[i] = value(l, i, version); }
    }
}

void check_pass(MeanKIndex& index, cudaStream_t stream, int blocks, int version,
                const std::vector<int>& order, bool hold_compute) {
    const std::size_t bytes = static_cast<std::size_t>(blocks) * width * sizeof(float);
    MappedHostBuffer output(bytes * order.size());
    Gate gate;
    index.begin_scoring_round(blocks, stream);
    try {
        for (std::size_t step = 0; step < order.size(); ++step) {
            const auto& sums = index.layer_sums_for_scoring(order[step], blocks, stream);
            if (hold_compute && step == 0) {
                CUDA_CHECK(cudaLaunchHostFunc(stream, gated_consumer, &gate));
            }
            CUDA_CHECK(cudaMemcpyAsync(static_cast<char*>(output.host_data()) + bytes * step,
                                       sums.data, bytes, cudaMemcpyDeviceToHost, stream));
            index.release_scoring_layer(order[step], stream);
        }
        CUDA_CHECK(index.end_scoring_round(stream));
    } catch (...) {
        gate.open.store(true, std::memory_order_release);
        (void)cudaStreamSynchronize(stream);
        throw;
    }
    gate.open.store(true, std::memory_order_release);
    CUDA_CHECK(cudaStreamSynchronize(stream));
    const auto* actual = static_cast<const float*>(output.host_data());
    for (std::size_t step = 0; step < order.size(); ++step) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(blocks) * width; ++i) {
            require(actual[step * blocks * width + i] == value(order[step], i, version),
                    "staged index differs from immutable source oracle");
        }
    }
}

void qualify() {
    cudaStream_t stream;
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    {
        MeanKIndex index(layers, heads, dim, capacity, MeanKIndexStorage::HostMapped, true);
        require(index.scoring_scratch_bytes() == 32 * 1024 * 1024, "double scratch budget changed");
        CUDA_CHECK(index.zero(stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));
        std::vector<int> sequential;
        for (int l = 0; l < layers; ++l) { sequential.push_back(l); }
        for (int version = 0; version < 3; ++version) {
            fill(index, version);
            for (int blocks : {1, 3, 513, 2049, 4096}) {
                check_pass(index, stream, blocks, version, sequential, true);
            }
            check_pass(index, stream, capacity, version, {0, 0, 3, 8, 9, 14, 15}, true);
            // Abandon after a view was handed out, including its speculative next-layer copy.
            index.begin_scoring_round(capacity, stream);
            (void)index.layer_sums_for_scoring(0, capacity, stream);
            bool rejected = false;
            try { (void)index.layer_sums_for_scoring(1, capacity, stream); }
            catch (const std::logic_error&) { rejected = true; }
            require(rejected, "scratch reuse without a consumer fence was accepted");
            // Begin closes an abandoned round; append/reset also join speculative reads.
            check_pass(index, stream, 513, version, sequential, false);
            index.begin_scoring_round(capacity, stream);
            (void)index.layer_sums_for_scoring(7, capacity, stream);
            CUDA_CHECK(index.zero(stream));
            CUDA_CHECK(cudaStreamSynchronize(stream));
            std::vector<float> zero(capacity * width);
            for (int l = 0; l < layers; ++l) {
                CUDA_CHECK(cudaMemcpy(zero.data(), index.layer_sums(l).data,
                                      zero.size() * sizeof(float), cudaMemcpyDefault));
                for (float cell : zero) { require(cell == 0.0f, "reset raced a speculative read"); }
            }
        }
        fill(index, 7);
        // Captured callers retain the original ordered upload route, including replay.
        MappedHostBuffer output(3 * width * layers * sizeof(float));
        cudaGraph_t graph; cudaGraphExec_t executable;
        CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
        index.begin_scoring_round(3, stream);
        for (int l = 0; l < layers; ++l) {
            const auto& view = index.layer_sums_for_scoring(l, 3, stream);
            CUDA_CHECK(cudaMemcpyAsync(static_cast<float*>(output.host_data()) + l * 3 * width,
                                       view.data, 3 * width * sizeof(float), cudaMemcpyDeviceToHost, stream));
            index.release_scoring_layer(l, stream);
        }
        CUDA_CHECK(index.end_scoring_round(stream));
        CUDA_CHECK(cudaStreamEndCapture(stream, &graph));
        CUDA_CHECK(cudaGraphInstantiate(&executable, graph, 0));
        for (int replay = 0; replay < 3; ++replay) { CUDA_CHECK(cudaGraphLaunch(executable, stream)); }
        CUDA_CHECK(cudaStreamSynchronize(stream));
        auto* actual = static_cast<float*>(output.host_data());
        for (int l = 0; l < layers; ++l) {
            for (std::size_t i = 0; i < 3 * width; ++i) {
                require(actual[l * 3 * width + i] == value(l, i, 7), "capture fallback changed data");
            }
        }
        CUDA_CHECK(cudaGraphExecDestroy(executable)); CUDA_CHECK(cudaGraphDestroy(graph));
        // Destruction while an asynchronous pass still has an unreleased view.
        index.begin_scoring_round(capacity, stream);
        (void)index.layer_sums_for_scoring(0, capacity, stream);
    }
    CUDA_CHECK(cudaStreamDestroy(stream));
    std::puts("PASS: exact full-capacity bytes, gated consumers, slot reuse, history versions, cancellation, reset, capture replay, destruction");
}
} // namespace

int main() {
    try { qualify(); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
