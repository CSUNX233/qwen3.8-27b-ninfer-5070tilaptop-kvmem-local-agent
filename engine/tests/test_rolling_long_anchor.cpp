#include "models/qwen3_5/frontend/rolling_anchor.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main() {
    using namespace ninfer;
    using namespace ninfer::models::qwen3_5;
    using namespace ninfer::models::qwen3_5::detail;
    int failures=0,checks=0;
    const auto check=[&](bool ok,const char* name){
        ++checks;
        if(!ok){std::cerr<<"FAIL "<<name<<'\n';++failures;}
    };
    check(!rolling_long_anchor_due(60766,208968,49152,0),"passed frontier is never replayed");
    check(rolling_long_anchor_due(52902,52599,49152,0),"first reachable anchor after long decode");
    check(!rolling_long_anchor_due(59710,59684,49152,52902),"short append does not churn state");
    check(rolling_long_anchor_due(102054,100000,49152,52902),"exact spacing boundary");
    check(!rolling_long_anchor_due(102053,100000,49152,52902),"below spacing boundary");
    check(!rolling_long_anchor_due(60000,60000,49152,0),"exact reuse frontier excluded");
    check(!rolling_long_anchor_due(60000,59000,0,0),"unspaced legacy mode unchanged");
    const auto max=std::numeric_limits<std::uint32_t>::max();
    check(rolling_long_anchor_due(max,max-1,49152,max-49152),"no addition overflow");
    check(!rolling_long_anchor_due(max-1,max-2,49152,max),"reversed frontier rejected");
    PreparedCacheOpportunity p{.kind=PromptCacheMarkerKind::PrivateLongAnchor,
        .evidence=SharedCandidateEvidence::EngineStructural|SharedCandidateEvidence::EngineObserved,
        .frontier=52902};
    check(is_rolling_long_anchor(p),"automatic rolling candidate detected");
    check(is_automatic_long_anchor(p),"rolling candidate preserves seed");
    p.evidence|=SharedCandidateEvidence::ExplicitBoundary;
    check(!is_rolling_long_anchor(p),"explicit marker never throttled");
    check(!is_automatic_long_anchor(p),"explicit marker replacement policy unchanged");
    p.evidence=SharedCandidateEvidence::EngineStructural;
    check(!is_rolling_long_anchor(p),"legacy grid never throttled");
    check(is_automatic_long_anchor(p),"backward grid also preserves restored seed");
    p.evidence=SharedCandidateEvidence::EngineStructural|SharedCandidateEvidence::EngineObserved;
    p.kind=PromptCacheMarkerKind::SharedStablePrefix;
    check(!is_rolling_long_anchor(p),"shared policy unchanged");
    check(protect_automatic_anchor_seed(true,2,103975,157291,49152),"early prune fallback survives rotation");
    check(!protect_automatic_anchor_seed(true,1,52902,103975,49152),"single-slot cache can advance");
    check(!protect_automatic_anchor_seed(false,2,103975,157291,49152),"explicit replacement unchanged");
    check(!protect_automatic_anchor_seed(true,2,157291,103975,49152),"non-forward capture has no pinned seed");
    check(protect_automatic_anchor_seed(true,2,4582,6380,512),"post-restore backward grid cannot replace early fallback");
    check(!protect_automatic_anchor_seed(true,2,103975,157291,0),"zero spacing retains legacy replacement policy");
    struct Anchor {std::uint32_t frontier,ordinal;};
    std::vector<Anchor> anchors{{72,1},{958,2}};
    auto seed=select_rolling_anchor_seed(anchors.begin(),anchors.end(),512);
    check(seed!=anchors.end()&&seed->ordinal==2,"tiny 72-token grid point cannot displace useful 958-token fallback");
    anchors={{72,1},{511,2}};
    check(select_rolling_anchor_seed(anchors.begin(),anchors.end(),512)==anchors.end(),"no useful seed leaves replacement unrestricted");
    anchors={{512,3},{1024,2}};
    seed=select_rolling_anchor_seed(anchors.begin(),anchors.end(),512);
    check(seed!=anchors.end()&&seed->ordinal==3,"spacing threshold is inclusive");
    anchors={{103975,8},{52902,5},{52902,4}};
    seed=select_rolling_anchor_seed(anchors.begin(),anchors.end(),49152);
    check(seed!=anchors.end()&&seed->ordinal==4,"production early seed is order independent with deterministic tie");
    anchors.clear();
    check(select_rolling_anchor_seed(anchors.begin(),anchors.end(),512)==anchors.end(),"empty set has no seed");
    std::cout<<"rolling anchor policy: "<<(failures?"FAIL":"PASS")<<" ("<<checks<<" checks)\n";
    return failures?1:0;
}
