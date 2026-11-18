#pragma once
#include "domain/Project.h"
#include <map>
#include <set>
#include <functional>
namespace jobs {
struct XmlResourceLoadValue {
    std::optional<std::int64_t> units;
    std::string issue;
    bool missing = false;
};
struct XmlScheduleValues {
    std::function<XmlResourceLoadValue(const std::string &, const std::string &, bool)> resourceLoad;
    std::string resourceLoadIssue;
    std::map<std::string, XmlBaselineRow> externalBaselineRows;
    std::shared_ptr<const PlanInput> baselineDurationCalendar;
    std::optional<std::string> actualStatusAt;
    std::map<std::string, std::pair<std::string, std::string>> remainingTasks;
    std::set<std::string> remainingCompleted;
    std::map<int, std::shared_ptr<const XmlScheduleValues>> numberedBaselines;
    std::map<std::string, std::pair<std::string, std::string>> earlyTasks, lateTasks;
    std::optional<std::string> earlyStart, earlyFinish, lateStart, lateFinish;
    std::set<std::string> earlyLateCompleted;
    std::string earlyLateIssue, lateIssue;
    std::map<std::string, std::pair<std::string, std::string>> baselineTasks;
    std::optional<std::string> baselineStart, baselineFinish;
    std::map<std::string, std::int64_t> baselineDurations, baselineTaskWork, baselineResourceWork;
    std::map<std::pair<std::string, std::string>, std::int64_t> baselineAssignmentWork;
    std::map<std::pair<std::string, std::string>, std::pair<std::string, std::string>> baselineAssignmentDates;
    std::optional<std::int64_t> baselineWork;
    std::map<std::string, std::optional<std::int64_t>> baselineTaskCost, baselineResourceCost;
    std::map<std::pair<std::string, std::string>, std::optional<std::int64_t>> baselineAssignmentCost;
    std::optional<std::int64_t> baselineCost;
    std::string baselineCostIssue;
    std::string baselineCaption, baselineIssue;
    std::map<std::string, std::pair<std::string, std::string>> tasks;
    std::optional<std::string> start, finish;
    std::map<std::string, std::pair<std::optional<std::string>, std::optional<std::string>>> actualTasks;
    std::optional<std::string> actualStart, actualFinish;
    std::map<std::string, std::int64_t> actualDurations, actualTaskWork, actualResourceWork;
    std::map<std::pair<std::string, std::string>, std::int64_t> actualAssignmentWork;
    std::optional<std::int64_t> actualWork;
    std::string actualWorkIssue;
    std::string actualCaption, actualIssue;
    std::map<std::string, std::int64_t> remainingDurations, remainingTaskWork, remainingResourceWork;
    std::map<std::pair<std::string, std::string>, std::int64_t> remainingAssignmentWork;
    std::optional<std::int64_t> remainingWork;
    std::string remainingCaption, remainingIssue;
    std::string cacheKey, caption, issue;
    bool resourceLoadMissing = true;
    bool externalBaseline = false;
    bool lateMissing = true;
    bool baselineMissing = false;
    bool actualMissing = false;
    bool remainingMissing = false;
    bool missing = false;
};
class XmlScheduleScope {
    struct Binding {
        const PlanInput *input = nullptr;
        std::shared_ptr<const XmlScheduleValues> values;
    };
    Binding binding_;
    const Binding *previous_;
    static thread_local const Binding *current_;

public:
    XmlScheduleScope();
    XmlScheduleScope(const PlanInput &, std::shared_ptr<const XmlScheduleValues>);
    ~XmlScheduleScope();
    XmlScheduleScope(const XmlScheduleScope &) = delete;
    static std::shared_ptr<const XmlScheduleValues> current(const PlanInput &);
};
bool xmlActualReference(const std::string &);
bool xmlActualQuantityReference(const std::string &);
bool xmlComputedReference(const std::string &);
bool xmlEarlyLateReference(const std::string &);
bool hasXmlEarlyLateReferences(const PlanInput &, bool activeOnly = false);
bool xmlRemainingReference(const std::string &);
bool xmlBaselineReference(const std::string &);
// Numbered slots are stable native approval versions: Baseline0=v1 ... Baseline10=v11.
std::optional<int> xmlBaselineSlot(const std::string &);
std::string xmlBaselineBaseKey(const std::string &);
std::string xmlBaselineNamedKey(const std::string &);
std::string xmlBaselineDisplayName(const std::string &);
bool hasXmlNumberedBaselineReferences(const PlanInput &, bool activeOnly = false);
bool xmlBaselineQuantityReference(const std::string &);
bool hasXmlBaselineReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlBaselineCostReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlRemainingReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlActualReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlActualQuantityReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlComputedReferences(const PlanInput &, bool activeOnly = false);
bool hasXmlScheduleReferences(const PlanInput &, bool activeOnly = false);
std::string xmlScheduleCacheKey(const PlanInput &);
std::string xmlScheduleCaption(const PlanInput &);
} // namespace jobs
