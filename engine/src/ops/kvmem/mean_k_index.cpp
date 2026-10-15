// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#include "ops/kvmem/mean_k_index.h"

#include "core/device.h"
#include "core/paged_kv_cache.h"
// For kvmem_shadow_enabled(). Included explicitly rather than relied on transitively through
// raw_k_harvest.h -> raw_k_shadow.h -> kvmem_shadow.h: the switch is read in THIS file, and a
// transitive include that happens to hold today is not a contract (that reliance failed to compile
// the first time this function was added).
#include "ops/kvmem/kvmem_shadow.h"
#include "ops/kvmem/mean_k_index_launch.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// KVMem mean-K index, host side. See mean_k_index.h for what it is, why it must be built
// incrementally, and why the stored medium is an F32 sum plus a token count rather than a mean.

namespace ninfer::ops {
namespace {

constexpr const char* kSubject = "mean_k_index";

// The logical block IS one KV page, so the block size is the page size: a block has to stay
// identifiable while KVMem moves the pages that hold it. Reading the constant from the cache header
// rather than repeating "64" is the difference between a contract and a coincidence. (It lives in
// namespace ninfer, not ninfer::ops.)
constexpr std::int32_t kBlockTokens = ninfer::kPagedKVPageSize;

void require_layout(const Tensor& raw_key, std::int32_t kv_size) {
    if (raw_key.data == nullptr || raw_key.dtype != DType::BF16) {
        throw std::invalid_argument(std::string(kSubject) + ": raw key must be a BF16 tensor");
    }
    if (raw_key.ne[0] != kv_size || raw_key.ne[1] <= 0 || raw_key.ne[2] != 1 || raw_key.ne[3] != 1) {
        throw std::invalid_argument(std::string(kSubject) + ": raw key must be {kv_size=" +
                                    std::to_string(kv_size) + ", width>0}, got {" +
                                    std::to_string(raw_key.ne[0]) + ", " +
                                    std::to_string(raw_key.ne[1]) + ", " +
                                    std::to_string(raw_key.ne[2]) + ", " +
                                    std::to_string(raw_key.ne[3]) + "}");
    }
}

std::size_t checked_bytes(std::int32_t layers, std::int32_t blocks, std::int32_t heads,
                          std::int32_t head_dim) {
    if (layers <= 0 || static_cast<std::size_t>(layers) > kMeanKMaxLayers) {
        throw std::invalid_argument(std::string(kSubject) + ": layer count out of range");
    }
    const std::size_t per_layer = mean_k_index_bytes(1, blocks, heads, head_dim);
    if (per_layer == 0) { return 0; }
    return per_layer * static_cast<std::size_t>(layers);
}

} // namespace

struct MeanKIndex::ScoringState {
    struct Timing {
        cudaEvent_t copy_begin = nullptr, copy_end = nullptr;
        cudaEvent_t wait_begin = nullptr, wait_end = nullptr;
        bool recorded = false;
    };
    std::unique_ptr<DeviceBuffer> second_storage;
    Tensor second_view;
    cudaStream_t transfer = nullptr, compute = nullptr;
    cudaEvent_t producer = nullptr, drained = nullptr;
    std::array<cudaEvent_t, 2> ready{}, used{};
    std::array<bool, 2> used_valid{};
    std::array<std::int32_t, 2> staged_layer{-1, -1};
    std::array<Timing, kMeanKMaxLayers> timing{};
    bool profile = false, timing_pass = false, active = false, async = false;
    std::int32_t blocks = 0, held_layer = -1;

    void initialize(bool prefetch, std::size_t bytes, std::int32_t capacity,
                    std::int32_t heads, std::int32_t dim) {
        const char* setting = std::getenv("NINFER_KVMEM_INDEX_TIMING");
        profile = setting != nullptr && std::strcmp(setting, "1") == 0;
        if (prefetch) {
            second_storage = std::make_unique<DeviceBuffer>(bytes);
            second_view = Tensor(second_storage->p, DType::FP32, {capacity, heads, dim});
            CUDA_CHECK(cudaStreamCreateWithFlags(&transfer, cudaStreamNonBlocking));
            CUDA_CHECK(cudaEventCreateWithFlags(&producer, cudaEventDisableTiming));
            CUDA_CHECK(cudaEventCreateWithFlags(&drained, cudaEventDisableTiming));
            for (int s = 0; s < 2; ++s) {
                CUDA_CHECK(cudaEventCreateWithFlags(&ready[s], cudaEventDisableTiming));
                CUDA_CHECK(cudaEventCreateWithFlags(&used[s], cudaEventDisableTiming));
            }
        }
        if (profile) {
            for (auto& t : timing) {
                CUDA_CHECK(cudaEventCreate(&t.copy_begin));
                CUDA_CHECK(cudaEventCreate(&t.copy_end));
                CUDA_CHECK(cudaEventCreate(&t.wait_begin));
                CUDA_CHECK(cudaEventCreate(&t.wait_end));
            }
        }
    }

    // Diagnostic only. Normal inference has no timing events, queries, or logging here.
    // At the normal prefill tail the existing compute drain already completed these events.
    void report() noexcept {
        if (!timing_pass) { return; }
        double copies = 0, exposed = 0;
        int measured = 0;
        for (auto& t : timing) {
            if (!t.recorded || cudaEventQuery(t.wait_end) != cudaSuccess) { continue; }
            float copy = 0, wait = 0;
            if (cudaEventElapsedTime(&copy, t.copy_begin, t.copy_end) != cudaSuccess ||
                cudaEventElapsedTime(&wait, t.wait_begin, t.wait_end) != cudaSuccess) { continue; }
            copies += copy; exposed += wait; ++measured;
        }
        if (measured > 0) {
            std::fprintf(stderr, "kvmem_index_timing: mode=%s blocks=%d layers=%d copy_ms=%.6f exposed_ms=%.6f\n",
                         async ? "prefetch" : "serial", blocks, measured, copies, exposed);
        }
    }

    ~ScoringState() {
        if (transfer != nullptr) { (void)cudaStreamSynchronize(transfer); }
        for (int s = 0; s < 2; ++s) {
            if (used_valid[s]) { (void)cudaEventSynchronize(used[s]); }
            if (ready[s] != nullptr) { (void)cudaEventDestroy(ready[s]); }
            if (used[s] != nullptr) { (void)cudaEventDestroy(used[s]); }
        }
        for (const auto& t : timing) {
            for (auto event : {t.copy_begin, t.copy_end, t.wait_begin, t.wait_end}) {
                if (event != nullptr) { (void)cudaEventDestroy(event); }
            }
        }
        if (producer != nullptr) { (void)cudaEventDestroy(producer); }
        if (drained != nullptr) { (void)cudaEventDestroy(drained); }
        if (transfer != nullptr) { (void)cudaStreamDestroy(transfer); }
    }
};

MeanKIndex::MeanKIndex(std::int32_t layers, std::int32_t heads, std::int32_t head_dim,
                       std::int32_t capacity_blocks, MeanKIndexStorage storage,
                       bool prefetch_scoring)
    : MeanKIndex(layers, heads, head_dim, capacity_blocks,
                 storage == MeanKIndexStorage::HostMapped &&
                         checked_bytes(layers, capacity_blocks, heads, head_dim) != 0
                     ? std::make_unique<MappedHostBuffer>(
                           checked_bytes(layers, capacity_blocks, heads, head_dim))
                     : nullptr, prefetch_scoring) {}

MeanKIndex::MeanKIndex(std::int32_t layers, std::int32_t heads, std::int32_t head_dim,
                       std::int32_t capacity_blocks,
                       std::unique_ptr<MappedHostBuffer> host_storage, bool prefetch_scoring)
    : layers_(layers),
      heads_(heads),
      head_dim_(head_dim),
      capacity_blocks_(capacity_blocks),
      sums_per_layer_(static_cast<std::size_t>(capacity_blocks > 0 ? capacity_blocks : 0) *
                      static_cast<std::size_t>(heads > 0 ? heads : 0) *
                      static_cast<std::size_t>(head_dim > 0 ? head_dim : 0)),
      elements_per_layer_(sums_per_layer_ +
                          static_cast<std::size_t>(capacity_blocks > 0 ? capacity_blocks : 0)),
      per_layer_bytes_(mean_k_index_bytes(1, capacity_blocks, heads, head_dim)),
      total_bytes_(checked_bytes(layers, capacity_blocks, heads, head_dim)),
      arena_(host_storage != nullptr
                 ? DeviceArena(DeviceSpan{host_storage->device_data(), total_bytes_})
                 : DeviceArena(std::max<std::size_t>(256, total_bytes_))),
      host_storage_(std::move(host_storage)) {
    if (total_bytes_ == 0) {
        // A zero-capacity index is a supported state, not an error: it is what a caller that chose
        // not to reserve index memory gets, and empty() tells it so.
        return;
    }
    // ONE allocation, sliced per layer, so the arena cannot insert alignment padding BETWEEN layers
    // and make the per-layer stride disagree with per_layer_bytes() -- the same reasoning (and the
    // same machine check) as RawKShadowHarvest's constructor. The sums and the counts of a layer live
    // in that same region ([sums][counts]) rather than in a second allocation, so the count offset the
    // kernel derives cannot drift from the view built here.
    flat_ = arena_.alloc(DType::FP32, {static_cast<std::int32_t>(elements_per_layer_), layers_});
    if (flat_.bytes() != total_bytes_) {
        throw std::logic_error(std::string(kSubject) +
                               ": arena allocation does not match the declared index size");
    }
    for (std::int32_t layer = 0; layer < layers_; ++layer) {
        const Tensor region = flat_.slice(1, layer, 1);  // F32 {elements_per_layer, 1}
        views_[static_cast<std::size_t>(layer)] =
            region.slice(0, 0, static_cast<std::int32_t>(sums_per_layer_))
                .view({capacity_blocks_, heads_, head_dim_});
        counts_[static_cast<std::size_t>(layer)] =
            region.slice(0, static_cast<std::int32_t>(sums_per_layer_), capacity_blocks_);
    }
    if (host_storage_ != nullptr) {
        score_storage_ = std::make_unique<DeviceBuffer>(sums_per_layer_ * sizeof(float));
        score_view_ = Tensor(score_storage_->p, DType::FP32,
                             {capacity_blocks_, heads_, head_dim_});
        scoring_ = std::make_unique<ScoringState>();
        scoring_->initialize(prefetch_scoring, sums_per_layer_ * sizeof(float),
                             capacity_blocks_, heads_, head_dim_);
    }
}

MeanKIndex::~MeanKIndex() {
    if (scoring_ != nullptr && scoring_->active) {
        (void)end_scoring_round(scoring_->compute);
    }
    // Destroy stream/event owners before the original scratch and mapped source are freed.
    scoring_.reset();
}

std::size_t MeanKIndex::scoring_scratch_bytes() const noexcept {
    return score_storage_ == nullptr ? 0 : sums_per_layer_ * sizeof(float) *
        (scoring_ != nullptr && scoring_->second_storage != nullptr ? 2 : 1);
}

void MeanKIndex::begin_scoring_round(std::int32_t populated_blocks, cudaStream_t stream) {
    if (populated_blocks < 0 || populated_blocks > capacity_blocks_) {
        throw std::out_of_range("mean_k_index: invalid scoring history size");
    }
    if (scoring_ == nullptr) { return; }
    CUDA_CHECK(end_scoring_round(stream)); // also drains an abandoned pass before reuse
    auto& s = *scoring_;
    cudaStreamCaptureStatus capture;
    CUDA_CHECK(cudaStreamIsCapturing(stream, &capture));
    s.compute = stream;
    s.blocks = populated_blocks;
    s.held_layer = -1;
    s.staged_layer = {-1, -1};
    for (auto& t : s.timing) { t.recorded = false; }
    s.async = s.transfer != nullptr && capture == cudaStreamCaptureStatusNone;
    s.timing_pass = s.profile && capture == cudaStreamCaptureStatusNone;
    // Timing events are deliberately absent from capture, including on the serial fallback.
    s.active = true;
    if (s.async) {
        CUDA_CHECK(cudaEventRecord(s.producer, stream));
        CUDA_CHECK(cudaStreamWaitEvent(s.transfer, s.producer, 0));
    }
}

void MeanKIndex::release_scoring_layer(std::int32_t layer_index, cudaStream_t stream) {
    if (scoring_ == nullptr || !scoring_->active || !scoring_->async) { return; }
    auto& s = *scoring_;
    if (stream != s.compute || layer_index != s.held_layer) {
        throw std::logic_error("mean_k_index: scoring release does not match its consumer");
    }
    const int slot = layer_index % 2;
    CUDA_CHECK(cudaEventRecord(s.used[slot], stream));
    s.used_valid[slot] = true;
    s.held_layer = -1;
}

cudaError_t MeanKIndex::end_scoring_round(cudaStream_t stream) noexcept {
    if (scoring_ == nullptr || !scoring_->active) { return cudaSuccess; }
    auto& s = *scoring_;
    if (stream != s.compute) { return cudaErrorInvalidResourceHandle; }
    if (s.async) {
        // A cancelled pass may not have released the last handed-out view. Fence all work already
        // submitted by its owner, then join every speculative source read before index mutation.
        if (s.held_layer >= 0) {
            const int slot = s.held_layer % 2;
            const cudaError_t st = cudaEventRecord(s.used[slot], stream);
            if (st != cudaSuccess) { return st; }
            s.used_valid[slot] = true;
            s.held_layer = -1;
        }
        cudaError_t st = cudaEventRecord(s.drained, s.transfer);
        if (st != cudaSuccess) { return st; }
        st = cudaStreamWaitEvent(stream, s.drained, 0);
        if (st != cudaSuccess) { return st; }
    }
    s.report();
    s.active = false;
    return cudaSuccess;
}

const Tensor& MeanKIndex::layer_sums(std::int32_t layer_index) const noexcept {
    static const Tensor kInvalid{};
    if (layer_index < 0 || layer_index >= layers_ || flat_.data == nullptr) { return kInvalid; }
    return views_[static_cast<std::size_t>(layer_index)];
}

const Tensor& MeanKIndex::layer_counts(std::int32_t layer_index) const noexcept {
    static const Tensor kInvalid{};
    if (layer_index < 0 || layer_index >= layers_ || flat_.data == nullptr) { return kInvalid; }
    return counts_[static_cast<std::size_t>(layer_index)];
}

const Tensor& MeanKIndex::layer_sums_for_scoring(std::int32_t layer_index,
                                               std::int32_t populated_blocks,
                                               cudaStream_t stream) {
    if (layer_index < 0 || layer_index >= layers_ || populated_blocks < 0 ||
        populated_blocks > capacity_blocks_) {
        throw std::out_of_range("mean_k_index: invalid scoring layer or block count");
    }
    if (host_storage_ == nullptr || empty()) { return layer_sums(layer_index); }
    const std::size_t count = static_cast<std::size_t>(populated_blocks) * heads_ * head_dim_ * sizeof(float);
    auto& s = *scoring_;
    if (s.active && (stream != s.compute || populated_blocks != s.blocks)) {
        throw std::logic_error("mean_k_index: scoring pass changed stream or history version");
    }
    if (s.active && s.async) {
        if (s.held_layer >= 0) {
            throw std::logic_error("mean_k_index: scoring view must be released before reuse");
        }
        const auto stage = [&](std::int32_t layer) {
            const int slot = layer % 2;
            if (s.staged_layer[slot] == layer) { return; }
            if (s.used_valid[slot]) { CUDA_CHECK(cudaStreamWaitEvent(s.transfer, s.used[slot], 0)); }
            auto& t = s.timing[static_cast<std::size_t>(layer)];
            if (s.timing_pass) { CUDA_CHECK(cudaEventRecord(t.copy_begin, s.transfer)); }
            void* target = slot == 0 ? score_storage_->p : s.second_storage->p;
            CUDA_CHECK(host_storage_->copy_to_device_async(target, count,
                        static_cast<std::size_t>(layer) * per_layer_bytes_, s.transfer));
            if (s.timing_pass) { CUDA_CHECK(cudaEventRecord(t.copy_end, s.transfer)); }
            CUDA_CHECK(cudaEventRecord(s.ready[slot], s.transfer));
            s.staged_layer[slot] = layer;
        };
        stage(layer_index);
        const int slot = layer_index % 2;
        auto& t = s.timing[static_cast<std::size_t>(layer_index)];
        if (s.timing_pass) { CUDA_CHECK(cudaEventRecord(t.wait_begin, stream)); }
        CUDA_CHECK(cudaStreamWaitEvent(stream, s.ready[slot], 0));
        if (s.timing_pass) { CUDA_CHECK(cudaEventRecord(t.wait_end, stream)); t.recorded = true; }
        // Next-layer data is independent of the current query/scorer. Its slot is reusable only
        // after the previous consumer's event, never just after its copy completed.
        if (layer_index + 1 < layers_) { stage(layer_index + 1); }
        s.held_layer = layer_index;
        return slot == 0 ? score_view_ : s.second_view;
    }
    const bool timed = s.active && s.timing_pass && s.transfer == nullptr;
    auto& t = s.timing[static_cast<std::size_t>(layer_index)];
    if (timed) {
        CUDA_CHECK(cudaEventRecord(t.wait_begin, stream));
        CUDA_CHECK(cudaEventRecord(t.copy_begin, stream));
    }
    CUDA_CHECK(host_storage_->copy_to_device_async(score_storage_->p, count,
                static_cast<std::size_t>(layer_index) * per_layer_bytes_, stream));
    if (timed) {
        CUDA_CHECK(cudaEventRecord(t.copy_end, stream));
        CUDA_CHECK(cudaEventRecord(t.wait_end, stream));
        t.recorded = true;
    }
    return score_view_;
}

cudaError_t MeanKIndex::zero(cudaStream_t stream) {
    if (total_bytes_ == 0 || flat_.data == nullptr) { return cudaSuccess; }
    const cudaError_t join = end_scoring_round(stream);
    if (join != cudaSuccess) { return join; }
    // Not folded into append on purpose. An append that zeroed its own fresh blocks would be replayed
    // verbatim inside a captured decode graph, and a replayed memset would wipe the sums the prefill
    // accumulated -- the accumulation is state that outlives the graph. Zeroing is therefore an
    // explicit setup step on a stream the caller controls, and the append stays a single launch.
    const cudaError_t st =
        cudaMemsetAsync(flat_.data, 0, total_bytes_, stream);
    if (st != cudaSuccess) { return st; }
    blocks_written_ = 0;
    tail_fill_      = 0;
    layer_ends_.assign(layers_, 0);
    dirty_slabs_.assign((capacity_blocks_ + MeanKIndexSnapshot::slab_blocks - 1) /
                         MeanKIndexSnapshot::slab_blocks, true);
    snapshot_.reset();
    return cudaSuccess;
}

void MeanKIndex::append_round(const RawKShadowHarvest& store, std::int32_t token_base,
                              cudaStream_t stream) {
    if (total_bytes_ == 0 || store.layers() != layers_) { return; }
    for (int layer = 0; layer < layers_; ++layer) {
        const Tensor view = store.staging_for_round(layer);
        append_one(view.data, layer, view.ne[1], token_base, store.tokens(),
                   heads_ * head_dim_ * store.tokens(), stream);
    }
}

void MeanKIndex::append_layer(const Tensor& raw_key, std::int32_t layer_index,
                              std::int32_t round_width, std::int32_t token_base,
                              cudaStream_t stream) {
    if (total_bytes_ == 0) { return; }
    if (layer_index < 0 || layer_index >= layers_) {
        throw std::invalid_argument("mean-K layer outside index");
    }
    require_layout(raw_key, heads_ * head_dim_);
    append_one(raw_key.data, layer_index, round_width, token_base, round_width,
               heads_ * head_dim_ * round_width, stream);
}

void MeanKIndex::append_one(const void* raw_key_bf16, std::int32_t layer_index,
                            std::int32_t round_width, std::int32_t token_base,
                            std::int32_t source_layer_stride, std::int32_t index_layer_stride,
                            cudaStream_t stream) {
    if (round_width <= 0 || raw_key_bf16 == nullptr) { return; }
    cudaStreamCaptureStatus capture;
    CUDA_CHECK(cudaStreamIsCapturing(stream, &capture));
    if (capture != cudaStreamCaptureStatusNone) {
        throw std::logic_error("mean-K absolute append must run outside graph capture");
    }
    if (token_base < 0 || static_cast<int64_t>(token_base) + round_width >
                          static_cast<int64_t>(capacity_blocks_) * kBlockTokens) {
        throw std::out_of_range("mean-K token append exceeds reserved capacity");
    }
    if (layer_ends_.empty()) { layer_ends_.assign(layers_, 0); }
    // A cached suffix may replay an already indexed tail. Its token identity is guaranteed by
    // the Program checkpoint, so consume only the new interval, once, per layer.
    const int end = token_base + round_width;
    const int begin = std::max(token_base, layer_ends_[layer_index]);
    if (begin >= end) { return; }
    CUDA_CHECK(end_scoring_round(stream));
    const auto* input = static_cast<const unsigned short*>(raw_key_bf16) +
                        static_cast<size_t>(begin - token_base) * heads_ * head_dim_;
    (void)index_layer_stride;
    CUDA_CHECK(detail::launch_mean_k_accumulate_tokens(input, flat_.data, layer_index,
        begin, end - begin, heads_, head_dim_, source_layer_stride, capacity_blocks_,
        static_cast<int>(elements_per_layer_), stream));
    layer_ends_[layer_index] = end;
    blocks_written_ = std::max(blocks_written_, (end + kBlockTokens - 1) / kBlockTokens);
    tail_fill_ = end % kBlockTokens;
    const int slab_count = (capacity_blocks_ + MeanKIndexSnapshot::slab_blocks - 1) /
                           MeanKIndexSnapshot::slab_blocks;
    if (dirty_slabs_.empty()) { dirty_slabs_.assign(slab_count, true); }
    for (int b = begin / kBlockTokens; b <= (end - 1) / kBlockTokens; ++b) {
        dirty_slabs_[b / MeanKIndexSnapshot::slab_blocks] = true;
    }
}

std::shared_ptr<const MeanKIndexSnapshot> MeanKIndex::snapshot(cudaStream_t stream) {
    CUDA_CHECK(end_scoring_round(stream));
    CUDA_CHECK(cudaStreamSynchronize(stream)); // freeze writers before reading mapped RAM
    auto out = std::make_shared<MeanKIndexSnapshot>();
    out->layers = layers_; out->heads = heads_; out->head_dim = head_dim_;
    out->capacity_blocks = capacity_blocks_;
    out->blocks_written = blocks_written_; out->tail_fill = tail_fill_;
    out->layer_ends = layer_ends_;
    const int slabs = (blocks_written_ + MeanKIndexSnapshot::slab_blocks - 1) /
                       MeanKIndexSnapshot::slab_blocks;
    out->slabs.resize(slabs);
    const size_t row = static_cast<size_t>(heads_) * head_dim_;
    for (int slab = 0; slab < slabs; ++slab) {
        if (snapshot_ && slab < static_cast<int>(snapshot_->slabs.size()) &&
            !dirty_slabs_[slab]) {
            out->slabs[slab] = snapshot_->slabs[slab];
            continue;
        }
        const int first = slab * MeanKIndexSnapshot::slab_blocks;
        const int n = std::min(MeanKIndexSnapshot::slab_blocks, capacity_blocks_ - first);
        auto data = std::make_shared<std::vector<float>>(static_cast<size_t>(layers_) * n * (row + 1));
        for (int layer = 0; layer < layers_; ++layer) {
            float* dst = data->data() + static_cast<size_t>(layer) * n * (row + 1);
            const float* src = host_storage_ ? static_cast<const float*>(host_storage_->host_data())
                                            : static_cast<const float*>(flat_.data);
            src += static_cast<size_t>(layer) * elements_per_layer_;
            if (host_storage_) {
                std::memcpy(dst, src + first * row, n * row * sizeof(float));
                std::memcpy(dst + n * row, src + sums_per_layer_ + first, n * sizeof(float));
            } else {
                CUDA_CHECK(cudaMemcpy(dst, src + first * row, n * row * sizeof(float), cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaMemcpy(dst + n * row, src + sums_per_layer_ + first, n * sizeof(float), cudaMemcpyDeviceToHost));
            }
        }
        out->slabs[slab] = std::move(data);
    }
    dirty_slabs_.assign((capacity_blocks_ + MeanKIndexSnapshot::slab_blocks - 1) /
                         MeanKIndexSnapshot::slab_blocks, false);
    snapshot_ = out;
    return out;
}

void MeanKIndex::restore(const std::shared_ptr<const MeanKIndexSnapshot>& state, cudaStream_t stream) {
    if (!state) { CUDA_CHECK(zero(stream)); return; }
    if (dirty_slabs_.empty()) {
        dirty_slabs_.assign((capacity_blocks_ + MeanKIndexSnapshot::slab_blocks - 1) /
                             MeanKIndexSnapshot::slab_blocks, true);
    }
    if (state->layers != layers_ || state->heads != heads_ || state->head_dim != head_dim_ ||
        state->capacity_blocks != capacity_blocks_) {
        throw std::invalid_argument("mean-K checkpoint geometry differs from workspace");
    }
    const bool dirty = std::any_of(dirty_slabs_.begin(), dirty_slabs_.end(), [](bool v) { return v; });
    if (state == snapshot_ && !dirty) { return; }
    CUDA_CHECK(end_scoring_round(stream));
    CUDA_CHECK(cudaStreamSynchronize(stream));
    const size_t row = static_cast<size_t>(heads_) * head_dim_;
    const int slabs = (capacity_blocks_ + MeanKIndexSnapshot::slab_blocks - 1) /
                       MeanKIndexSnapshot::slab_blocks;
    for (int slab = 0; slab < slabs; ++slab) {
        const auto saved = slab < static_cast<int>(state->slabs.size()) ? state->slabs[slab] : nullptr;
        const auto current = snapshot_ && slab < static_cast<int>(snapshot_->slabs.size())
                               ? snapshot_->slabs[slab] : nullptr;
        if (saved == current && !dirty_slabs_[slab]) { continue; }
        const int first = slab * MeanKIndexSnapshot::slab_blocks;
        const int n = std::min(MeanKIndexSnapshot::slab_blocks, capacity_blocks_ - first);
        for (int layer = 0; layer < layers_; ++layer) {
            float* dst = host_storage_ ? static_cast<float*>(host_storage_->host_data())
                                      : static_cast<float*>(flat_.data);
            dst += static_cast<size_t>(layer) * elements_per_layer_;
            const float* src = saved ? saved->data() + static_cast<size_t>(layer) * n * (row + 1) : nullptr;
            if (host_storage_) {
                if (src) {
                    std::memcpy(dst + first * row, src, n * row * sizeof(float));
                    std::memcpy(dst + sums_per_layer_ + first, src + n * row, n * sizeof(float));
                } else {
                    std::memset(dst + first * row, 0, n * row * sizeof(float));
                    std::memset(dst + sums_per_layer_ + first, 0, n * sizeof(float));
                }
            } else if (src) {
                CUDA_CHECK(cudaMemcpyAsync(dst + first * row, src, n * row * sizeof(float), cudaMemcpyHostToDevice, stream));
                CUDA_CHECK(cudaMemcpyAsync(dst + sums_per_layer_ + first, src + n * row, n * sizeof(float), cudaMemcpyHostToDevice, stream));
            } else {
                CUDA_CHECK(cudaMemsetAsync(dst + first * row, 0, n * row * sizeof(float), stream));
                CUDA_CHECK(cudaMemsetAsync(dst + sums_per_layer_ + first, 0, n * sizeof(float), stream));
            }
        }
    }
    blocks_written_ = state->blocks_written; tail_fill_ = state->tail_fill;
    layer_ends_ = state->layer_ends;
    dirty_slabs_.assign(slabs, false);
    snapshot_ = state;
}

namespace {
thread_local std::unique_ptr<MeanKIndex>* index_owner = nullptr;
}
void mean_k_index_bind(std::unique_ptr<MeanKIndex>* owner) noexcept { index_owner = owner; }

MeanKIndex* mean_k_index_for(std::int32_t layers, std::int32_t heads, std::int32_t head_dim,
                             std::int32_t capacity_blocks) {
    // Read the switch in the DRIVER, like the harvest does (kvmem_shadow.h:55-61): the OFF arm must
    // allocate nothing and issue nothing. A null return is the normal "KVMem is off" answer, never an
    // error, and callers gate on it.
    if (!detail::kvmem_shadow_enabled()) { return nullptr; }
    if (layers <= 0 || heads <= 0 || head_dim <= 0 || capacity_blocks <= 0) { return nullptr; }

    thread_local std::unique_ptr<MeanKIndex> standalone;
    auto& owner = index_owner ? *index_owner : standalone;
    MeanKIndex* installed = owner.get();
    if (installed == nullptr) {
        const char* host_env = std::getenv("NINFER_KVMEM_INDEX_HOST");
        const bool host_backed = host_env != nullptr && std::strcmp(host_env, "1") == 0;
        if (host_env != nullptr && host_env[0] != '\0' && !host_backed &&
            std::strcmp(host_env, "0") != 0) {
            throw std::invalid_argument("NINFER_KVMEM_INDEX_HOST must be 0 or 1");
        }
        const char* prefetch_env = std::getenv("NINFER_KVMEM_INDEX_PREFETCH");
        const bool prefetch = prefetch_env == nullptr || prefetch_env[0] == '\0' ||
                              std::strcmp(prefetch_env, "1") == 0;
        if (!prefetch && std::strcmp(prefetch_env, "0") != 0) {
            throw std::invalid_argument("NINFER_KVMEM_INDEX_PREFETCH must be 0 or 1");
        }
        installed = new MeanKIndex(layers, heads, head_dim, capacity_blocks,
                                  host_backed ? MeanKIndexStorage::HostMapped
                                              : MeanKIndexStorage::Device, prefetch);
        owner.reset(installed);
        // REQUIRED before the first append: arena bytes are not guaranteed zero, and the accumulate
        // kernel ADDS into the destination, so an unwritten block would otherwise start from
        // garbage and its count would claim tokens it never saw.
        //
        // Synchronised here rather than left stream-ordered on purpose. This runs once, outside any
        // capture, on the first prefill chunk -- the same place the harvest pays its one-time
        // allocation. Leaving it async would put a memset in front of the first append on a stream
        // whose ordering the caller does not control, and a silently-lost zero would corrupt the
        // index in the one way that is hardest to attribute (a wrong ranking, much later).
        cudaError_t zero_status = installed->zero(nullptr);
        if (zero_status == cudaSuccess) { zero_status = cudaStreamSynchronize(nullptr); }
        std::fprintf(stderr,
                     "kvmem_index: installed layers=%d heads=%d head_dim=%d blocks=%d "
                     "bytes=%.1f MiB storage=%s score_scratch=%.1f MiB zero=%s\n",
                     static_cast<int>(layers), static_cast<int>(heads),
                     static_cast<int>(head_dim), static_cast<int>(capacity_blocks),
                     static_cast<double>(installed->bytes_total()) / (1024.0 * 1024.0),
                     installed->host_backed() ? "host-mapped-staged" : "device",
                     static_cast<double>(installed->scoring_scratch_bytes()) / (1024.0 * 1024.0),
                     cudaGetErrorName(zero_status));
        if (zero_status != cudaSuccess) {
            // Fail loudly instead of handing back an index whose counts are noise: a bad count is not
            // detectable downstream, it just moves the ranking.
            owner.reset();
            installed = nullptr;
            throw std::runtime_error(std::string(kSubject) + ": zero() failed: " +
                                     cudaGetErrorName(zero_status));
        }
    }
    return installed;
}

} // namespace ninfer::ops
