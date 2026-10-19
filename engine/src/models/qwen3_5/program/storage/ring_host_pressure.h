#pragma once

#include "core/device.h"
#include "models/qwen3_5/program/storage/host_kv_store.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ninfer::models::qwen3_5::detail {

// Potential Host headroom which can be supplied without releasing any logical owner.
inline std::size_t ring_host_headroom(KVAddressSpaceStore& addresses,
                                      LogicalKVPageStore& pages,
                                      const HostKVExtentStore& extents) {
    const auto& pool = pages.physical_pool();
    std::size_t count = pool.usable_pages() - pool.allocated_pages();
    std::vector<LogicalKVPageHandle> seen;
    addresses.for_each_address([&](const KVAddressSpaceHandle& address, bool,
                                   std::uint32_t mapped) {
        for (std::uint32_t index = 0; index < mapped; ++index) {
            const auto page = addresses.logical_page(address, index);
            if (pages.device_resident(page) && pages.host_resident(page) &&
                pages.committed_columns(page) == static_cast<std::uint32_t>(kPagedKVPageSize) &&
                extents.can_release_page_replica(pages, page) &&
                std::find(seen.begin(), seen.end(), page) == seen.end()) {
                seen.push_back(page);
                ++count;
            }
        }
    });
    return count * extents.page_layout(pages).page_stride;
}

// Ring retrieval leaves a Host duplicate after restoring a page to Device. When Host fills,
// recycle those duplicates before asking the ring to demote more pages. Every logical page,
// address reference, checkpoint and Device slot survives. Call only at an execution boundary,
// outside context transactions, with no outstanding extent reservation.
inline std::uint32_t reclaim_ring_host_duplicates(DeviceContext& device,
                                                  KVAddressSpaceStore& addresses,
                                                  LogicalKVPageStore& pages,
                                                  HostKVExtentStore& extents,
                                                  std::uint32_t target) {
    if (target == 0) { return 0; }
    std::vector<LogicalKVPageHandle> releases;
    // Amortize the transfer fence when the arena is under sustained pressure.
    const std::uint32_t budget = std::max(target, 64U);
    addresses.for_each_address([&](const KVAddressSpaceHandle& address, bool,
                                   std::uint32_t mapped) {
        // Prefer newer resident pages; the oldest pages are the ring's next demotion targets.
        for (std::uint32_t index = mapped; index != 0 && releases.size() < budget; --index) {
            const auto page = addresses.logical_page(address, index - 1U);
            if (!pages.device_resident(page) || !pages.host_resident(page) ||
                pages.committed_columns(page) != static_cast<std::uint32_t>(kPagedKVPageSize) ||
                !extents.can_release_page_replica(pages, page) ||
                std::find(releases.begin(), releases.end(), page) != releases.end()) {
                continue;
            }
            releases.push_back(page);
        }
    });
    if (releases.empty()) { return 0; }
    // Device residency can be published while H2D on the compute stream is still in flight.
    // Fence all ranks before allowing the Host allocator to reuse any source bytes.
    device.synchronize();
    if (!extents.release_page_replicas(pages, releases)) {
        throw std::logic_error("ring Host duplicate release is not valid at execution boundary");
    }
    return static_cast<std::uint32_t>(releases.size());
}

} // namespace ninfer::models::qwen3_5::detail
