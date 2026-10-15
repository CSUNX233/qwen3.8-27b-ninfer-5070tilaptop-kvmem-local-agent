// Exercise the public Ops at the model shapes selected for the local launch schedules.
// The oracle independently unpacks GGUF values on CPU and accumulates the represented BF16
// inputs in FP64. It neither calls the GPU dequantizer nor copies the vector kernel arithmetic.
#include "core/arena.h"
#include "core/tensor.h"
#include "core/weight.h"
#include "ninfer/ops/linear.h"
#include "ninfer/ops/linear_add.h"
#include "ninfer/ops/linear_swiglu.h"
#include "ops/linear/gguf/ggml_bridge.h"
#define GGML_COMMON_DECL_CPP
#define GGML_COMMON_IMPL_CPP
#include "ggml-common.h"
#include <cuda_fp16.h>
#include <cuda_bf16.h>
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace ninfer;
void checked(cudaError_t e) { if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e)); }
float half_at(const std::uint8_t* p) { __half h; std::memcpy(&h,p,2); return __half2float(h); }
std::uint32_t u32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v,p,4); return v; }
float unpack(int type,const std::uint8_t* b,int i) {
    if(type==10) {
        const int h=i/128, v=i%128;
        const unsigned sc=b[8*h+v/16];
        const unsigned q=(b[16+32*h+v%32]>>(2*(v/32)))&3;
        return half_at(b+80)*float((sc&15)*q)-half_at(b+82)*float(sc>>4);
    }
    const int sub=i/32, group=(i%32)/8, j=i%8;
    if(type==22) {
        const auto* qs=b+2;
        const unsigned index=qs[4*sub+group] | ((b[66+sub]<<(8-2*group))&0x300);
        const auto* grid=reinterpret_cast<const std::uint8_t*>(&iq2s_grid[index]);
        const unsigned sc=(b[74+sub]>>(4*(group/2)))&15;
        const unsigned sign=(qs[32+4*sub+group]>>j)&1;
        return half_at(b)*(0.5f+float(sc))*0.25f*float(grid[j])*(sign?-1.f:1.f);
    }
    if(type==18) {
        const auto* qs=b+2;
        const std::uint32_t a=u32(qs+64+4*sub);
        const unsigned encoded=(a>>(7*group))&127;
        unsigned sign=encoded;
        unsigned parity=0;
        for(unsigned z=encoded;z;z>>=1) parity^=z&1;
        sign|=parity<<7;
        const auto* grid=reinterpret_cast<const std::uint8_t*>(&iq3xxs_grid[qs[8*sub+2*group+j/4]]);
        return half_at(b)*(0.5f+float(a>>28))*0.5f*float(grid[j%4])*((sign&(1u<<j))?-1.f:1.f);
    }
    throw std::runtime_error("unqualified format");
}
Weight weight(QType type,const void* data,int rows,int k) {
    Weight w;
    w.qtype=type; w.layout=QuantLayout::GgufBlocks; w.qdata=w.payload=data;
    w.n=w.shape[0]=w.padded_shape[0]=rows; w.k=w.shape[1]=w.padded_shape[1]=k;
    w.ndim=2; w.group=w.group_size=256;
    return w;
}
void verify(const char* op,const std::vector<__nv_bfloat16>& got,const std::vector<double>& want,
            double budget=1) {
    double error=0,den=0,worst=0,peak=0;
    for(std::size_t i=0;i<want.size();++i) {
        const double v=__bfloat162float(got[i]), d=v-want[i];
        if(!std::isfinite(v) || !std::isfinite(want[i])) throw std::runtime_error("nonfinite");
        error+=d*d; den+=want[i]*want[i]; worst=std::max(worst,std::abs(d));
        peak=std::max(peak,std::abs(want[i]));
    }
    const double rms=std::sqrt(error/std::max(den,1e-30)), max=worst/std::max(peak,1e-30);
    std::cout<<op<<" rms="<<rms<<" max="<<max<<std::endl;
    if(rms>=0.02*budget || max>=0.08*budget) throw std::runtime_error("FP64 criterion");
}
int main(int argc,char** argv) try {
    if(argc!=4) throw std::runtime_error("usage: qualify model.ninfer matrices.txt output.bin");
    std::ofstream trace(argv[3],std::ios::binary);
    if(!trace) throw std::runtime_error("trace file");
    std::ifstream model(argv[1],std::ios::binary), metadata(argv[2]);
    if(!model || !metadata) throw std::runtime_error("fixture file");
    std::mt19937 rng(17512);
    std::normal_distribution<float> normal(0.f,0.25f);
    int type,rows,k; std::uint64_t offset,bytes; int checks=0;
    while(metadata>>type>>rows>>k>>offset>>bytes) {
        if(!((type==22 && rows!=248320) || (type==18 && rows==17408) ||
             (type==10 && (rows==17408 || rows==5120)))) continue;
        const QType qt=type==22?QType::GGUF_IQ2_S:type==18?QType::GGUF_IQ3_XXS:QType::GGUF_Q2_K;
        const int block_bytes=type==22?82:type==18?98:84;
        const std::size_t row_bytes=bytes/rows;
        std::vector<std::uint8_t> packed(bytes);
        model.seekg(offset); model.read(reinterpret_cast<char*>(packed.data()),bytes);
        if(!model) throw std::runtime_error("weight read");
        // Decode the real matrix once. All output rows are checked, including each grid tail.
        std::vector<float> exact(std::size_t(rows)*k);
        for(int r=0;r<rows;++r) for(int c=0;c<k;++c)
            exact[std::size_t(r)*k+c]=unpack(type,packed.data()+r*row_bytes+(c/256)*block_bytes,c%256);
        DeviceBuffer dw(bytes); dw.copy_from_host(packed.data(),bytes);
        const auto w=weight(qt,dw.p,rows,k);
        std::vector<__nv_bfloat16> x(std::size_t(k)*4),res(std::size_t(rows)*4);
        for(auto& v:x) v=__float2bfloat16(normal(rng));
        for(auto& v:res) v=__float2bfloat16(normal(rng));
        std::vector<float> xf(x.size());
        for(std::size_t i=0;i<x.size();++i) xf[i]=__bfloat162float(x[i]);
        DeviceBuffer dx(x.size()*2),dy(res.size()*2);
        dx.copy_from_host(x.data(),x.size()*2);
        auto up=w; // Same represented matrix as both operands; still tests both public products.
        const std::size_t cap=std::max({
            ops::linear_workspace_capacity_bytes(qt,rows,k,ops::LinearPolicy::A16Only,1,4),
            ops::linear_swiglu_pair_workspace_capacity_bytes(qt,qt,rows,k,ops::LinearPolicy::A16Only,1,4)});
        WorkspaceArena workspace(cap+4096);
        for(int t:{1,4}) {
            std::cout<<"type="<<type<<" rows="<<rows<<" k="<<k<<" t="<<t<<std::endl;
            Tensor input(dx.p,DType::BF16,{k,t}),output(dy.p,DType::BF16,{rows,t});
            std::vector<double> plain(std::size_t(rows)*t),want(plain.size());
            std::vector<__nv_bfloat16> got(plain.size());
            DeviceBuffer activation(ops::gguf::vector_activation_bytes(k,t));
            DeviceBuffer plane(plain.size()*sizeof(float));
            ops::gguf::quantize_vector_activation(static_cast<__nv_bfloat16*>(dx.p),k,t,nullptr,activation.p,nullptr);
            ops::gguf::vector_product(static_cast<ops::gguf::GgmlType>(type),dw.p,row_bytes,rows,k,activation.p,t,
                {nullptr,static_cast<float*>(plane.p),rows,ops::gguf::Epilogue::Store,nullptr,0},nullptr);
            checked(cudaDeviceSynchronize());
            std::vector<float> raw(plain.size()); plane.copy_to_host(raw.data(),raw.size()*sizeof(float));
            trace.write(reinterpret_cast<const char*>(raw.data()),raw.size()*sizeof(float));
            for(int col=0;col<t;++col) for(int r=0;r<rows;++r) {
                double sum=0;
                for(int c=0;c<k;++c) sum+=double(exact[std::size_t(r)*k+c])*xf[std::size_t(col)*k+c];
                plain[std::size_t(col)*rows+r]=sum;
            }
            ops::linear(input,w,output,ops::LinearPolicy::A16Only,workspace,nullptr);
            checked(cudaDeviceSynchronize()); dy.copy_to_host(got.data(),got.size()*2);
            verify("linear",got,plain); ++checks;
            trace.write(reinterpret_cast<const char*>(got.data()),got.size()*2);
            dy.copy_from_host(res.data(),got.size()*2);
            ops::linear_add(input,w,output,ops::LinearPolicy::A16Only,workspace,nullptr);
            checked(cudaDeviceSynchronize()); dy.copy_to_host(got.data(),got.size()*2);
            for(std::size_t i=0;i<want.size();++i) want[i]=plain[i]+__bfloat162float(res[i]);
            verify("linear_add",got,want); ++checks;
            trace.write(reinterpret_cast<const char*>(got.data()),got.size()*2);
            ops::linear_swiglu(input,w,up,output,ops::LinearPolicy::A16Only,workspace,nullptr);
            checked(cudaDeviceSynchronize()); dy.copy_to_host(got.data(),got.size()*2);
            for(std::size_t i=0;i<want.size();++i) want[i]=plain[i]/(1.+std::exp(-plain[i]))*plain[i];
            verify("linear_swiglu_pair",got,want,2); ++checks;
            trace.write(reinterpret_cast<const char*>(got.data()),got.size()*2);
        }
    }
    if(checks!=42) throw std::runtime_error("fixture coverage");
    std::cout<<"PASS "<<checks<<" public Op checks against independent CPU decode / FP64 oracle"<<std::endl;
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
