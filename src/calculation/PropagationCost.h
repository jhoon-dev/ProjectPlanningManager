#pragma once
#include <algorithm>
#include <cstdint>
namespace ppm {
// Deterministic work estimates, not elapsed-time predictions. Decisions affect
// optional pruning only; the exact search and native certificate remain required.
struct PropagationCost {
    using Count = std::uint64_t;
    static constexpr Count ceiling = 2000000;
    Count movable = 0, candidates = 0, spans = 0, edges = 0;
    static Count capped(Count value) {
        return std::min(ceiling, value);
    }
    Count sweep() const {
        return capped(candidates + 3 * spans + edges);
    }
    Count repeatSavings() const {
        return capped(movable * (candidates > movable ? candidates - movable : 0));
    }
    bool worthwhile() const {
        return movable > 1 && repeatSavings() >= 4 * std::max<Count>(1, sweep());
    }
};
class PropagationDecision {
    PropagationCost::Count failures_ = 0, completed_ = 0, next_;
    bool active_, exact_;
    bool remainingPays() const {
        const auto rate = (failures_ + completed_) / (completed_ + 1);
        return (cost.movable - completed_) * rate >= 2 * std::max<PropagationCost::Count>(1, cost.sweep());
    }

public:
    PropagationCost cost;
    explicit PropagationDecision(PropagationCost value, bool exact = true)
        : next_(std::max<PropagationCost::Count>(64, value.sweep())), active_(exact && value.worthwhile()),
          exact_(exact), cost(value) {}
    bool needsEstimate() const {
        return !exact_ && failures_ >= next_ && remainingPays();
    }
    void refine(PropagationCost value) {
        cost = value;
        next_ = std::max<PropagationCost::Count>(64, value.sweep());
        active_ = value.worthwhile();
        exact_ = true;
    }
    void failedProbe() {
        failures_ = PropagationCost::capped(failures_ + 1);
    }
    void completedGoal() {
        completed_ = std::min(cost.movable, completed_ + 1);
    }
    // Returns true only on an observed-work activation, not on every invocation.
    bool observe() {
        if (exact_ && !active_ && failures_ >= next_) {
            // Past failures are sunk work. Require enough projected work in
            // the remaining goals to amortize a new sweep before activating.
            if (!remainingPays())
                return false;
            active_ = true;
            return true;
        }
        return false;
    }
    bool active() const {
        return active_;
    }
    bool feedback(PropagationCost::Count checks, PropagationCost::Count exclusions) {
        if (checks && exclusions * 8 < checks) {
            active_ = false;
            next_ = failures_ + std::max<PropagationCost::Count>(256, 4 * cost.sweep());
            return true;
        }
        return false;
    }
};
} // namespace ppm
