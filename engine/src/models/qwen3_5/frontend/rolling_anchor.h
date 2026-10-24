#pragma once

#include "models/qwen3_5/frontend/prepared_prompt.h"
#include <cstdint>
#include <cstdlib>

namespace ninfer::models::qwen3_5::detail {

// Opt-in catch-up for append-heavy conversations. The old backward grid can only
// nominate frontiers which an endpoint cache hit has already passed, so no state
// image is ever captured. Keep that grid for cold prefill, and additionally offer
// the last structural message boundary which the next append can still execute.
inline bool rolling_long_anchor_enabled() noexcept {
    const char* value = std::getenv("NINFER_C2_ROLLING_ANCHOR");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

inline bool is_rolling_long_anchor(const PreparedCacheOpportunity& opportunity) noexcept {
    // This evidence combination tags a private, automatically observed structural
    // candidate without changing PreparedPromptData/CaptureGroup ABI. It grants
    // no shared-cache standing; explicit private markers are never throttled.
    return opportunity.kind == PromptCacheMarkerKind::PrivateLongAnchor &&
           has_shared_candidate_evidence(opportunity.evidence,
                                         SharedCandidateEvidence::EngineStructural) &&
           has_shared_candidate_evidence(opportunity.evidence,
                                         SharedCandidateEvidence::EngineObserved) &&
           !has_shared_candidate_evidence(opportunity.evidence,
                                          SharedCandidateEvidence::ExplicitBoundary);
}

inline bool rolling_long_anchor_due(std::uint32_t frontier, std::uint32_t reuse_base,
                                     std::uint32_t spacing,
                                     std::uint32_t last_retained_frontier) noexcept {
    // Never replay an already-reused prefix just to obtain an optional checkpoint.
    // Use subtraction after ordering checks to keep large contexts overflow-safe.
    return spacing != 0 && frontier > reuse_base &&
           frontier >= last_retained_frontier &&
           frontier - last_retained_frontier >= spacing;
}

inline bool is_automatic_long_anchor(const PreparedCacheOpportunity& opportunity) noexcept {
    return opportunity.kind == PromptCacheMarkerKind::PrivateLongAnchor &&
           has_shared_candidate_evidence(opportunity.evidence,
                                         SharedCandidateEvidence::EngineStructural) &&
           !has_shared_candidate_evidence(opportunity.evidence,
                                          SharedCandidateEvidence::ExplicitBoundary);
}

inline bool protect_automatic_anchor_seed(bool automatic_candidate, std::size_t count,
                                         std::uint32_t latest_retained_frontier,
                                         std::uint32_t new_frontier,
                                         std::uint32_t spacing) noexcept {
    // Keep one early fallback when a forward automatic capture rotates a full set.
    // Otherwise small coverage-loss ties can move BOTH slots past a later prune.
    // This includes the backward grid: after a restore its now-reachable points
    // must not erase the seed before the next near-tail catch-up capture.
    // This only restricts normal replacement, never pressure reclamation. A
    // single-slot cache must remain free to advance rather than freeze forever.
    return automatic_candidate && spacing != 0 && count > 1 &&
           new_frontier > latest_retained_frontier;
}

template <typename Iterator>
inline Iterator select_rolling_anchor_seed(Iterator first, Iterator last,
                                           std::uint32_t minimum_frontier) noexcept {
    // A backward-grid point just beyond endpoint reuse can be only a few tokens
    // into history. Do not let that incidental point monopolize the fallback
    // slot while useful long checkpoints are repeatedly replaced.
    Iterator seed = last;
    for (auto it = first; it != last; ++it) {
        if (it->frontier < minimum_frontier) { continue; }
        if (seed == last || it->frontier < seed->frontier ||
            (it->frontier == seed->frontier && it->ordinal < seed->ordinal)) {
            seed = it;
        }
    }
    return seed;
}

} // namespace ninfer::models::qwen3_5::detail
