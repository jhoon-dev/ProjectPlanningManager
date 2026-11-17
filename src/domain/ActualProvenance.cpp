#include "domain/ActualProvenance.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <tuple>
#include <iterator>
namespace jobs {
std::map<std::string, ActualDerivation> actualDerivations(const Project &p) {
    using Key = std::tuple<std::string, std::string, std::string>;
    std::map<Key, std::map<std::string, const PeriodActualEntry *>> timelines;
    std::map<std::string, ActualDerivation> result;
    for (const auto &e : p.periodActuals) {
        calculationCheckpoint();
        auto &timeline = timelines[{e.taskId, e.resourceId, e.date}];
        auto it = timeline.upper_bound(e.statusAt);
        const auto *old = it == timeline.begin() ? nullptr : std::prev(it)->second;
        ActualDerivation d;
        if (proportionalWork(e))
            d.workSourceId = e.id;
        else if (old && e.workUnitMinutes && e.workUnitMinutes == old->workUnitMinutes)
            d.workSourceId = result.at(old->id).workSourceId;
        if (proportionalCost(e))
            d.costSourceId = e.id;
        else if (old && e.actualMinor && e.actualMinor == old->actualMinor)
            d.costSourceId = result.at(old->id).costSourceId;
        result[e.id] = std::move(d);
        timeline[e.statusAt] = &e;
    }
    return result;
}
} // namespace jobs
