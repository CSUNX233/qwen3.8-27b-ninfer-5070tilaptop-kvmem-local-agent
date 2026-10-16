#include "core/device.h"
#include "models/qwen3_5/state/decoder_state.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>

int main() {
    try {
#ifdef _WIN32
        _putenv_s("NINFER_SELECTION_DIFF","1");
#else
        setenv("NINFER_SELECTION_DIFF","1",1);
#endif
        using namespace ninfer;
        using namespace ninfer::models::qwen3_5;
        LayoutBuilder builder;
        auto layout=plan_decoder_state(builder,{.full_attention_layers=1,.capacity=262144,
            .kv_heads=4,.attention_head_dim=256,.kv_storage=KvCacheStorage::RotatedLloyd4KeyInt4Value,
            .text_physical_page_groups=2});
        DeviceArena arena(builder.finish(256));
        PagedKVCache cache(DeviceSpan{arena.base(),arena.capacity()},layout.text_kv);
        auto* stable=cache.batch_layer_view(0).selected_blocks;
        std::mt19937 rng(161616);
        std::vector<std::uint32_t> expected(128,~0U);
        int cases=0;
        for(int i=0;i<1000;++i) {
            if(i%7==0) { cache.clear_selection(); }
            if(i%3==0) { expected[rng()%expected.size()]=rng(); }
            if(i%17==0) { expected.resize(1+rng()%128,0); }
            cache.install_selection(expected);
            auto* ptr=cache.batch_layer_view(0).selected_blocks;
            if(ptr!=stable) { throw std::runtime_error("selection device pointer moved"); }
            std::vector<std::uint32_t> got(expected.size());
            CUDA_CHECK(cudaMemcpy(got.data(),ptr,got.size()*4,cudaMemcpyDeviceToHost));
            if(got!=expected) { throw std::runtime_error("installed selection differs from CPU oracle"); }
            ++cases;
        }
        std::printf("PASS %d bitmap updates, duplicate, diff, resize, clear/re-enable, stable pointer\n",cases);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
    return 0;
}
