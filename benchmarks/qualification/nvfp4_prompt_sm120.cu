// Standalone SM120 NVFP4 prompt qualification and interleaved CUDA-event benchmark.
// Compile with engine/src and engine/include on the include path; no engine libraries required.
#include "ops/softmax_attention/dense/causal_cache/prompt_nvfp4.cuh"
#include "ops/softmax_attention/dense/causal_cache/prompt_nvfp4_sm120.cuh"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
using namespace ninfer::ops;
#define CK(x) do { auto e=(x); if(e!=cudaSuccess){fprintf(stderr,"CUDA %s:%d %s\n",__FILE__,__LINE__,cudaGetErrorString(e));exit(2);} }while(0)
struct Buffers {
    __nv_bfloat16 *q,*out;
    unsigned char *k,*v,*ks,*vs;
    int *table,*pos;
    unsigned *mask;
};
cudaStream_t test_stream = nullptr;
template<class T> T* dev(const std::vector<T>& h) {
    T* p; CK(cudaMalloc(&p,h.size()*sizeof(T))); CK(cudaMemcpy(p,h.data(),h.size()*sizeof(T),cudaMemcpyHostToDevice)); return p;
}
template<class G,class M> void launch_metadata(bool candidate,Buffers b,int width,M md) {
    if(candidate) {
        CK(cudaFuncSetAttribute(causal_attention_prompt_nvfp4_sm120_kernel<G,M>,cudaFuncAttributeMaxDynamicSharedMemorySize,kCausalPromptNvfp4Sm120SmemBytes));
        causal_attention_prompt_nvfp4_sm120_kernel<G,M><<<dim3((width+31)/32,G::QHeads),kCausalPromptNvfp4Sm120Threads,kCausalPromptNvfp4Sm120SmemBytes,test_stream>>>(b.q,b.k,b.v,b.ks,b.vs,md,b.pos,0.0625f,b.out,width);
    } else {
        CK(cudaFuncSetAttribute(causal_attention_prompt_nvfp4_kernel<G,M>,cudaFuncAttributeMaxDynamicSharedMemorySize,kCausalPromptNvfp4SmemBytes));
        causal_attention_prompt_nvfp4_kernel<G,M><<<dim3((width+31)/32,G::QHeads),kCausalPromptNvfp4Threads,kCausalPromptNvfp4SmemBytes,test_stream>>>(b.q,b.k,b.v,b.ks,b.vs,md,b.pos,0.0625f,b.out,width);
    }
    CK(cudaGetLastError());
}
template<int H,int KH> void launch(int which,Buffers b,int width,int valid,bool masked) {
    using G=CausalAttentionGeometry<H,KH,1>;
    const auto* mask=masked?b.mask:nullptr;
    if(valid==width && width==128 && !masked) {
        launch_metadata<G>(which,b,width,PagedKVDirectMetadata{b.table,mask});
    } else if(valid==width) {
        launch_metadata<G>(which,b,width,PagedKVBatchMetadata<false>{b.table,nullptr,b.pos+2,1000000,mask});
    } else {
        launch_metadata<G>(which,b,width,PagedKVBatchMetadata<true>{b.table,b.pos+1,b.pos+2,1000000,mask});
    }
}
__global__ void decode_qualification(unsigned short* out) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=256*127)return;
    unsigned packed=(unsigned(i%256))*0x01010101u;
    int4 b=causal_prompt_nvfp4_sm120_bf16x8(reinterpret_cast<unsigned char*>(&packed),i/256);
    *reinterpret_cast<int4*>(out+8*i)=b;
}
float fp4(unsigned c) { constexpr float a[]={0,.5f,1,1.5f,2,3,4,6}; return (c&8)?-a[c&7]:a[c&7]; }
float fp8(unsigned c) { int e=(c>>3)&15,m=c&7; return e?std::ldexp(1.f+m/8.f,e-7):std::ldexp(float(m),-9); }
void hadamard(std::vector<float>& v) { for(int s=1;s<256;s*=2) for(int a=0;a<256;a+=2*s) for(int j=0;j<s;j++){float x=v[a+j],y=v[a+j+s];v[a+j]=x+y;v[a+j+s]=x-y;}for(float& x:v)x*=.0625f; }
double oracle(const std::vector<__nv_bfloat16>& q,const std::vector<unsigned char>& k,
              const std::vector<unsigned char>& v,const std::vector<unsigned char>& ks,
              const std::vector<unsigned char>& vs,const std::vector<int>& table,
              const std::vector<unsigned>& mask,const std::vector<__nv_bfloat16>& actual,
              int h,int kh,int row,int head,int base,bool masked) {
    std::vector<float> qr(256);for(int d=0;d<256;d++)qr[d]=float(q[(row*h+head)*256+d]);hadamard(qr);
    for(float& f:qr)f=float(__float2bfloat16(f));
    int end=base+row+1;std::vector<double> logits(end,-INFINITY); double mx=-INFINITY;
    auto idx=[&](int t,int d,int leading){return ((size_t(table[t/64])*kh+head/(h/kh))*64+t%64)*leading+d;};
    for(int t=0;t<end;t++) {
        if(masked && !(mask[t/64/32]>>(t/64%32)&1))continue;
        double sum=0;for(int d=0;d<256;d++) {auto b=k[idx(t,d/2,128)];float x=fp4((d&1)?b>>4:b&15)*fp8(ks[idx(t,d/16,16)]);sum+=double(qr[d])*float(__float2bfloat16(x));}
        logits[t]=sum*.0625;mx=std::max(mx,logits[t]);
    }
    std::vector<double> o(256);double den=0;
    if(mx!=-INFINITY)for(int t=0;t<end;t++)if(logits[t]!=-INFINITY){double p=std::exp(logits[t]-mx);den+=p;for(int d=0;d<256;d++){auto b=v[idx(t,d/2,128)];o[d]+=p*fp4((d&1)?b>>4:b&15)*fp8(vs[idx(t,d/16,16)]);}}
    // Independent FP64 inverse Hadamard and normalization.
    for(int s=1;s<256;s*=2)for(int a=0;a<256;a+=2*s)for(int j=0;j<s;j++){double x=o[a+j],y=o[a+j+s];o[a+j]=x+y;o[a+j+s]=x-y;}
    double err=0,norm=0;for(int d=0;d<256;d++){double ref=den?o[d]/(den*16):0,diff=float(actual[(row*h+head)*256+d])-ref;err+=diff*diff;norm+=ref*ref;}
    return std::sqrt(err/std::max(norm,1e-30));
}
template<int H,int KH> void test(int width,int valid,int context,int maskmode,bool fragmented,int repeats) {
    int pages=(context+width+63)/64+1;
    std::mt19937 rng(61019+width+context);
    std::vector<int> table(pages);for(int i=0;i<pages;i++)table[i]=i;if(fragmented)std::shuffle(table.begin(),table.end(),rng);
    std::vector<unsigned char> k(size_t(pages)*64*KH*128),v(k.size()),ks(size_t(pages)*64*KH*16),vs(ks.size());
    for(auto& x:k)x=rng();for(auto& x:v)x=rng();for(auto& x:ks)x=40+rng()%21;for(auto& x:vs)x=40+rng()%21;
    std::vector<__nv_bfloat16> q(width*H*256);for(auto& x:q)x=__float2bfloat16((int(rng()%2001)-1000)/1000.f);
    std::vector<unsigned> mask((pages+31)/32,0);for(int p=0;p<pages;p++)if(maskmode==0||(maskmode==1&&(p<192||p>=context/64-304||p%32==7))||(maskmode==3&&(p%11==7)))mask[p/32]|=1u<<(p%32);
    std::vector<int> pos={context,valid,0};Buffers b{dev(q),nullptr,dev(k),dev(v),dev(ks),dev(vs),dev(table),dev(pos),dev(mask)};
    CK(cudaMalloc(&b.out,q.size()*2));
    const char* names[]={"baseline","sm120"};
    std::vector<std::vector<__nv_bfloat16>> outputs(2,std::vector<__nv_bfloat16>(q.size()));
    bool masked=maskmode!=0;int failed=0;
    for(int a : {0,1}) {launch<H,KH>(a,b,width,valid,masked);CK(cudaDeviceSynchronize());CK(cudaMemcpy(outputs[a].data(),b.out,q.size()*2,cudaMemcpyDeviceToHost));
        double er=0,norm=0;int mismatches=0;for(size_t j=0;j<q.size();j++){double r=float(outputs[0][j]),d=float(outputs[a][j])-r;er+=d*d;norm+=r*r;if(memcmp(&outputs[a][j],&outputs[0][j],2))mismatches++;if(!std::isfinite(float(outputs[a][j])))failed++;}
        double l2=sqrt(er/std::max(norm,1e-30)); if(l2>.004)failed++;
        double ref=0;if(valid)for(int head:{0,H-1})for(int row:{0,valid-1})ref=std::max(ref,oracle(q,k,v,ks,vs,table,mask,outputs[a],H,KH,row,head,context,masked));
        if(ref>.015)failed++;
        printf("CHECK H=%d KH=%d T=%d valid=%d C=%d mask=%d frag=%d %s mismatches=%d vsbase_l2=%.8g oracle_l2=%.8g\n",H,KH,width,valid,context,maskmode,fragmented,names[a],mismatches,l2,ref);fflush(stdout);
    }
    if(failed){fprintf(stderr,"QUALIFICATION FAILED %d\n",failed);exit(3);}
    CK(cudaStreamCreateWithFlags(&test_stream,cudaStreamNonBlocking));
    CK(cudaStreamBeginCapture(test_stream,cudaStreamCaptureModeGlobal));
    launch<H,KH>(1,b,width,valid,masked);
    cudaGraph_t graph; CK(cudaStreamEndCapture(test_stream,&graph));
    cudaGraphExec_t exec; CK(cudaGraphInstantiate(&exec,graph,0));
    for(int replay=0;replay<5;replay++) {
        CK(cudaMemsetAsync(b.out,0xa5,q.size()*2,test_stream));
        CK(cudaGraphLaunch(exec,test_stream)); CK(cudaStreamSynchronize(test_stream));
        std::vector<__nv_bfloat16> replayed(q.size());
        CK(cudaMemcpy(replayed.data(),b.out,q.size()*2,cudaMemcpyDeviceToHost));
        if(memcmp(replayed.data(),outputs[1].data(),q.size()*2)) {
            fprintf(stderr,"GRAPH REPLAY MISMATCH\n");exit(3);
        }
    }
    if(masked) {
        CK(cudaMemsetAsync(b.mask,0,mask.size()*sizeof(unsigned),test_stream));
        CK(cudaGraphLaunch(exec,test_stream));CK(cudaStreamSynchronize(test_stream));
        std::vector<__nv_bfloat16> empty(q.size());
        CK(cudaMemcpy(empty.data(),b.out,q.size()*2,cudaMemcpyDeviceToHost));
        for(auto x:empty)if(float(x)!=0.f){fprintf(stderr,"GRAPH MASK UPDATE MISMATCH\n");exit(3);}
        CK(cudaMemcpyAsync(b.mask,mask.data(),mask.size()*sizeof(unsigned),cudaMemcpyHostToDevice,test_stream));
        CK(cudaGraphLaunch(exec,test_stream));CK(cudaStreamSynchronize(test_stream));
        CK(cudaMemcpy(empty.data(),b.out,q.size()*2,cudaMemcpyDeviceToHost));
        if(memcmp(empty.data(),outputs[1].data(),q.size()*2)){fprintf(stderr,"GRAPH MASK RESTORE MISMATCH\n");exit(3);}
    }
    CK(cudaGraphExecDestroy(exec));CK(cudaGraphDestroy(graph));
    CK(cudaStreamDestroy(test_stream));test_stream=nullptr;
    puts("GRAPH PASS 5 identical replays");
    cudaEvent_t start,end;CK(cudaEventCreate(&start));CK(cudaEventCreate(&end));
    std::vector<std::vector<float>> timings(2);
    for(int r=0;r<repeats;r++)for(int j=0;j<2;j++) {int a=(r%2)?1-j:j;launch<H,KH>(a,b,width,valid,masked);CK(cudaEventRecord(start));for(int z=0;z<3;z++)launch<H,KH>(a,b,width,valid,masked);CK(cudaEventRecord(end));CK(cudaEventSynchronize(end));float ms;CK(cudaEventElapsedTime(&ms,start,end));timings[a].push_back(ms/3);}
    for(int a : {0,1}){auto& t=timings[a];sort(t.begin(),t.end());printf("TIME H=%d T=%d valid=%d C=%d mask=%d frag=%d %s median_ms=%.6f min_ms=%.6f\n",H,width,valid,context,maskmode,fragmented,names[a],t[t.size()/2],t[0]);}fflush(stdout);
    CK(cudaEventDestroy(start));CK(cudaEventDestroy(end));
    for(void* p:{(void*)b.q,(void*)b.out,(void*)b.k,(void*)b.v,(void*)b.ks,(void*)b.vs,(void*)b.table,(void*)b.pos,(void*)b.mask})CK(cudaFree(p));
}
int main(int argc,char** argv) {
    cudaDeviceProp p;CK(cudaGetDeviceProperties(&p,0));printf("DEVICE %s cc=%d%d SMs=%d smem_SM=%zu smem_block=%zu\n",p.name,p.major,p.minor,p.multiProcessorCount,p.sharedMemPerMultiprocessor,p.sharedMemPerBlockOptin);fflush(stdout);
    unsigned short* dp;CK(cudaMalloc(&dp,256*127*8*2));
    decode_qualification<<<(256*127+255)/256,256>>>(dp);CK(cudaGetLastError());
    std::vector<unsigned short> decoded(256*127*8);CK(cudaMemcpy(decoded.data(),dp,decoded.size()*2,cudaMemcpyDeviceToHost));CK(cudaFree(dp));
    for(int i=0;i<256*127;i++)for(int d=0;d<8;d++) {
        float f=fp4(((i%256)>>((d%2)*4))&15)*fp8(i/256);
        auto b=__float2bfloat16(f);unsigned short expected;memcpy(&expected,&b,2);
        if(expected!=decoded[i*8+d]){fprintf(stderr,"DECODE FAIL i=%d d=%d expected=%x actual=%x\n",i,d,expected,decoded[i*8+d]);return 3;}
    }
    puts("DECODE PASS 260096 values: all byte codes and finite nonnegative scale codes");
    test<24,4>(128,128,0,0,false,5);
    test<24,4>(128,128,8192,0,true,9);
    test<24,4>(128,117,8192,1,true,7);
    test<24,4>(65,49,2048,3,true,5);
    test<24,4>(128,128,39040,0,true,7);
    test<24,4>(128,128,204800,1,true,7);
    test<24,4>(128,128,512,2,true,3);
    test<24,4>(9,9,224,3,true,3);
    test<24,4>(9,9,224,0,true,3);
    test<24,4>(128,73,67,0,true,3);
    test<24,4>(128,128,131072,3,true,5);
    test<16,2>(33,29,219,1,true,3);
    test<16,2>(17,0,128,0,true,3);
    puts("ALL PASS");
}
