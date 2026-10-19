#include "models/qwen3_5/program/storage/ring_host_pressure.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace store = ninfer::models::qwen3_5::detail;

namespace {
void check(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
} // namespace

int main() {
    int count = 0;
    const auto error = cudaGetDeviceCount(&count);
    if (error == cudaErrorNoDevice || error == cudaErrorInsufficientDriver || count == 0) {
        std::cout << "SKIP: no usable CUDA device\n";
        return 77;
    }
    try {
        CUDA_CHECK(error);
        ninfer::DeviceContext device(0);
        ninfer::LayoutBuilder builder;
        ninfer::DeviceKVPagePoolSpec spec{
            .page_group_count = 4,
            .allow_logical_reservation = true,
            .geometry = {.page_tokens = 64,
                         .device_plane_order = ninfer::PagedKVPlaneOrder::PageMajor,
                         .planes = {{.dtype = ninfer::DType::BF16, .leading_extent = 8,
                                     .head_extent = 2}}},
        };
        const auto pool_layout = ninfer::plan_device_kv_page_pool(builder, spec);
        const auto table_layout = ninfer::plan_kv_execution_tables(
            builder, {.logical_page_capacity = 8, .table_rows = 2});
        ninfer::DeviceArena arena(builder.finish(256));
        const ninfer::DeviceSpan backing{arena.base(), arena.capacity()};
        ninfer::DeviceKVPagePool physical(backing, pool_layout);
        ninfer::KVExecutionTablePool tables(backing, table_layout, physical);
        const auto host_layout = ninfer::plan_host_kv_page_layout(physical.geometry());
        const std::array layouts{host_layout};
        ninfer::HostKVArena host(4 * host_layout.page_stride, layouts);
        store::LogicalKVPageStore pages(physical, 12);
        store::HostKVExtentStore extents(host, 4);
        store::KVAddressSpaceStore addresses(pages, tables, 4, 8);
        const auto backup = [&](const auto& handles) {
            auto reservation = extents.prepare(pages, handles);
            check(reservation.has_value(), "reserve Host backup");
            physical.copy_to_host(extents.device_sources(*reservation),
                                  extents.writable_view(*reservation), device.stream);
            device.synchronize();
            (void)extents.publish(std::move(*reservation));
        };

        const auto old = addresses.create_active(2, 0, {});
        check(old.has_value(), "old cache address");
        addresses.ensure_mapped_to_tokens(*old, 128, device.stream);
        addresses.commit_frontier(*old, 128);
        addresses.deactivate(*old);
        const std::array old_pages{addresses.logical_page(*old, 0),
                                   addresses.logical_page(*old, 1)};
        backup(old_pages);
        for (const auto page : old_pages) { check(pages.drop_device_replica(page), "old Host only"); }

        const auto active = addresses.create_active(8, 0, {});
        check(active.has_value(), "active address");
        addresses.ensure_mapped_to_tokens(*active, 193, device.stream);
        addresses.commit_frontier(*active, 193);
        const std::array live{addresses.logical_page(*active, 0),
                              addresses.logical_page(*active, 1),
                              addresses.logical_page(*active, 2),
                              addresses.logical_page(*active, 3)};
        const auto& plane = physical.plane(0);
        CUDA_CHECK(cudaMemsetAsync(plane.data, 0x5a, 4 * plane.nb[3], device.stream));
        for (const auto page : live) { pages.set_writer(page, false); }
        backup((std::array{live[0], live[1]}));
        check(host.free_bytes() == 0 && physical.allocated_pages() == 4, "both tiers full");
        check(store::ring_host_headroom(addresses, pages, extents) == 2 * host_layout.page_stride,
              "duplicate Host replicas count as headroom before eviction");
        check(!extents.prepare(pages, std::array{live[2]}), "legacy demotion fails on Host full");
        pages.pin_source(live[0]);
        check(store::reclaim_ring_host_duplicates(device, addresses, pages, extents, 1) == 1,
              "reclaim one unpinned duplicate");
        check(pages.host_resident(live[0]) && !pages.host_resident(live[1]), "pinned source survives");
        for (const auto page : old_pages) {
            check(pages.host_resident(page) && !pages.device_resident(page), "old cache survives");
        }
        backup((std::array{live[3]}));
        check(store::reclaim_ring_host_duplicates(device, addresses, pages, extents, 1) == 0,
              "partial tail and pinned source cannot be reclaimed");
        check(store::ring_host_headroom(addresses, pages, extents) == 0,
              "pinned and partial Host replicas are not available headroom");
        pages.unpin_source(live[0]);
        check(store::reclaim_ring_host_duplicates(device, addresses, pages, extents, 1) == 1,
              "unpin makes full-page duplicate reclaimable");
        backup((std::array{live[2]}));
        pages.release_active_reference(live[2]);
        check(pages.drop_device_replica(live[2]), "demotion succeeds after duplicate reclaim");
        addresses.ensure_mapped_to_tokens(*active, 257, device.stream);
        check(addresses.mapped_pages(*active) == 5 && addresses.committed_frontier(*active) == 193,
              "new page maps without truncating history");
        // Read the demoted page back after the Host extent has been partitioned/rebound.
        const auto replica = pages.host_replica(live[2]);
        const auto source = extents.view(replica.extent).subview(replica.page_offset, 1);
        for (std::size_t i = 0; i < host_layout.page_stride; ++i) {
            check(static_cast<unsigned char>(source.data()[i]) == 0x5a, "Host payload preserved");
        }
        addresses.destructive_truncate(*active, 193);
        auto restore = physical.make_empty_reservation();
        physical.resize_reservation(restore, 1);
        check(addresses.restore_page_from_host(*active, 2, source, restore, device.stream),
              "restore duplicate on compute stream");
        // Deliberately do not synchronize: reclamation itself must fence this H2D source.
        check(store::reclaim_ring_host_duplicates(device, addresses, pages, extents, 1) == 1,
              "reclaim waits for the restored Device replica");
        std::array<std::int32_t, 4> slots;
        const auto row = tables.matrix().slice(1, 0, 1).view({8});
        CUDA_CHECK(cudaMemcpy(slots.data(), row.data, sizeof(slots), cudaMemcpyDeviceToHost));
        std::vector<unsigned char> bytes(host_layout.page_stride);
        CUDA_CHECK(cudaMemcpy(bytes.data(), static_cast<unsigned char*>(plane.data) +
                                            slots[2] * plane.nb[3], bytes.size(),
                              cudaMemcpyDeviceToHost));
        check(std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0x5a; }),
              "restored Device payload survives source reclamation");
        backup((std::array{live[1]}));
        addresses.deactivate(*active);
        const auto shared = addresses.create_inactive();
        check(shared.has_value(), "shared prefix address");
        auto fork = addresses.prepare_prefix_fork(*active, *shared, 128, 2, 0);
        addresses.commit_prefix_fork(std::move(fork), device.stream);
        check(pages.address_references(live[1]) == 2, "duplicate shared by two addresses");
        check(store::reclaim_ring_host_duplicates(device, addresses, pages, extents, 1) == 1,
              "shared duplicate is reclaimed exactly once");
        check(pages.address_references(live[1]) == 2 && pages.device_resident(live[1]) &&
                  addresses.committed_frontier(*active) == 193 &&
                  addresses.committed_frontier(*shared) == 128,
              "shared references and both frontiers survive");
        device.synchronize();
        std::cout << "ok: Host-full reproduction, duplicate reclamation, pins, partial tail, "
                     "old cache, mapped history, payload, asynchronous restore and shared prefix\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
