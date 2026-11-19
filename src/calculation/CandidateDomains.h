#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
namespace ppm {
// Reversible exclusions over immutable candidate arrays. A zero-sized domain is
// outside this search unit; the caller keeps its task at the current dates.
class CandidateDomains {
    std::vector<std::vector<std::uint8_t>> live_;
    std::vector<std::size_t> counts_;
    std::vector<std::pair<std::size_t, std::size_t>> trail_;

public:
    explicit CandidateDomains(const std::vector<std::size_t> &sizes) : counts_(sizes) {
        for (const auto size : sizes)
            live_.emplace_back(size, 1);
    }
    bool live(std::size_t task, std::size_t candidate) const {
        return live_[task][candidate] != 0;
    }
    std::size_t count(std::size_t task) const {
        return counts_[task];
    }
    std::size_t mark() const {
        return trail_.size();
    }
    void exclude(std::size_t task, std::size_t candidate) {
        if (live(task, candidate)) {
            live_[task][candidate] = 0;
            --counts_[task];
            trail_.emplace_back(task, candidate);
        }
    }
    void restore(std::size_t mark) {
        while (trail_.size() > mark) {
            const auto [task, candidate] = trail_.back();
            trail_.pop_back();
            live_[task][candidate] = 1;
            ++counts_[task];
        }
    }
};
} // namespace ppm
