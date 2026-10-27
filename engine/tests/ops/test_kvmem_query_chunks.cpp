#include "core/device.h"
#include "ops/kvmem/kvmem_score.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
using namespace ninfer;
using namespace ninfer::ops;
using namespace ninfer::ops::detail;
namespace {
constexpr int L=16,H=24,G=4,D=256,B=8,R=H*D;
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void env(const char* k,const char* v) {
#ifdef _WIN32
    _putenv_s(k,v);
#else
    setenv(k,v,1);
#endif
}
std::uint16_t bits(float x) { auto u=std::bit_cast<std::uint32_t>(x);return std::uint16_t((u+0x7fffU+((u>>16)&1U))>>16); }
double val(std::uint16_t x) { return std::bit_cast<float>(std::uint32_t(x)<<16); }
float key(int l,int b,int g,int d) { return float(((d+3*b+g+l)%17)-8)/32; }
std::uint16_t query(int l,int t,int h,int d) { return bits(float(((t*11+d*3+h+l)%23)-11)/16); }
void run(int count,bool split) {
    std::unique_ptr<MeanKIndex> index; mean_k_index_bind(&index);
    auto* idx=mean_k_index_for(L,G,D,B);
    cudaStream_t stream;CUDA_CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    RawKShadowHarvest harvest(L,G*D,128);harvest.set_round_width(128);
    std::shared_ptr<const MeanKIndexSnapshot> earlier;
    for(int origin=0;origin<384;origin+=128) {
        for(int l=0;l<L;++l) {
            std::vector<std::uint16_t> k(128*G*D);
            for(int t=0;t<128;++t) for(int g=0;g<G;++g) for(int d=0;d<D;++d)
                k[(t*G+g)*D+d]=bits(key(l,(origin+t)/64,g,d));
            CUDA_CHECK(cudaMemcpyAsync(harvest.staging(l).data,k.data(),k.size()*2,cudaMemcpyHostToDevice,stream));
            CUDA_CHECK(cudaStreamSynchronize(stream));
        }
        idx->append_committed(harvest,origin,128,128,stream);
        if(origin==128) earlier=idx->snapshot(stream);
    }
    const auto final_history=idx->snapshot(stream);
    KvMemScoreProbe probe;kvmem_score_bind(&probe);
    constexpr int begin=509;const int end=begin+count;
    std::vector<std::int32_t> prompt(end+128,17);
    auto chunk=[&](int origin,int width,int omit=-1,bool publish=true) {
        kvmem_score_set_query_identity(begin,end,prompt);kvmem_score_set_chunk_origin(origin);
        kvmem_score_begin(B,idx->blocks_written(),idx->tail_fill(),64,L,H,G,D,stream);
        DeviceBuffer q(std::size_t(width)*R*2);
        for(int l=0;l<L;++l) {
            if(l==omit) continue;
            std::vector<std::uint16_t> data(std::size_t(width)*R);
            for(int t=0;t<width;++t) for(int h=0;h<H;++h) for(int d=0;d<D;++d)
                data[(t*H+h)*D+d]=query(l,origin+t-begin,h,d);
            CUDA_CHECK(cudaMemcpyAsync(q.p,data.data(),data.size()*2,cudaMemcpyHostToDevice,stream));
            kvmem_score_accumulate(l,q.p,width,width,stream);
            CUDA_CHECK(cudaStreamSynchronize(stream));
        }
        kvmem_score_submit_readback(stream);
        if(publish && kvmem_query_publish()) {
            const auto saved=probe.query;
            require(kvmem_replay_query(saved,64,stream,"assembled_test"),"assembled query failed fresh scoring");
        }
        kvmem_score_finish("chunk_oracle");CUDA_CHECK(idx->end_scoring_round(stream));
    };
    if(split) {
        idx->restore(earlier,stream);
        chunk(begin-5,8);
        require(!probe.query,"incomplete first fragment was published as a complete query");
        idx->restore(final_history,stream);
        int offset=3;while(offset<count) {int width=std::min(128,count-offset);chunk(begin+offset,width);offset+=width;}
    } else chunk(begin,count);
    const auto saved=probe.query;
    require(saved && saved->origin==begin && saved->tokens==count,"query lost rows at a prefill chunk boundary");
    for(int l=0;l<L;++l) {
        const auto* data=reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(saved->storage->host_data())+l*saved->layer_bytes);
        for(int t=0;t<count;++t) for(int h=0;h<H;++h) for(int d=0;d<D;++d)
            require(data[(t*H+h)*D+d]==query(l,t,h,d),"cached query row differs from supplied BF16 input");
    }
    const auto fresh_scores=probe.score_host;
    require(kvmem_rescore_cached_query(begin,end,end+64,64,stream,prompt),"complete cached query could not rescore");
    // Independent FP64 softmax over six known, constant-key pages.
    std::vector<double> oracle(6,0);
    for(int l=0;l<L;++l) for(int t=0;t<count;++t) for(int h=0;h<H;++h) {
        double logits[6],top=-1.e30,z=0;
        for(int b=0;b<6;++b) {
            double dot=0;for(int d=0;d<D;++d) dot+=val(query(l,t,h,d))*key(l,b,h/(H/G),d);
            logits[b]=dot/16;top=std::max(top,logits[b]);
        }
        for(double x:logits) z+=std::exp(x-top);
        for(int b=0;b<6;++b) oracle[b]+=std::exp(logits[b]-top)/z/(L*H);
    }
    double mass=0;for(int b=0;b<6;++b) {
        require(std::abs(probe.score_host[b]-oracle[b])<2.e-5*std::max(1.,oracle[b]),"cached scorer disagrees with independent FP64 oracle");
        require(std::abs(fresh_scores[b]-oracle[b])<2.e-5*std::max(1.,oracle[b]),"fresh assembled scorer disagrees with independent FP64 oracle");
        mass+=probe.score_host[b];
    }
    require(std::abs(mass-count)<1.e-4*count,"full-query score mass does not equal complete query length");
    if(split) {
        // Missing layer, missing chunk, changed identity, cancelled round, and checkpoint rollback
        // must never turn partial/new bytes into a published complete query.
        chunk(begin-5,8);chunk(begin+3,count-3,L-1);
        require(probe.query==saved,"missing layer replaced immutable checkpoint query");
        chunk(begin-5,8);chunk(begin+4,count-4);
        require(probe.query==saved,"gap in query capture was silently accepted");
        chunk(begin-5,8);prompt[begin]++;chunk(begin+3,count-3);
        require(probe.query==saved,"changed token identity mixed query fragments");prompt[begin]--;
        chunk(begin-5,8,-1,false);chunk(begin+3,count-3);
        require(probe.query==saved,"cancelled capture was later published");
        chunk(begin-5,8);probe.pending_query->heads++;chunk(begin+3,count-3);
        require(probe.query==saved,"changed geometry mixed query fragments");
        KvMemContextSnapshot checkpoint;checkpoint.query=saved;
        chunk(begin-5,8);probe.pending_query.reset();probe.query=checkpoint.query;
        chunk(begin+3,count-3);require(probe.query==saved,"rollback recovered unpublished partial query");
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));CUDA_CHECK(cudaStreamDestroy(stream));
    kvmem_score_bind(nullptr);mean_k_index_bind(nullptr);
    std::printf("PASS query rows=%d split=%d: exact capture, fresh/cached FP64 scores%s\n",count,int(split),split?", identity/cancel/gap/rollback":"");
}
}
int main() {
    env("NINFER_TERNARY_KVMEM","1");env("NINFER_TERNARY_KVMEM_SCORE","1");
    env("NINFER_KVMEM_INDEX_HOST","1");env("NINFER_KVMEM_CACHE_QUERY","1");
    env("NINFER_KVMEM_QUERY_ASSEMBLE","1");
    try {
        KvMemScoreProbe p;kvmem_score_bind(&p);std::vector<std::int32_t> prompt(600,17);
        kvmem_score_set_query_identity(0,512,prompt);require(p.query_identity.size()==512,"bounded complete query rejected");
        kvmem_score_set_query_identity(0,513,prompt);require(p.query_identity.empty(),"oversized query admitted to full capture");
        kvmem_score_bind(nullptr);
        for(int n:{9,64,259,512}) {run(n,false);run(n,true);}return 0;
    }
    catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
