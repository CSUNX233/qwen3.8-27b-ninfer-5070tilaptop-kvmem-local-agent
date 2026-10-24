#pragma once

#include "models/qwen3_5/program/storage/host_kv_store.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace ninfer::models::qwen3_5::detail {

struct RingDemoteStats {
    std::uint32_t copy_batches = 0;
    std::uint32_t copied_pages = 0;
};

// Preserve the ring's oldest-first choice and all existing eligibility checks. Group cold
// pages into one Host extent so contiguous physical runs use the pool's strided D2H path.
// The compute stream orders the copies before subsequent reuse of a released Device slot.
inline std::uint32_t demote_ring_pages(
    KVAddressSpaceStore& addresses, LogicalKVPageStore& pages,
    const KVAddressSpaceHandle& address, HostKVExtentStore& extents, RankStreams streams,
    std::uint32_t sink_pages, std::uint32_t target_free,
    std::span<const std::uint32_t> preferred, std::uint32_t batch_limit,
    RingDemoteStats* stats = nullptr) {
    if (!addresses.valid(address) || target_free == 0) { return 0; }
    batch_limit = std::clamp(batch_limit, 1U, 64U);
    const std::uint32_t mapped = addresses.mapped_pages(address);
    std::uint32_t demoted = 0;
    std::vector<LogicalKVPageHandle> batch;
    batch.reserve(std::min(target_free, batch_limit));
    const auto flush = [&]() -> bool {
        std::size_t offset = 0;
        while (offset < batch.size()) {
            std::size_t count = batch.size() - offset;
            auto members = std::span<const LogicalKVPageHandle>(batch).subspan(offset, count);
            auto reserved = extents.prepare(pages, members);
            // A large extent can fail while smaller ones still fit. Keep the one-page
            // fallback so batching cannot turn partial Host headroom into no progress.
            while (!reserved && count > 1) {
                count /= 2;
                members = std::span<const LogicalKVPageHandle>(batch).subspan(offset, count);
                auto smaller = extents.prepare(pages, members);
                if (smaller) { reserved.emplace(std::move(*smaller)); }
            }
            if (!reserved) { return false; }
            const auto sources = extents.device_sources(*reserved);
            pages.physical_pool().copy_to_host(sources, extents.writable_view(*reserved), streams);
            (void)extents.publish(std::move(*reserved));
            if (stats != nullptr) {
                ++stats->copy_batches;
                stats->copied_pages += static_cast<std::uint32_t>(count);
            }
            for (const auto logical : members) {
                pages.release_active_reference(logical);
                if (pages.drop_device_replica(logical)) { ++demoted; }
            }
            offset += count;
        }
        batch.clear();
        return true;
    };

    for (unsigned pass = 0; pass < 2 && demoted < target_free; ++pass) {
        for (std::uint32_t index = sink_pages;
             index < mapped && demoted + batch.size() < target_free; ++index) {
            if (pass == 0 && std::binary_search(preferred.begin(), preferred.end(), index)) {
                continue;
            }
            const auto logical = addresses.logical_page(address, index);
            if (!pages.valid(logical) || !pages.device_resident(logical) ||
                pages.source_pins(logical) != 0 ||
                pages.committed_columns(logical) != static_cast<std::uint32_t>(kPagedKVPageSize)) {
                continue;
            }
            if (pages.host_resident(logical)) {
                if (!pages.host_replica_current(logical)) { continue; }
                // Preserve order at an existing Host replica; it needs no new transfer.
                if (!flush()) { return demoted; }
                pages.set_writer(logical, false);
                pages.release_active_reference(logical);
                if (pages.drop_device_replica(logical)) { ++demoted; }
            } else {
                pages.set_writer(logical, false);
                batch.push_back(logical);
                if (batch.size() == batch_limit && !flush()) { return demoted; }
            }
        }
        if (!flush()) { return demoted; }
    }
    return demoted;
}

} // namespace ninfer::models::qwen3_5::detail
