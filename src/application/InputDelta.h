#pragma once
#include "domain/Project.h"
#include <algorithm>
#include <utility>

namespace ppm {
// Dynamic bytes owned by a value, excluding sizeof(value) and allocator overhead.
inline std::size_t inputExtraBytes(const NetworkPosition &v) {
    return v.taskId.size();
}
std::size_t inputExtraBytes(const Task &);
std::size_t inputExtraBytes(const WbsNode &);
std::size_t inputExtraBytes(const Resource &);
std::size_t inputExtraBytes(const Assignment &);
std::size_t inputExtraBytes(const TaskCost &);
std::size_t inputExtraBytes(const TaskCalendar &);
std::size_t inputExtraBytes(const CustomField &);
std::size_t inputExtraBytes(const TaskAttribute &);
std::size_t inputExtraBytes(const ActivityCodeType &);
std::size_t inputExtraBytes(const SavedTaskView &);
std::size_t inputExtraBytes(const SavedResourceView &);
inline std::size_t inputExtraBytes(const RemainingWorkPlan &v) {
    std::size_t n = v.taskId.size() + v.resourceId.size() + v.statusAt.size();
    for (const auto &d : v.days)
        n += sizeof(d) + d.date.size();
    return n;
}
inline std::size_t inputExtraBytes(const std::string &value) {
    return value.size();
}

template <class T> struct ValueDelta {
    std::optional<std::pair<T, T>> values;
    ValueDelta() = default;
    ValueDelta(const T &before, const T &after) {
        if (before != after)
            values.emplace(before, after);
    }
    void apply(T &target, bool forward) const {
        if (values)
            target = forward ? values->second : values->first;
    }
};

template <class T> class VectorDelta {
public:
    struct Span {
        std::size_t index;
        std::vector<T> before, after;
    };
    VectorDelta() = default;
    VectorDelta(const std::vector<T> &before, const std::vector<T> &after) {
        if (before.size() == after.size()) {
            // Separate runs keep distant cell edits from retaining intervening rows.
            for (std::size_t i = 0; i < before.size();) {
                if (before[i] == after[i]) {
                    ++i;
                    continue;
                }
                const auto begin = i++;
                while (i < before.size() && before[i] != after[i])
                    ++i;
                spans_.push_back(
                    {begin, {before.begin() + begin, before.begin() + i}, {after.begin() + begin, after.begin() + i}});
            }
        } else {
            std::size_t begin = 0, tail = 0;
            const auto common = std::min(before.size(), after.size());
            while (begin < common && before[begin] == after[begin])
                ++begin;
            while (tail < common - begin && before[before.size() - tail - 1] == after[after.size() - tail - 1])
                ++tail;
            spans_.push_back(
                {begin, {before.begin() + begin, before.end() - tail}, {after.begin() + begin, after.end() - tail}});
        }
    }
    void apply(std::vector<T> &target, bool forward) const {
        for (const auto &span : spans_) {
            const auto &removed = forward ? span.before : span.after;
            const auto &inserted = forward ? span.after : span.before;
            const auto position = target.begin() + std::ptrdiff_t(span.index);
            if (removed.size() == inserted.size())
                std::copy(inserted.begin(), inserted.end(), position);
            else {
                target.erase(position, position + std::ptrdiff_t(removed.size()));
                target.insert(target.begin() + std::ptrdiff_t(span.index), inserted.begin(), inserted.end());
            }
        }
    }
    std::size_t bytes() const {
        std::size_t total = spans_.size() * sizeof(Span);
        for (const auto &span : spans_) {
            for (const auto &value : span.before)
                total += sizeof(T) + inputExtraBytes(value);
            for (const auto &value : span.after)
                total += sizeof(T) + inputExtraBytes(value);
        }
        return total;
    }

private:
    std::vector<Span> spans_;
};

inline std::size_t inputExtraBytes(const ActivityDefinition &a) {
    std::size_t n = a.id.size() + a.name.size() + a.kind.size() + a.taskId.size() + a.startTaskId.size() +
                    a.finishTaskId.size() + a.wbsId.size() + a.calendarId.size();
    for (const auto &step : a.steps)
        n += sizeof(step) + step.id.size() + step.name.size();
    return n;
}
struct InputDelta {
    InputDelta(const Project &before, const Project &after);
    void apply(Project &target, bool forward) const;
    std::size_t bytes() const;

private:
    ValueDelta<std::string> name_, startDate_, calendar_, calculationProfile_, currency_, costProfile_;
    ValueDelta<std::optional<std::string>> statusAt_;
    ValueDelta<std::optional<PrintPreferences>> printPreferences_;
    VectorDelta<NetworkPosition> networkPositions_;
    VectorDelta<ActivityDefinition> activities_;
    ValueDelta<ResourcePlan> resourcePlan_;
    ValueDelta<std::optional<XmlWbsCodes>> xmlWbs_;
    VectorDelta<Task> tasks_;
    VectorDelta<std::string> holidays_;
    VectorDelta<WbsNode> wbsNodes_;
    VectorDelta<Resource> resources_;
    VectorDelta<Assignment> assignments_;
    VectorDelta<TaskCost> taskCosts_;
    VectorDelta<TaskCalendar> calendars_;
    VectorDelta<CustomField> customFields_, projectFields_, resourceFields_, assignmentFields_;
    VectorDelta<TaskAttribute> projectAttributes_, taskSummaryAttributes_;
    VectorDelta<ActivityCodeType> codeTypes_;
    VectorDelta<SavedTaskView> savedTaskViews_;
    VectorDelta<SavedResourceView> savedResourceViews_;
    VectorDelta<RemainingWorkPlan> remainingWork_;
    ValueDelta<std::optional<LevelingSelection>> planLeveling_, remainingLeveling_;
    ValueDelta<std::optional<ProgressPolicySelection>> progressPolicy_;
};
} // namespace ppm
