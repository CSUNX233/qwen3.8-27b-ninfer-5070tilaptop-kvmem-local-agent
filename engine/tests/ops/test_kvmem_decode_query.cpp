#include "core/device.h"
#include "core/paged_kv_cache.h"
#include "ninfer/ops/rmsnorm.h"
#include "ninfer/ops/rmsnorm_rope.h"
#include "ops/kvmem/kvmem_score.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace ninfer;
using namespace ninfer::ops;
using namespace ninfer::ops::detail;
namespace {
void require(bool value, const char* why) { if (!value) { throw std::runtime_error(why); } }
void env(const char* name, const char* val) {
#ifdef _WIN32
    _putenv_s(name,val);
#else
    setenv(name,val,1);
#endif
}
std::uint16_t bits(float x) {
    auto u = std::bit_cast<std::uint32_t>(x);
    return static_cast<std::uint16_t>((u + 0x7fffU + ((u >> 16) & 1U)) >> 16);
}
float value(std::uint16_t x) { return std::bit_cast<float>(std::uint32_t(x) << 16); }
template<class T> std::vector<T> read(const Tensor& x) {
    std::vector<T> out(x.bytes() / sizeof(T));
    CUDA_CHECK(cudaMemcpy(out.data(), x.data, x.bytes(), cudaMemcpyDeviceToHost)); return out;
}

void capture_oracle() {
    std::size_t cases = 0;
    for (int heads : {16, 24}) for (int width : {1, 2, 4, 17, 128}) {
        const int kh = heads == 16 ? 2 : 4;
        // Separate allocations also make overlap validation unambiguous.
        DeviceBuffer qb(width*heads*256*2), kb(width*kh*256*2), qwb(512), kwb(512), pb(width*4);
        DeviceBuffer qob(qb.bytes), kob(kb.bytes), cb(kb.bytes), nb(kb.bytes);
        Tensor q(qb.p,DType::BF16,{256,heads,width}), k(kb.p,DType::BF16,{256,kh,width});
        Tensor qw(qwb.p,DType::BF16,{256}), kw(kwb.p,DType::BF16,{256}), p(pb.p,DType::I32,{width});
        Tensor qo(qob.p,DType::BF16,{256,heads,width}), ko(kob.p,DType::BF16,{256,kh,width});
        Tensor captured(cb.p,DType::BF16,{256,kh,width}), normalized(nb.p,DType::BF16,{256,kh,width});
        std::vector<std::uint16_t> weights(256), qi(width*heads*256), ki(width*kh*256);
        for (int d=0;d<256;++d) weights[d]=bits(float((d*7)%19-9)/32);
        qwb.copy_from_host(weights.data(),512); kwb.copy_from_host(weights.data(),512);
        cudaStream_t stream; CUDA_CHECK(cudaStreamCreate(&stream));
        cudaGraph_t graph; cudaGraphExec_t executable;
        // Inputs are initialized before capture and changed before each replay.
        CUDA_CHECK(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal));
        rmsnorm_rope_capture_key(p,qw,kw,q,k,qo,ko,captured,stream);
        CUDA_CHECK(cudaStreamEndCapture(stream,&graph));
        CUDA_CHECK(cudaGraphInstantiate(&executable,graph,nullptr,nullptr,0));
        for (int round=0;round<3;++round) {
            for (size_t i=0;i<qi.size();++i) qi[i]=bits(float(int((i*13+round*11)%127)-63)/32);
            for (size_t i=0;i<ki.size();++i) ki[i]=bits(float(int((i*17+round*23)%113)-56)/16);
            std::vector<int> positions(width);
            for (int t=0;t<width;++t) positions[t]=round*100000+t;
            qb.copy_from_host(qi.data(),qi.size()*2); kb.copy_from_host(ki.data(),ki.size()*2);
            pb.copy_from_host(positions.data(),positions.size()*4);
            CUDA_CHECK(cudaGraphLaunch(executable,stream)); CUDA_CHECK(cudaStreamSynchronize(stream));
            const auto cq=read<std::uint16_t>(qo), ck=read<std::uint16_t>(ko), raw=read<std::uint16_t>(captured);
            // Numerical oracle: represented BF16 input, serial FP64 sum and normalization.
            double error2=0, norm2=0;
            for (size_t base=0;base<ki.size();base+=256) {
                double squares=0;
                for (int d=0;d<256;++d) { double v=value(ki[base+d]); squares+=v*v; }
                const double inv=1/std::sqrt(squares/256+1.e-6);
                for (int d=0;d<256;++d) {
                    const double expected=value(ki[base+d])*inv*(1+value(weights[d]));
                    const double delta=value(raw[base+d])-expected;
                    error2+=delta*delta; norm2+=expected*expected;
                }
            }
            require(std::sqrt(error2/norm2)<0.0025,"captured key fails independent FP64 normalization oracle");
            rmsnorm(k,kw,1.e-6f,true,normalized,stream);
            rmsnorm_rope(p,qw,kw,q,k,qo,ko,stream); CUDA_CHECK(cudaStreamSynchronize(stream));
            require(raw==read<std::uint16_t>(normalized),"captured key differs from standalone represented normalization");
            require(cq==read<std::uint16_t>(qo) && ck==read<std::uint16_t>(ko),"capture changes normal q/k output bits");
            ++cases;
        }
        CUDA_CHECK(cudaGraphExecDestroy(executable)); CUDA_CHECK(cudaGraphDestroy(graph));
        CUDA_CHECK(cudaStreamDestroy(stream));
    }
    std::printf("PASS fusion: %zu graph replays, changed inputs/positions, FP64 norm oracle and exact q/k equality\n",cases);
}

void committed_oracle() {
    constexpr int layers=16, cells=1024, blocks=8;
    for (auto storage : {MeanKIndexStorage::Device,MeanKIndexStorage::HostMapped}) {
        MeanKIndex index(layers,4,256,blocks,storage);
        RawKShadowHarvest harvest(layers,cells,128);
        std::vector<float> expected(layers*blocks*(cells+1),0);
        int cursor=0;
        for (int width : {1,4,17,4,1,4,17,4,17}) {
            // Capture metadata intentionally disagrees with replay width: replay must not rely on it.
            harvest.set_round_width(128);
            for (int committed : {0,1,width}) {
                auto before=index.snapshot(nullptr);
                for (int layer=0;layer<layers;++layer) {
                    std::vector<std::uint16_t> data(width*cells);
                    for (size_t i=0;i<data.size();++i) data[i]=bits(float(int((i*17+layer*11+cursor)%61)-30)/64);
                    CUDA_CHECK(cudaMemcpy(harvest.staging(layer).data,data.data(),data.size()*2,cudaMemcpyHostToDevice));
                    for (int t=0;t<committed;++t) {
                        const size_t base=layer*blocks*(cells+1), page=(cursor+t)/64;
                        for (int d=0;d<cells;++d) expected[base+page*cells+d]+=value(data[t*cells+d]);
                        expected[base+blocks*cells+page]+=1;
                    }
                }
                index.append_committed(harvest,cursor,width,committed,nullptr);
                // A repeated commit is idempotent, including partial page boundaries.
                index.append_committed(harvest,cursor,width,committed,nullptr);
                CUDA_CHECK(cudaDeviceSynchronize());
                for (int l=0;l<layers;++l) {
                    const auto sums=read<float>(index.layer_sums(l)), counts=read<float>(index.layer_counts(l));
                    const size_t base=l*blocks*(cells+1);
                    require(std::equal(sums.begin(),sums.end(),expected.begin()+base),"committed sums include rejected/stale keys");
                    require(std::equal(counts.begin(),counts.end(),expected.begin()+base+blocks*cells),"committed page counts disagree with token oracle");
                }
                // Snapshot rollback must remove speculative append and permit another branch.
                auto after=index.snapshot(nullptr); index.restore(before,nullptr); index.restore(after,nullptr);
                cursor+=committed;
            }
        }
    }
    std::puts("PASS commit: device/host, graph widths, accepted/rejected/zero prefixes, replay, page boundaries and checkpoint restore");
}

void cached_query_oracle() {
    constexpr int layers=16, cells=1024, blocks=8, qt=4;
    std::unique_ptr<MeanKIndex> index;
    mean_k_index_bind(&index);
    auto* idx=mean_k_index_for(layers,4,256,blocks);
    RawKShadowHarvest harvest(layers,cells,128);
    harvest.set_round_width(128);
    std::vector<std::uint16_t> keys(128*cells), qbits(qt*24*256);
    for(size_t i=0;i<keys.size();++i) keys[i]=bits(float(int((i*7)%31)-15)/32);
    for(size_t i=0;i<qbits.size();++i) qbits[i]=bits(float(int((i*13)%53)-26)/32);
    DeviceBuffer q(qbits.size()*2); q.copy_from_host(qbits.data(),qbits.size()*2);
    for(int l=0;l<layers;++l) CUDA_CHECK(cudaMemcpy(harvest.staging(l).data,keys.data(),keys.size()*2,cudaMemcpyHostToDevice));
    idx->append_committed(harvest,0,128,128,nullptr);
    KvMemScoreProbe probe; kvmem_score_bind(&probe);
    std::vector<std::int32_t> prompt(256,17);
    auto score=[&] {
        kvmem_score_set_query_identity(128,132,prompt); kvmem_score_set_chunk_origin(128);
        kvmem_score_begin(blocks,idx->blocks_written(),idx->tail_fill(),kPagedKVPageSize,layers,24,4,256,nullptr);
        for(int l=0;l<layers;++l) kvmem_score_accumulate(l,q.p,qt,qt,nullptr);
        kvmem_score_submit_readback(nullptr); kvmem_query_publish();
        kvmem_score_finish("test_fresh");
    };
    // The first question may be in the first chunk, before any index exists.
    auto indexed=idx->snapshot(nullptr); CUDA_CHECK(idx->zero(nullptr)); score();
    require(probe.query && probe.query->tokens==qt,"first-chunk question was lost with zero history");
    idx->restore(indexed,nullptr);
    score(); const auto saved=probe.query; const auto old_generation=probe.generation;
    require(saved && saved->tokens==qt,"query was not captured");
    require(!kvmem_rescore_cached_query(129,133,200,kPagedKVPageSize,nullptr,prompt),"changed question identity was accepted");
    require(!kvmem_rescore_cached_query(128,132,131,kPagedKVPageSize,nullptr,prompt),"partially cached question was accepted");
    require(!kvmem_rescore_cached_query(128,132,132,kPagedKVPageSize,nullptr,prompt),"unchanged cached question unnecessarily refreshed its ranking");
    auto other=prompt; other[130]=18;
    require(!kvmem_rescore_cached_query(128,132,256,kPagedKVPageSize,nullptr,other),"same positions with different question tokens were accepted");
    require(probe.generation==old_generation,"rejected cache match changed published ranking");
    idx->append_committed(harvest,128,128,128,nullptr);
    require(kvmem_rescore_cached_query(128,132,256,kPagedKVPageSize,nullptr,prompt),"cached question did not refresh");
    const auto refreshed=probe.score_host; const auto ranking=probe.kept;
    require(probe.n_blocks==4 && refreshed[3]>0,"new committed history missing from refreshed scores");
    score();
    for(int b=0;b<4;++b) require(std::abs(refreshed[b]-probe.score_host[b])<1.e-6f,"cached Q refresh differs from fresh Q scoring");
    require(ranking==probe.kept,"cached Q refresh changes fresh selector behavior");
    require(saved!=probe.query,"fresh query mutated immutable checkpoint data");
    double mass=0;for(int b=0;b<4;++b)mass+=probe.score_host[b];
    require(std::abs(mass-qt)<1.e-4,"query probability mass invalid");
    kvmem_score_bind(nullptr); mean_k_index_bind(nullptr);
    std::puts("PASS cached query: exact-prefix gates, immutable snapshot, new decode pages, fresh-vs-cached score and ranking equality");
}
}
int main() {
    env("NINFER_TERNARY_KVMEM","1"); env("NINFER_TERNARY_KVMEM_SCORE","1");
    env("NINFER_KVMEM_INDEX_HOST","1"); env("NINFER_KVMEM_CACHE_QUERY","1");
    env("NINFER_TERNARY_KVMEM_SCORE_BUDGET","128"); env("NINFER_TERNARY_KVMEM_SCORE_SINK","64");
    try { capture_oracle(); committed_oracle(); cached_query_oracle(); return 0; }
    catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what());return 1; }
}
