#pragma once

#include "runtime/contract/request.h"
#include "runtime/contract/timing.h"
#include <compare>
#include <algorithm>
#include <span>

namespace ninfer::runtime {

struct LaneId {
    std::uint32_t value = 0;

    [[nodiscard]] friend constexpr bool operator==(LaneId, LaneId) noexcept  = default;
    [[nodiscard]] friend constexpr auto operator<=>(LaneId, LaneId) noexcept = default;
};

enum class ConsumeStatus : std::uint8_t {
    Consumed,
    InvariantMismatch,
};

enum class CommitDisposition : std::uint8_t {
    Active,
    Finishable,
    CancelledReleased,
};

// The product Engine only needs statistics for rows whose sequence is released by commit.
// Direct diagnostic callers may temporarily request cumulative snapshots for every row.
enum class CommitObservation : std::uint8_t {
    ReleasedRowsOnly,
    AllRows,
};

struct CommitDecision {
    std::uint32_t accepted_tokens = 0;
    bool terminal                 = false;
    bool cancelled                = false;
    // Copied unchanged from the corresponding OutputDecision; still relative to this row's
    // accepted span.
    std::optional<std::uint32_t> prefix_execution_split_after;
};

struct BeginSummary {
    std::uint32_t prompt_tokens        = 0;
    std::uint32_t reused_prompt_tokens = 0;
    PrefixReusePath prefix_reuse_path  = PrefixReusePath::Root;

    [[nodiscard]] friend constexpr bool operator==(BeginSummary, BeginSummary) noexcept = default;
};

struct GeneratedRound {
    std::span<const TokenId> tokens;
};

struct BatchedGeneratedRound {
    std::span<const TokenId> tokens;
    std::span<const std::int32_t> row_counts;
    std::uint32_t row_stride = 1;
    ExecutionTiming timing;
};

struct PrefillStepResult {
    BeginSummary summary;
    GeneratedRound round;
    std::uint32_t processed_prompt_tokens = 0;
    bool complete                         = false;
    ExecutionTiming timing;
    // On the completing step of a request that asked for them.
    std::optional<FirstTokenLogprobs> first_token_logprobs;
};

struct RoundBudget {
    std::uint32_t generated_tokens_remaining = 0;
    // A one-hot speculative proposal must not license a suffix after this token: a host-side
    // sampling transition has to commit before another target distribution is sampled.
    std::optional<TokenId> sampling_boundary_token;

    [[nodiscard]] std::span<const TokenId>
    drafts_before_sampling_boundary(std::span<const TokenId> drafts) const noexcept {
        if (!sampling_boundary_token || drafts.empty()) { return drafts; }
        const auto at = std::find(drafts.begin(), drafts.end(), *sampling_boundary_token);
        return drafts.first(static_cast<std::size_t>(at - drafts.begin()));
    }
};

} // namespace ninfer::runtime
