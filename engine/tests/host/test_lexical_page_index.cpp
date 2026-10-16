#include "models/qwen3_5/program/storage/lexical_page_index.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <unordered_set>

using ninfer::models::qwen3_5::detail::LexicalPageIndex;
using Tokens = std::vector<std::int32_t>;

// Independent reference: full token scan, token-valued sets/maps, full sort, as in v0.1.2.
std::vector<std::uint32_t> reference(std::span<const std::int32_t> ledger,
                                    std::uint32_t begin, std::uint32_t end,
                                    std::uint32_t budget) {
    if (budget == 0 || end <= begin || ledger.empty()) { return {}; }
    std::unordered_set<std::int32_t> query;
    for (auto token : ledger.last(std::min<std::size_t>(256,ledger.size()))) { query.insert(token); }
    std::vector<Tokens> pages;
    std::unordered_map<std::int32_t,std::uint32_t> df;
    for (auto p=begin;p<end;++p) {
        auto first=std::size_t(p)*64;
        if (first>=ledger.size()) { break; }
        Tokens matches;
        for(auto i=first;i<std::min(first+64,ledger.size());++i) {
            auto token=ledger[i];
            if(query.count(token) && std::find(matches.begin(),matches.end(),token)==matches.end()) {
                matches.push_back(token);
            }
        }
        for(auto token:matches) { ++df[token]; }
        pages.push_back(std::move(matches));
    }
    std::unordered_map<std::int32_t,double> weights;
    for(auto [t,f]:df) { weights[t]=std::log((double(pages.size())+1)/(double(f)+1))+1; }
    std::vector<std::pair<double,std::uint32_t>> scores;
    for(std::size_t i=0;i<pages.size();++i) {
        double s=0;
        for(auto token:pages[i]) { s+=weights[token]; }
        if(s>0) { scores.emplace_back(s,begin+std::uint32_t(i)); }
    }
    std::sort(scores.begin(),scores.end(),[](auto a,auto b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    if(scores.size()>budget) { scores.resize(budget); }
    std::vector<std::uint32_t> out;
    for(auto [s,p]:scores) { out.push_back(p); }
    std::sort(out.begin(),out.end());return out;
}

int main() {
    try {
        std::mt19937 rng(1641337);
        std::size_t cases=0;
        LexicalPageIndex cache;
        auto check=[&](const Tokens& ids,std::uint32_t b,std::uint32_t e,std::uint32_t n){
            if(cache.select(ids,b,e,n)!=reference(ids,b,e,n)) {
                throw std::runtime_error("incremental selection differs from independent full scan");
            }
            ++cases;
        };
        for(int trial=0;trial<100;++trial) {
            Tokens ids(2000+trial*79);
            for(auto& t:ids) { t=std::int32_t(rng()%(trial%2?31:5003))-5; }
            cache.reset();
            auto pages=std::uint32_t((ids.size()+63)/64);
            for(std::uint32_t e=1;e<=pages+2;e+=2) {
                for(auto n:{0u,1u,17u,96u,10000u}) { check(ids,0,e,n); }
            }
            check(ids,3,pages,17);check(ids,0,7,96);check(ids,0,pages,96);
            std::fill(ids.begin(),ids.end(),42);cache.reset();check(ids,0,pages,17);
            ids.resize(65);check(ids,0,2,96);
            for(int i=0;i<700;++i) { ids.push_back(42);check(ids,0,std::uint32_t((ids.size()+63)/64),17); }
            // Same-sized rewritten ledger requires the caller's explicit reset.
            for(auto& t:ids) { t=std::int32_t(rng()%97); } cache.reset();check(ids,1,13,96);
        }
        Tokens ids(204885);for(auto& t:ids) { t=std::int32_t(rng()%5003); }
        cache.reset();
        for(std::uint32_t e=192;e<=3202;e+=2) { check(ids,192,e,96); }
        auto bench=[&](bool indexed){
            cache.reset();std::uint64_t checksum=0;
            auto start=std::chrono::steady_clock::now();
            for(std::uint32_t e=194;e<=3202;e+=2) {
                auto out=indexed?cache.select(ids,192,e,96):reference(ids,192,e,96);
                for(auto p:out) { checksum+=p; }
            }
            auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::printf("%s_ms=%.3f checksum=%llu\n",indexed?"incremental":"full_scan",ms,(unsigned long long)checksum);
            return checksum;
        };
        if(bench(false)!=bench(true)) { throw std::runtime_error("benchmark checksum mismatch"); }
        std::printf("PASS %zu independent selections, ties, ranges, append, partial pages, query changes, reset\n",cases);
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 1; }
    return 0;
}
