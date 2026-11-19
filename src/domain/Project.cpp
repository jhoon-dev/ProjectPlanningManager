#include "domain/XmlFormula.h"
#include "domain/ResourceAvailability.h"
#include "domain/XmlBaselines.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "domain/Activities.h"
#include "domain/Diagnostics.h"
#include "domain/ExternalEvm.h"
#include "domain/FinancialPeriods.h"
#include "domain/XerScheduleAdoption.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "domain/Project.h"
#include "domain/PeriodActuals.h"
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
#include "domain/XmlRollDown.h"
#include "domain/XmlRollUp.h"
#include "domain/ResourceView.h"
#include "domain/RemainingWork.h"
#include "domain/Productivity.h"
#include "domain/CalculationControl.h"
#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace ppm {
// Keep the complete aggregate comparisons in one translation unit. Inlining them
// into every editor duplicates the expanding archive/custom-field model at -O3.
bool PlanInput::operator==(const PlanInput &) const = default;
bool Project::operator==(const Project &) const = default;
bool hasAssignmentPeriods(const PlanInput &p) {
    return std::any_of(p.assignments.begin(), p.assignments.end(), [](const auto &a) { return !a.periods.empty(); });
}
bool isEffort(const PlanInput &p) {
    return p.calculationProfile == effortProfile;
}
bool hasWorkAssignmentPeriods(const PlanInput &p) {
    std::unordered_set<std::string> work;
    for (const auto &t : p.tasks)
        if (t.taskType != TaskType::FixedDuration || t.workDriven)
            work.insert(t.id);
    return std::any_of(p.assignments.begin(), p.assignments.end(),
                       [&](const auto &a) { return !a.periods.empty() && work.contains(a.taskId); });
}
bool hasConstraints(const PlanInput &p) {
    return p.calculationProfile == constraintProfile || isEffort(p);
}
bool isPdm(const PlanInput &p) {
    return p.calculationProfile == pdmProfile || hasConstraints(p);
}
bool usesWork(const Task &t) {
    return t.taskType == TaskType::FixedWork || t.workDriven;
}

std::string engineFor(const PlanInput &p) {
    if (p.planLeveling && hasMaterialInventory(p))
        return materialFloatEngine;
    if (hasWorkAssignmentPeriods(p))
        return p.planLeveling ? "ppm-adopted-work-periods-1.0" : "ppm-effort-work-periods-1.0";
    if (hasAssignmentPeriods(p))
        return p.planLeveling ? "ppm-adopted-periods-1.0" : "ppm-effort-periods-1.0";
    if (p.planLeveling)
        return "ppm-adopted-leveling-1.8";
    return isEffort(p) ? "ppm-effort-1.1" : isPdm(p) ? "ppm-pdm-1.2" : "ppm-cpm-0.2";
}
std::string legacyEngineFor(const PlanInput &p) {
    if (p.planLeveling)
        return "ppm-adopted-leveling-1.0";
    return isEffort(p)         ? "ppm-effort-1.0"
           : hasConstraints(p) ? "ppm-pdm-1.1"
           : isPdm(p)          ? "ppm-pdm-1.0"
                               : "ppm-cpm-0.2";
}
bool supportsEngine(const PlanInput &p, const std::string &engine) {
    if (hasAssignmentPeriods(p))
        return engine == engineFor(p) ||
               (p.planLeveling && hasMaterialInventory(p) &&
                engine == (hasWorkAssignmentPeriods(p) ? "ppm-adopted-work-periods-1.0" : "ppm-adopted-periods-1.0"));
    return engine == engineFor(p) || engine == legacyEngineFor(p) ||
           (p.planLeveling && (engine == "ppm-adopted-leveling-1.1" || engine == "ppm-adopted-leveling-1.2" ||
                               engine == "ppm-adopted-leveling-1.3" || engine == "ppm-adopted-leveling-1.4" ||
                               engine == "ppm-adopted-leveling-1.5" || engine == "ppm-adopted-leveling-1.6" ||
                               engine == "ppm-adopted-leveling-1.7" || engine == "ppm-adopted-leveling-1.8"));
}
std::string levelingPolicyFor(const PlanInput &p) {
    if (p.resourcePlan.autoDelayForReceipts)
        return materialArrivalPolicy;
    if (hasWorkAssignmentPeriods(p))
        return "serial-effort-work-periods-v1";
    if (hasAssignmentPeriods(p))
        return "serial-effort-periods-v1";
    return isEffort(p)         ? "serial-effort-split-v2"
           : hasConstraints(p) ? "serial-no-split-constrained-v2"
           : isPdm(p)          ? "serial-no-split-pdm-v2"
                               : "serial-no-split-v1";
}
std::string legacyLevelingPolicyFor(const PlanInput &p) {
    return isEffort(p)         ? "serial-effort-split-v1"
           : hasConstraints(p) ? "serial-no-split-constrained-v1"
           : isPdm(p)          ? "serial-no-split-pdm-v1"
                               : "serial-no-split-v1";
}
bool supportsLevelingPolicy(const PlanInput &p, const std::string &policy) {
    if (p.resourcePlan.autoDelayRemainingForReceipts && policy == remainingMaterialPolicy)
        return true;
    if (p.resourcePlan.autoDelayForReceipts)
        return policy == materialArrivalPolicy;
    if (hasAssignmentPeriods(p))
        return policy == levelingPolicyFor(p) ||
               (isEffort(p) && (policy == datedRemainingPolicy || policy == datedEffortPolicy ||
                                (!hasWorkAssignmentPeriods(p) && policy == "serial-effort-work-periods-v1")));
    // Flat effort demand is the degenerate contour case, also used by historical decision metadata validation.
    return policy == levelingPolicyFor(p) || policy == legacyLevelingPolicyFor(p) ||
           (isEffort(p) && (policy == "serial-effort-periods-v1" || policy == "serial-effort-work-periods-v1" ||
                            policy == datedRemainingPolicy || policy == datedEffortPolicy));
}
void validateLevelingSelection(const PlanInput &p, const LevelingSelection &s) {
    if ((s.priority != "task-order" && s.priority != "shortest-first" && s.priority != "longest-first" &&
         !advancedPriority(s.priority)) ||
        s.horizonWorkDays < 1 || s.horizonWorkDays > 5200 || !supportsLevelingPolicy(p, s.policy) ||
        !validUtcStamp(s.adoptedAt) || s.reason.empty() || s.reason.size() > 4096 || s.sourceDocumentId.empty() ||
        !s.sourceRevision || s.sourceRevision > 9007199254740991ULL)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
}
void invalidateLevelingSelection(Project &p, const Project &before) {
    if (!p.planLeveling && !p.remainingLeveling)
        return;
    PlanInput a = p, b = before;
    a.planLeveling.reset();
    b.planLeveling.reset();
    // Descriptive provenance does not change resource demand or adopted placements.
    for (auto *input : {&a, &b}) {
        input->xmlBaselines.reset();
        for (auto &r : input->resources)
            r.productivity.reset();
        for (auto &assignment : input->assignments) {
            assignment.productivityEstimate.reset();
            assignment.nativeCurve.reset();
        }
    }
    if (a != b || p.statusAt != before.statusAt || p.progress != before.progress ||
        p.progressPolicy != before.progressPolicy || p.remainingWork != before.remainingWork ||
        p.materialActuals != before.materialActuals) {
        p.planLeveling.reset();
        p.remainingLeveling.reset();
    }
}

std::vector<Dependency> dependencies(const Task &task) {
    std::vector<Dependency> out;
    for (const auto &id : task.predecessors) {
        auto it = std::find_if(task.dependencyRules.begin(), task.dependencyRules.end(),
                               [&](const auto &d) { return d.predecessorId == id; });
        out.push_back(it == task.dependencyRules.end() ? Dependency{id, DependencyType::FS, 0} : *it);
    }
    return out;
}

bool validDate(const std::string &s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return false;
    const int y = std::stoi(s.substr(0, 4));
    return y >= 1 &&
           std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{unsigned(std::stoi(s.substr(5, 2)))},
                                       std::chrono::day{unsigned(std::stoi(s.substr(8, 2)))}}
               .ok();
}
static bool blank(const std::string &s) {
    return s.empty() || std::all_of(s.begin(), s.end(), [](unsigned char c) { return c <= ' '; });
}
void validateTaskName(const std::string &name) {
    if (blank(name))
        throw AppError(ErrorCode::TASK_ID_NAME_INVALID, {});
}
static std::int64_t civilMinute(const std::string &s) {
    const auto date = std::chrono::year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                      unsigned(std::stoi(s.substr(8, 2)));
    return std::int64_t(std::chrono::sys_days{date}.time_since_epoch().count()) * 1440 +
           std::stoi(s.substr(11, 2)) * 60 + std::stoi(s.substr(14, 2));
}
bool validStamp(const std::string &s) {
    if (s.size() != 16 || s[10] != ' ' || s[13] != ':' || !validDate(s.substr(0, 10)))
        return false;
    for (auto i : {11, 12, 14, 15})
        if (s[std::size_t(i)] < '0' || s[std::size_t(i)] > '9')
            return false;
    return std::stoi(s.substr(11, 2)) < 24 && std::stoi(s.substr(14, 2)) < 60;
}
bool validUtcStamp(const std::string &s) {
    if (s.size() != 20 || s[10] != 'T' || s[16] != ':' || s[19] != 'Z')
        return false;
    auto local = s.substr(0, 16);
    local[10] = ' ';
    return validStamp(local) && s[17] >= '0' && s[17] <= '5' && s[18] >= '0' && s[18] <= '9';
}
int currencyDecimals(const std::string &c) {
    if (c == "KRW" || c == "JPY")
        return 0;
    if (c == "USD" || c == "EUR")
        return 2;
    throw AppError(ErrorCode::CURRENCY_UNSUPPORTED, {});
}
void validatePlan(const PlanInput &p) {
    validateXmlBaselines(p);
    validateResourcePlanning(p);
    if (p.planLeveling && p.planLeveling->policy == remainingMaterialPolicy)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    if (p.planLeveling)
        validateLevelingSelection(p, *p.planLeveling);
    validateClassification(p);
    validateXmlFormulas(p);
    validateXmlRollDown(p);
    validateXmlRollUp(p);
    validateXmlWbsCodes(p);
    validateActivities(p);
    validateResourcePlan(p);
    currencyDecimals(p.currency);
    if (p.costProfile != "cost-day-minor-half-up-v1")
        throw AppError(ErrorCode::COST_POLICY_UNSUPPORTED, {});
    if (blank(p.name))
        throw AppError(ErrorCode::PROJECT_NAME_REQUIRED, {});
    if (!validDate(p.startDate))
        throw AppError(ErrorCode::PROJECT_START_DATE_INVALID, {});
    if (p.calendar != "standard-mon-fri-8h-v1")
        throw AppError(ErrorCode::CALENDAR_POLICY_UNSUPPORTED, {});
    if (p.calculationProfile != "native-fs0-workminutes-v1" && !isPdm(p))
        throw AppError(ErrorCode::CALCULATION_POLICY_UNSUPPORTED, {});
    std::unordered_set<std::string> calendarIds;
    for (const auto &c : p.calendars) {
        calculationCheckpoint();
        if (blank(c.id) || blank(c.name) || !calendarIds.insert(c.id).second || c.weekdays.empty() || c.periods.empty())
            throw AppError(ErrorCode::CALENDAR_DEFINITION_INVALID, {}, ErrorContext{.calendarIds = {c.id}});
        std::unordered_set<int> weekdays;
        for (int day : c.weekdays)
            if (day < 1 || day > 7 || !weekdays.insert(day).second)
                throw AppError(ErrorCode::CALENDAR_WEEKDAYS_INVALID, {}, ErrorContext{.calendarIds = {c.id}});
        int end = -1;
        for (const auto &period : c.periods) {
            if (period.begin < 0 || period.end > 1440 || period.begin >= period.end || period.begin < end)
                throw AppError(ErrorCode::CALENDAR_PERIODS_INVALID, {}, ErrorContext{.calendarIds = {c.id}});
            end = period.end;
        }
        std::unordered_set<std::string> dates;
        for (const auto &d : c.holidays)
            if (!validDate(d) || !dates.insert(d).second)
                throw AppError(ErrorCode::CALENDAR_HOLIDAYS_INVALID, {}, ErrorContext{.calendarIds = {c.id}});
    }
    if (!p.xmlFormulaCalendarId.empty() && !calendarIds.contains(p.xmlFormulaCalendarId))
        throw AppError(ErrorCode::CALENDAR_NOT_FOUND, {}, ErrorContext{.calendarIds = {p.xmlFormulaCalendarId}});
    if (!p.calendars.empty() && !isPdm(p))
        throw AppError(ErrorCode::CALENDAR_REQUIRES_PDM, {});
    std::unordered_set<std::string> holidays;
    for (const auto &date : p.holidays)
        if (!validDate(date) || !holidays.insert(date).second)
            throw AppError(ErrorCode::PROJECT_HOLIDAYS_INVALID, {});
    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, std::size_t> nodes;
    for (std::size_t i = 0; i < p.wbsNodes.size(); ++i) {
        const auto &node = p.wbsNodes[i];
        if (blank(node.id) || blank(node.name) || !nodes.emplace(node.id, i).second)
            throw AppError(ErrorCode::WBS_ID_NAME_INVALID, {});
    }
    for (const auto &node : p.wbsNodes)
        if (!node.parentId.empty() && !nodes.contains(node.parentId))
            throw AppError(ErrorCode::WBS_PARENT_NOT_FOUND, {});
    std::vector<int> color(p.wbsNodes.size(), 0);
    for (std::size_t i = 0; i < p.wbsNodes.size(); ++i) {
        auto j = i;
        std::vector<std::size_t> chain;
        while (color[j] == 0) {
            color[j] = 1;
            chain.push_back(j);
            const auto &parent = p.wbsNodes[j].parentId;
            if (parent.empty())
                break;
            j = nodes.at(parent);
            if (color[j] == 1)
                throw AppError(ErrorCode::WBS_CYCLE, {});
        }
        for (auto index : chain)
            color[index] = 2;
    }
    for (const auto &t : p.tasks) {
        calculationCheckpoint();
        validateTaskName(t.name);
        if (blank(t.id) || !ids.insert(t.id).second || nodes.contains(t.id))
            throw AppError(ErrorCode::TASK_ID_NAME_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        if (!t.wbsId.empty() && !nodes.contains(t.wbsId))
            throw AppError(ErrorCode::TASK_WBS_NOT_FOUND, {}, ErrorContext{.taskIds = {t.id}});
        if (t.durationMinutes && (*t.durationMinutes < 0 || *t.durationMinutes > 5000000))
            throw AppError(ErrorCode::TASK_DURATION_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        std::unordered_set<std::string> refs;
        for (const auto &id : t.predecessors)
            if (blank(id) || !refs.insert(id).second)
                throw AppError(ErrorCode::PREDECESSOR_IDS_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        if (!t.calendarId.empty() && !calendarIds.contains(t.calendarId))
            throw AppError(ErrorCode::CALENDAR_NOT_FOUND, {}, ErrorContext{.taskIds = {t.id}}, "CALENDAR_NOT_FOUND_3");
        std::unordered_set<std::string> rules;
        for (const auto &d : t.dependencyRules) {
            if (!refs.contains(d.predecessorId) || !rules.insert(d.predecessorId).second || !isValid(d.type) ||
                d.lagMinutes < -5000000 || d.lagMinutes > 5000000 ||
                (d.type == DependencyType::FS && d.lagMinutes == 0))
                throw AppError(ErrorCode::DEPENDENCY_RULE_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        }
        if ((!t.dependencyRules.empty() || !t.calendarId.empty()) && !isPdm(p))
            throw AppError(ErrorCode::DEPENDENCY_REQUIRES_PDM, {}, ErrorContext{.taskIds = {t.id}});
        const bool dated = t.constraintType != ConstraintType::ASAP && t.constraintType != ConstraintType::ALAP;
        if (!isValid(t.constraintType) || dated != t.constraintAt.has_value() ||
            (t.constraintAt && !validStamp(*t.constraintAt)) || (t.deadlineAt && !validStamp(*t.deadlineAt)))
            throw AppError(ErrorCode::CONSTRAINT_INPUT_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        if ((t.constraintType != ConstraintType::ASAP || t.constraintAt || t.deadlineAt) && !hasConstraints(p))
            throw AppError(ErrorCode::CONSTRAINT_REQUIRES_POLICY, {}, ErrorContext{.taskIds = {t.id}});
        if (!isValid(t.taskType) || (t.workUnitMinutes && (*t.workUnitMinutes < 0 || *t.workUnitMinutes > maxWork)) ||
            (t.taskType == TaskType::FixedDuration && (t.workDriven || t.workUnitMinutes)) ||
            (t.taskType == TaskType::FixedWork && !t.workDriven) || (t.workDriven != t.workUnitMinutes.has_value()))
            throw AppError(ErrorCode::TASK_EFFORT_INVALID, {}, ErrorContext{.taskIds = {t.id}});
        std::string last;
        if (t.breaks.size() > 1000)
            throw AppError(ErrorCode::TASK_BREAK_LIMIT, {}, ErrorContext{.taskIds = {t.id}});
        for (const auto &gap : t.breaks) {
            if (!validStamp(gap.start) || !validStamp(gap.finish) || gap.start >= gap.finish ||
                (!last.empty() && gap.start < last))
                throw AppError(ErrorCode::TASK_BREAK_INVALID, {}, ErrorContext{.taskIds = {t.id}});
            last = gap.finish;
        }
        if ((t.taskType != TaskType::FixedDuration || t.workDriven || !t.breaks.empty()) && !isEffort(p))
            throw AppError(ErrorCode::TASK_EFFORT_REQUIRES_POLICY, {}, ErrorContext{.taskIds = {t.id}});
    }
    std::unordered_set<std::string> resources, pairs;
    const auto validUnits = [](const std::optional<int> &n) { return !n || (*n >= 0 && *n <= 100000); };
    std::size_t availabilityCount = 0;
    for (const auto &r : p.resources) {
        calculationCheckpoint();
        validateResourceAvailability(r);
        availabilityCount += r.availability.size();
        if (availabilityCount > 100000)
            throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
        if (!r.calendarId.empty() && (!isEffort(p) || !calendarIds.contains(r.calendarId)))
            throw AppError(ErrorCode::RESOURCE_CALENDAR_INVALID, {}, ErrorContext{.resourceIds = {r.id}});
        if (blank(r.id) || blank(r.name) || !resources.insert(r.id).second || ids.contains(r.id) ||
            nodes.contains(r.id) || (r.type != "labor" && r.type != "equipment") || !validUnits(r.maxUnits))
            throw AppError(ErrorCode::RESOURCE_INPUT_INVALID, {}, ErrorContext{.resourceIds = {r.id}});
        std::unordered_set<std::string> dates;
        for (const auto &d : r.exceptions)
            if (!validDate(d.date) || !dates.insert(d.date).second || !validUnits(d.units))
                throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID, {}, ErrorContext{.resourceIds = {r.id}});
        dates.clear();
        for (const auto &rate : r.rates)
            if (!validDate(rate.effectiveDate) || !dates.insert(rate.effectiveDate).second ||
                (rate.hourlyMinor && (*rate.hourlyMinor < 0 || *rate.hourlyMinor > 1000000000LL)))
                throw AppError(ErrorCode::RESOURCE_RATE_INVALID, {}, ErrorContext{.resourceIds = {r.id}});
    }
    std::unordered_map<std::string, const Task *> assignmentTasks;
    for (const auto &t : p.tasks)
        assignmentTasks[t.id] = &t;
    std::size_t periodCount = 0;
    for (const auto &a : p.assignments) {
        calculationCheckpoint();
        // Length prefix avoids collisions even if imported IDs contain separators.
        auto pair = std::to_string(a.taskId.size()) + ":" + a.taskId + a.resourceId;
        if (!ids.contains(a.taskId) || !resources.contains(a.resourceId) || a.units <= 0 || a.units > 100000 ||
            !pairs.insert(pair).second)
            throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID, {});
        if (!a.periods.empty()) {
            const auto *task = assignmentTasks.at(a.taskId);
            if (!isEffort(p) || (usesWork(*task) ? !task->workUnitMinutes.has_value()
                                                 : (!task->durationMinutes || *task->durationMinutes <= 0)))
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"기간 또는 공수가 확정된 작업만 지원합니다."});
            periodCount += a.periods.size();
            if (a.periods.size() > 1024 || periodCount > 100000)
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                               {"배정당 1,024개, 계획당 100,000개 구간까지 지원합니다."});
            std::int64_t end = 0;
            for (const auto &v : a.periods) {
                calculationCheckpoint();
                if (v.fromMinute < end || v.fromMinute < 0 || v.toMinute <= v.fromMinute || v.toMinute > 5000000 ||
                    v.units < 0 || v.units > 100000)
                    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                                   {"구간은 정렬·비중복, 0~5,000,000 근무분, 배정률 0~100,000%여야 합니다."});
                end = v.toMinute;
            }
        }
    }
    std::unordered_set<std::string> costs;
    for (const auto &c : p.taskCosts)
        if (!ids.contains(c.taskId) || !costs.insert(c.taskId).second ||
            (c.fixedMinor && (*c.fixedMinor < 0 || *c.fixedMinor > maxMoney)) ||
            (c.accrual != "uniform" && c.accrual != "start" && c.accrual != "finish") ||
            (c.credit != "physical" && c.credit != "zero-hundred"))
            throw AppError(ErrorCode::TASK_COST_INVALID, {});
}
void validateProgressPolicySelection(const ProgressPolicySelection &s) {
    if ((s.policy != retainedProgressPolicy && s.policy != overrideProgressPolicy &&
         s.policy != actualDatesProgressPolicy) ||
        blank(s.adoptedBy) || s.adoptedBy.size() > 1024 || blank(s.reason) || s.reason.size() > 4096 ||
        !validUtcStamp(s.adoptedAt) || blank(s.sourceDocumentId) || s.sourceDocumentId.size() > 1024 ||
        !s.sourceRevision || s.sourceRevision > 9007199254740991ULL)
        throw AppError(ErrorCode::PROGRESS_POLICY_INVALID);
}
void validate(const Project &p) {
    validateCurrencyConversions(p);
    validateLedgerIdMigrations(p);
    validateXerScheduleAdoptions(p);
    validateXmlBaselineMerges(p);
    if (p.xerFinancials)
        validateXerFinancials(*p.xerFinancials);
    validateXerFinancialRevisions(p);
    validateExternalEvmAdoptions(p);
    validateFinancialPeriods(p);
    validatePresentation(p);
    validateStepReports(p);
    validateLevelingDecisions(p);
    if (p.progressPolicy) {
        validateProgressPolicySelection(*p.progressPolicy);
        if (p.progressPolicy->sourceRevision > p.revision)
            throw AppError(ErrorCode::PROGRESS_POLICY_INVALID);
    }
    if (p.planLeveling && p.statusAt)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    if (p.remainingLeveling) {
        if (!p.statusAt || p.planLeveling)
            throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
        validateLevelingSelection(p, *p.remainingLeveling);
    }
    validatePlan(p);
    validateSavedTaskViews(p);
    validateSavedResourceViews(p);
    validateRemainingWork(p);
    if (p.remainingLeveling && !supportsLevelingPolicy(p, p.remainingLeveling->policy))
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    if (p.planLeveling &&
        (p.planLeveling->policy == datedRemainingPolicy || p.planLeveling->policy == datedEffortPolicy))
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    if (blank(p.documentId) || p.revision == 0 || p.revision > 9007199254740991ULL)
        throw AppError(ErrorCode::PROJECT_ID_REVISION_INVALID, {});
    if (p.statusAt && !validStamp(*p.statusAt))
        throw AppError(ErrorCode::STATUS_DATE_INVALID, {});
    std::unordered_set<std::string> baselineIds;
    for (std::size_t i = 0; i < p.baselines.size(); ++i) {
        calculationCheckpoint();
        const auto &b = p.baselines[i];
        validatePlan(b.input);
        if (blank(b.id) || !baselineIds.insert(b.id).second || blank(b.name) || blank(b.approvedBy) ||
            blank(b.reason) || !validUtcStamp(b.createdAt) || blank(b.sourceDocumentId) || b.inputRevision == 0 ||
            b.inputRevision >= p.revision || b.version != i + 1 || !supportsEngine(b.input, b.engineVersion) ||
            !validStamp(b.projectFinish) || b.schedule.size() != b.input.tasks.size())
            throw AppError(ErrorCode::BASELINE_METADATA_INVALID, {});
        std::unordered_set<std::string> taskIds;
        for (const auto &t : b.input.tasks)
            taskIds.insert(t.id);
        for (const auto &row : b.schedule) {
            calculationCheckpoint();
            if (!taskIds.erase(row.taskId) || !validStamp(row.start) || !validStamp(row.finish) ||
                row.start > row.finish || row.completedActual || row.remainingStart)
                throw AppError(ErrorCode::BASELINE_TASKS_MISMATCH, {});
        }
    }
    std::unordered_set<std::string> taskIds;
    for (const auto &t : p.tasks)
        taskIds.insert(t.id);
    std::unordered_map<std::string, std::string> previous;
    std::unordered_set<std::string> entryIds;
    for (const auto &e : p.progress) {
        calculationCheckpoint();
        if (e.remainingWorkUnitMinutes && !isEffort(p))
            throw AppError(ErrorCode::REMAINING_WORK_REQUIRES_POLICY, {});
        if (e.remainingWorkUnitMinutes && (*e.remainingWorkUnitMinutes < 0 || *e.remainingWorkUnitMinutes > maxWork ||
                                           (e.actualFinish && *e.remainingWorkUnitMinutes != 0)))
            throw AppError(ErrorCode::REMAINING_WORK_INVALID, {});
        if (blank(e.id) || !entryIds.insert(e.id).second || !taskIds.contains(e.taskId) || !validStamp(e.statusAt) ||
            !validUtcStamp(e.recordedAt) || blank(e.reporter) || blank(e.reason) || blank(e.sourceDocumentId) ||
            e.recordedRevision == 0 || e.recordedRevision > p.revision || e.actualDurationMinutes < 0 ||
            e.actualDurationMinutes > 5000000 || e.remainingMinutes < 0 || e.remainingMinutes > 5000000 ||
            (e.planDurationMinutes && (*e.planDurationMinutes < 0 || *e.planDurationMinutes > 5000000)) ||
            e.previousEntryId != previous[e.taskId])
            throw AppError(ErrorCode::PROGRESS_METADATA_INVALID, {});
        if ((e.actualStart && (!validStamp(*e.actualStart) || *e.actualStart > e.statusAt)) ||
            (e.actualFinish && (!validStamp(*e.actualFinish) || !e.actualStart || *e.actualFinish < *e.actualStart ||
                                *e.actualFinish > e.statusAt)) ||
            (!e.actualStart && e.actualDurationMinutes != 0) || (e.actualFinish && e.remainingMinutes != 0) ||
            (e.actualStart && !e.actualFinish && e.remainingMinutes == 0 && !e.remainingWorkUnitMinutes))
            throw AppError(ErrorCode::PROGRESS_DATES_INVALID, {});
        if (!e.actualStart && e.remainingMinutes == 0 && e.planDurationMinutes.value_or(1) != 0 &&
            e.remainingWorkUnitMinutes.value_or(0) == 0)
            throw AppError(ErrorCode::PROGRESS_COMPLETION_REQUIRED, {});
        if (e.actualStart &&
            e.actualDurationMinutes > civilMinute(e.actualFinish.value_or(e.statusAt)) - civilMinute(*e.actualStart))
            throw AppError(ErrorCode::PROGRESS_DURATION_INVALID, {});
        previous[e.taskId] = e.id;
    }
    std::unordered_set<std::string> scenarios;
    for (const auto &s : p.scenarios) {
        calculationCheckpoint();
        if (blank(s.id) || !scenarios.insert(s.id).second || blank(s.name) || !validUtcStamp(s.createdAt) ||
            blank(s.sourceDocumentId) || !s.inputRevision || s.inputRevision >= p.revision ||
            !supportsLevelingPolicy(s.input, s.policy) ||
            (s.priority != "task-order" && s.priority != "shortest-first" && s.priority != "longest-first" &&
             !advancedPriority(s.priority)) ||
            s.horizonWorkDays < 1 || s.horizonWorkDays > 5200)
            throw AppError(ErrorCode::SCENARIO_METADATA_INVALID, {});
        Project snapshot;
        static_cast<PlanInput &>(snapshot) = s.input;
        snapshot.documentId = s.sourceDocumentId;
        snapshot.revision = s.inputRevision;
        snapshot.statusAt = s.statusAt;
        snapshot.progress = s.progress;
        snapshot.progressPolicy = s.progressPolicy;
        snapshot.remainingWork = s.remainingWork;
        snapshot.materialActuals = s.materialActuals;
        if (s.policy == remainingMaterialPolicy && !supportsLevelingPolicy(snapshot, s.policy))
            throw AppError(ErrorCode::SCENARIO_METADATA_INVALID);
        validate(snapshot);
    }
    previous.clear();
    entryIds.clear();
    for (const auto &e : p.costEntries) {
        calculationCheckpoint();
        if (blank(e.id) || !entryIds.insert(e.id).second || !taskIds.contains(e.taskId) || !validStamp(e.statusAt) ||
            !validUtcStamp(e.recordedAt) || blank(e.reporter) || blank(e.reason) || blank(e.sourceDocumentId) ||
            e.currency != p.currency || !e.recordedRevision || e.recordedRevision > p.revision ||
            e.previousEntryId != previous[e.taskId] ||
            (e.actualMinor && (*e.actualMinor < 0 || *e.actualMinor > maxMoney)) ||
            (e.physicalBasisPoints && (*e.physicalBasisPoints < 0 || *e.physicalBasisPoints > 10000)))
            throw AppError(ErrorCode::COST_REPORT_INVALID, {});
        previous[e.taskId] = e.id;
    }
    validatePeriodActuals(p);
    validateMaterialActuals(p);
}
const ProgressEntry *progressAsOf(const Project &p, const std::string &taskId) {
    if (!p.statusAt)
        return nullptr;
    const ProgressEntry *result = nullptr;
    for (const auto &entry : p.progress)
        if (entry.taskId == taskId && entry.statusAt <= *p.statusAt && (!result || result->statusAt <= entry.statusAt))
            result = &entry;
    return result;
}
const CostEntry *costAsOf(const Project &p, const std::string &taskId, const std::string &stamp) {
    const CostEntry *result = nullptr;
    for (const auto &e : p.costEntries)
        if (e.taskId == taskId && e.statusAt <= stamp && (!result || e.statusAt >= result->statusAt))
            result = &e;
    return result;
}
} // namespace ppm
