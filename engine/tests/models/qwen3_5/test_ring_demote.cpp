#include "models/qwen3_5/program/storage/ring_demote.h"
#include "core/device.h"
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>

namespace {
namespace s = ninfer::models::qwen3_5::detail;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

void exercise(unsigned batch, bool tight, bool require_batched) {
    ninfer::DeviceContext device(0);
    ninfer::LayoutBuilder builder;
    ninfer::KVPageGeometry geometry{.page_tokens=64,
        .device_plane_order=ninfer::PagedKVPlaneOrder::PageMajor};
    for (unsigned i=0; i<32; ++i) geometry.planes.push_back({ninfer::DType::BF16, 128, 2});
    auto plan = ninfer::plan_device_kv_page_pool(builder, {.page_group_count=40,
        .allow_logical_reservation=true, .geometry=geometry});
    auto table_plan = ninfer::plan_kv_execution_tables(builder, {.logical_page_capacity=96, .table_rows=1});
    ninfer::DeviceArena arena(builder.finish(256));
    ninfer::DeviceKVPagePool physical({arena.base(),arena.capacity()},plan);
    ninfer::KVExecutionTablePool tables({arena.base(),arena.capacity()}, table_plan, physical);
    const auto host_layout = ninfer::plan_host_kv_page_layout(physical.geometry());
    const std::array layouts{host_layout};
    ninfer::HostKVArena host((tight ? 3 : 40)*host_layout.page_stride, layouts);
    s::LogicalKVPageStore pages(physical,96);
    s::HostKVExtentStore extents(host,96);
    s::KVAddressSpaceStore addresses(pages,tables,2,96);
    const auto active = addresses.create_active(96,0,{});
    check(active.has_value(), "create active ring address");
    addresses.ensure_mapped_to_tokens(*active, 32*64+1, device.stream);
    addresses.commit_frontier(*active, 32*64+1);
    std::array<std::int32_t,33> slots;
    CUDA_CHECK(cudaMemcpy(slots.data(), tables.matrix().data, sizeof(slots), cudaMemcpyDeviceToHost));
    for(unsigned plane=0; plane<physical.plane_count(); ++plane) {
        const auto& tensor=physical.plane(plane);
        for(unsigned page=0; page<33; ++page) {
            CUDA_CHECK(cudaMemsetAsync(static_cast<unsigned char*>(tensor.data)+slots[page]*tensor.nb[3],
                1+(page*7+plane*3)%251,tensor.nb[3],device.stream));
        }
    }
    device.synchronize();
    const auto pinned = addresses.logical_page(*active,3);
    pages.set_writer(pinned,false);
    pages.pin_source(pinned);
    const std::array<std::uint32_t,3> prefer{1,4,9};
    s::RingDemoteStats stats;
    const auto begin=std::chrono::steady_clock::now();
    const auto freed=s::demote_ring_pages(addresses,pages,*active,extents,device.stream,
        1,20,prefer,batch,&stats);
    device.synchronize();
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    check(freed==(tight?3U:20U), "Host pressure must make partial progress without losing pages");
    check(addresses.mapped_pages(*active)==33 && addresses.committed_frontier(*active)==2049,
        "demotion changed history or frontier");
    for(unsigned page=0;page<33;++page) {
        const auto logical=addresses.logical_page(*active,page);
        if(page==0 || page==1 || page==3 || page==4 || page==9 || page==32)
            check(pages.device_resident(logical), "sink, preference, pinned source or partial tail demoted");
        if(pages.device_resident(logical)) continue;
        check(pages.host_resident(logical)&&pages.host_replica_current(logical),"lost KV contents");
        const auto replica=pages.host_replica(logical);
        auto source=extents.view(replica.extent).subview(replica.page_offset,1);
        for(unsigned plane=0;plane<physical.plane_count();++plane) {
            const auto& layout=host_layout.planes[plane];
            const unsigned char value=1+(page*7+plane*3)%251;
            for(std::size_t k=0;k<layout.page_payload_bytes;++k)
                check(static_cast<unsigned char>(source.data()[layout.offset+k])==value,"Host payload mismatch");
        }
        auto reservation=physical.make_empty_reservation();
        physical.resize_reservation(reservation,1);
        check(addresses.restore_page_from_host(*active,page,source,reservation,device.stream),"restore demoted KV");
        device.synchronize();
        CUDA_CHECK(cudaMemcpy(slots.data(),tables.matrix().data,sizeof(slots),cudaMemcpyDeviceToHost));
        for(unsigned plane=0;plane<physical.plane_count();++plane) {
            const auto& tensor=physical.plane(plane);
            std::vector<unsigned char> bytes(host_layout.planes[plane].page_payload_bytes);
            CUDA_CHECK(cudaMemcpy(bytes.data(),static_cast<unsigned char*>(tensor.data)+slots[page]*tensor.nb[3],
                bytes.size(),cudaMemcpyDeviceToHost));
            check(std::all_of(bytes.begin(),bytes.end(),[&](auto v){return v==1+(page*7+plane*3)%251;}),
                "Device roundtrip payload mismatch");
        }
    }
    std::cout << "batch="<<batch<<" tight="<<tight<<" freed="<<freed
              <<" copy_batches="<<stats.copy_batches<<" milliseconds="<<ms<<std::endl;
    if(require_batched&&!tight) check(stats.copy_batches==1,"regression: one transfer batch required for 20 pages");
    if (!tight) {
        s::RingDemoteStats duplicates;
        check(s::demote_ring_pages(addresses,pages,*active,extents,device.stream,
            1,20,prefer,batch,&duplicates)==20,"repeat demotion must reuse current Host replicas");
        check(duplicates.copy_batches==0&&duplicates.copied_pages==0,"Host duplicates copied again");
        const std::array<std::uint32_t,0> no_preferences{};
        const auto extra=s::demote_ring_pages(addresses,pages,*active,extents,device.stream,
            1,100,prefer,batch);
        check(extra==10,"second pass must reclaim preferred pages only when needed");
        check(pages.device_resident(addresses.logical_page(*active,0))&&pages.device_resident(pinned)&&
            pages.device_resident(addresses.logical_page(*active,32)),"second pass violated protected pages");
        check(s::demote_ring_pages(addresses,pages,*active,extents,device.stream,
            1,100,no_preferences,batch)==0,"no eligible pages must return without spinning");
    }
    // The final pressure checks also enqueue D2H. The fixture owns the Host arena,
    // so finish those copies before its memory can be freed and reused by the next case.
    device.synchronize();
    pages.unpin_source(pinned);
}
} // namespace

int main(int argc,char** argv) {
    int count = 0;
    const auto error = cudaGetDeviceCount(&count);
    if (error == cudaErrorNoDevice || error == cudaErrorInsufficientDriver ||
        (error == cudaSuccess && count == 0)) {
        std::cout << "SKIP: no usable CUDA device\n";
        return 77;
    }
    try {
        CUDA_CHECK(error);
        const unsigned batch=argc>1?std::stoul(argv[1]):32;
        const bool require_batched=argc>2;
        exercise(batch,false,require_batched);
        exercise(batch,true,false);
        std::cout<<"PASS payload roundtrip, Host exhaustion, sink, preferred pages, source pin and partial tail\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}
}
