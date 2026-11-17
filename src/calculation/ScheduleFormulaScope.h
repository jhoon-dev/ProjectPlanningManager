#pragma once
#include "domain/XmlSchedule.h"
#include "calculation/Scheduler.h"
namespace jobs {
enum class ScheduleFormulaMode { Inherit, Active, Plan };
std::shared_ptr<const XmlScheduleValues> makeScheduleFormulaValues(const Project &, const ScheduleResult *,
                                                                   const ScheduleResult *remaining = nullptr);
class ScheduleFormulaScope {
    std::optional<ScheduleResult> calculated_;
    std::optional<ScheduleResult> remainingCalculated_;
    std::shared_ptr<const XmlScheduleValues> values_;
    std::unique_ptr<XmlScheduleScope> scope_;

public:
    explicit ScheduleFormulaScope(const Project &, const ScheduleResult * = nullptr,
                                  ScheduleFormulaMode = ScheduleFormulaMode::Inherit, bool calculateMissing = true);
    const std::shared_ptr<const XmlScheduleValues> &values() const {
        return values_;
    }
};
} // namespace jobs
