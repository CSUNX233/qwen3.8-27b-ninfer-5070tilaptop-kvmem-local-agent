#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace ninfer::models::qwen3_5::detail {

// Request-local acceleration of the existing lexical fallback. The caller resets on request
// admission or destructive ledger edits; within that lifetime the ledger is append-only.
// Cache only token matches/counts, never scores: IDF is recomputed over the exact current
// document range, and matching tokens retain their ledger order for identical FP addition.
class LexicalPageIndex {
public:
    void reset() { *this = LexicalPageIndex{}; }

    std::vector<std::uint32_t> select(std::span<const std::int32_t> ledger,
                                      std::uint32_t begin, std::uint32_t end,
                                      std::uint32_t budget) {
        if (budget == 0 || end <= begin || ledger.empty()) { return {}; }
        constexpr std::size_t page_size = 64, query_window = 256;
        const auto query = ledger.last(std::min(query_window, ledger.size()));
        if (!valid_ || begin != begin_ || end < end_ || ledger.size() < ledger_size_ ||
            query.size() != query_.size() ||
            !std::equal(query.begin(), query.end(), query_.begin())) {
            reset();
            valid_ = true;
            begin_ = begin;
            end_ = begin;
            query_.assign(query.begin(), query.end());
            for (auto token : query) {
                auto [it, inserted] = query_ids_.emplace(token, query_ids_.size());
                if (inserted) { frequencies_.push_back(0); }
            }
        }
        // A final partial document can grow after an append. Remove its previous counts and
        // rebuild that document; complete documents already contain all 64 immutable tokens.
        if (!matches_.empty() && tail_tokens_ < page_size && ledger.size() > ledger_size_) {
            for (auto id : matches_.back()) { --frequencies_[id]; }
            matches_.pop_back();
        }
        for (std::uint32_t page = begin_ + static_cast<std::uint32_t>(matches_.size());
             page < end; ++page) {
            const std::size_t first = static_cast<std::size_t>(page) * page_size;
            if (first >= ledger.size()) { break; }
            const std::size_t last = std::min(first + page_size, ledger.size());
            std::vector<std::uint16_t> distinct;
            for (std::size_t i = first; i < last; ++i) {
                const auto found = query_ids_.find(ledger[i]);
                if (found == query_ids_.end()) { continue; }
                const auto id = static_cast<std::uint16_t>(found->second);
                if (std::find(distinct.begin(), distinct.end(), id) == distinct.end()) {
                    distinct.push_back(id);
                }
            }
            for (auto id : distinct) { ++frequencies_[id]; }
            matches_.push_back(std::move(distinct));
            tail_tokens_ = last - first;
        }
        end_ = end;
        ledger_size_ = ledger.size();
        const double documents = static_cast<double>(matches_.size());
        weights_.resize(frequencies_.size());
        for (std::size_t i = 0; i < weights_.size(); ++i) {
            weights_[i] = std::log((documents + 1.0) /
                                  (static_cast<double>(frequencies_[i]) + 1.0)) + 1.0;
        }
        scored_.clear();
        scored_.reserve(matches_.size());
        for (std::size_t i = 0; i < matches_.size(); ++i) {
            double score = 0.0;
            for (auto id : matches_[i]) { score += weights_[id]; }
            if (score > 0.0) { scored_.emplace_back(score, begin_ + static_cast<std::uint32_t>(i)); }
        }
        const auto better = [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        };
        const auto count = std::min<std::size_t>(budget, scored_.size());
        std::partial_sort(scored_.begin(), scored_.begin() + count, scored_.end(), better);
        std::vector<std::uint32_t> selected;
        selected.reserve(count);
        for (std::size_t i = 0; i < count; ++i) { selected.push_back(scored_[i].second); }
        std::sort(selected.begin(), selected.end());
        return selected;
    }

private:
    bool valid_ = false;
    std::uint32_t begin_ = 0, end_ = 0;
    std::size_t ledger_size_ = 0, tail_tokens_ = 0;
    std::vector<std::int32_t> query_;
    std::unordered_map<std::int32_t, std::size_t> query_ids_;
    std::vector<std::uint32_t> frequencies_;
    std::vector<std::vector<std::uint16_t>> matches_;
    std::vector<double> weights_;
    std::vector<std::pair<double, std::uint32_t>> scored_;
};

} // namespace ninfer::models::qwen3_5::detail
