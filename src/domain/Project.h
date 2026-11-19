#pragma once
#include "domain/Presentation.h"
#include "domain/ActivityTypes.h"
#include "domain/ResourcePlanTypes.h"
#include "domain/MaterialActualTypes.h"
#include "domain/PlanTypes.h"
#include "domain/Diagnostics.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include "domain/ScheduleTypes.h"
#include "domain/ClassificationTypes.h"
#include "domain/PeriodActualTypes.h"
#include "domain/LevelingSelection.h"
#include "domain/LevelingDecision.h"
#include "domain/ProgressPolicySelection.h"
#include "domain/RemainingWorkTypes.h"
#include "domain/XerFinancials.h"
#include "domain/ExternalEvmTypes.h"
#include "domain/FinancialPeriodTypes.h"
#include "domain/ProductivityTypes.h"
#include "domain/XmlBaselineTypes.h"

namespace ppm {
inline constexpr const char *pdmProfile = "native-pdm-taskcal-v1";
inline constexpr const char *constraintProfile = "native-pdm-constraints-v1";
inline constexpr const char *effortProfile = "native-effort-split-v1";
inline constexpr std::int64_t maxWork = 500000000000LL;
struct TaskBreak {
    std::string start, finish;
    bool operator==(const TaskBreak &) const = default;
};
struct Dependency {
    std::string predecessorId;
    DependencyType type = DependencyType::FS;
    std::int64_t lagMinutes = 0;
    bool operator==(const Dependency &) const = default;
};
struct WorkPeriod {
    int begin = 540, end = 720;
    bool operator==(const WorkPeriod &) const = default;
};
struct TaskCalendar {
    std::string id, name;
    std::vector<int> weekdays = {1, 2, 3, 4, 5};
    std::vector<WorkPeriod> periods = {{540, 720}, {780, 1080}};
    std::vector<std::string> holidays;
    bool operator==(const TaskCalendar &) const = default;
};
struct Task {
    std::string id;
    std::string name;
    std::optional<std::int64_t> durationMinutes;
    std::vector<std::string> predecessors;
    std::string wbsId = {};
    // IDs are the graph; only non-FS0 edge attributes are stored as overrides.
    std::vector<Dependency> dependencyRules = {};
    std::string calendarId = {};
    ConstraintType constraintType = ConstraintType::ASAP;
    std::optional<std::string> constraintAt = {};
    std::optional<std::string> deadlineAt = {};
    TaskType taskType = TaskType::FixedDuration;
    // 100 = one resource-minute, 6000 = one resource-hour. Never inferred actual work.
    std::optional<std::int64_t> workUnitMinutes = {};
    bool workDriven = false;
    std::vector<TaskBreak> breaks = {};
    std::vector<TaskAttribute> attributes = {};
    bool operator==(const Task &) const = default;
};
struct WbsNode {
    std::string id, name, parentId;
    std::vector<TaskAttribute> attributes = {};
    bool operator==(const WbsNode &) const = default;
};
// Renewable capacity, in hundredths of a unit (100 = one person/machine).
// Unknown capacity is distinct from zero. Exceptions replace the daily maximum.
struct CapacityDay {
    std::string date;
    std::optional<int> units;
    bool operator==(const CapacityDay &) const = default;
};
// Civil minutes, both endpoint minutes included. Missing endpoints are unbounded.
struct AvailabilityPeriod {
    std::optional<std::string> from, to;
    std::optional<int> units;
    bool operator==(const AvailabilityPeriod &) const = default;
};
struct Resource {
    std::string id, name;
    std::string type = "labor";
    std::optional<int> maxUnits = 100;
    std::vector<CapacityDay> exceptions;
    struct Rate {
        std::string effectiveDate;
        std::optional<std::int64_t> hourlyMinor;
        bool operator==(const Rate &) const = default;
    };
    std::vector<Rate> rates = {};
    std::string calendarId = {};
    std::vector<TaskAttribute> attributes = {};
    std::optional<ProductivityRate> productivity = {};
    std::optional<std::string> availableFrom = {}, availableTo = {};
    std::vector<AvailabilityPeriod> availability = {};
    bool operator==(const Resource &) const = default;
};
struct AssignmentPeriod {
    // Half-open task working-minute offsets; gaps use the assignment's default units.
    std::int64_t fromMinute = 0, toMinute = 0;
    int units = 100;
    bool operator==(const AssignmentPeriod &) const = default;
};
struct NativeCurve {
    std::string source, externalId, name, reference, reporter, recordedAt;
    int mspCode = -1, baseUnits = 100;
    // Twenty 5% duration bins; index zero is the zero-width origin.
    std::vector<int> weights;
    std::int64_t durationMinutes = 0, workUnitMinutes = 0;
    std::vector<AssignmentPeriod> projectedPeriods;
    bool operator==(const NativeCurve &) const = default;
};
struct Assignment {
    std::string taskId, resourceId;
    int units = 100;
    std::vector<AssignmentPeriod> periods = {};
    std::optional<NativeCurve> nativeCurve = {};
    std::optional<ProductivityEstimate> productivityEstimate = {};
    std::vector<TaskAttribute> attributes = {};
    std::vector<std::string> rollDownMissing = {}; // Explicit XML missing overrides, distinct from inheritance.
    bool operator==(const Assignment &) const = default;
};
struct ResourcePlanningSubmission {
    // rate, estimate, curve; clearing metadata does not erase materialized rates.
    std::string action, taskId, resourceId, reason, reporter, recordedAt;
    std::optional<ProductivityRate> rate;
    std::optional<std::int64_t> quantityMilli;
    std::optional<NativeCurve> curve;
    bool confirmed = false;
    bool keepDuration = false;
};
inline constexpr std::int64_t maxMoney = 9000000000000LL;
struct TaskCost {
    std::string taskId;
    std::optional<std::int64_t> fixedMinor;
    std::string accrual = "uniform";
    std::string credit = "physical";
    bool operator==(const TaskCost &) const = default;
};
struct PlanInput {
    std::string name;
    std::string startDate;
    std::string calendar = "standard-mon-fri-8h-v1";
    std::vector<Task> tasks;
    std::vector<std::string> holidays;
    std::string calculationProfile = "native-fs0-workminutes-v1";
    std::vector<WbsNode> wbsNodes;
    std::vector<Resource> resources;
    std::vector<Assignment> assignments;
    std::string currency = "KRW";
    std::string costProfile = "cost-day-minor-half-up-v1";
    std::vector<TaskCost> taskCosts;
    std::vector<TaskCalendar> calendars;
    std::vector<CustomField> customFields;
    std::vector<ActivityCodeType> codeTypes;
    std::optional<LevelingSelection> planLeveling;
    std::vector<CustomField> projectFields, resourceFields, assignmentFields;
    std::vector<TaskAttribute> projectAttributes;
    std::vector<ActivityDefinition> activities;
    ResourcePlan resourcePlan;
    std::optional<XmlWbsCodes> xmlWbs;
    std::vector<TaskAttribute> taskSummaryAttributes;
    // Imported XML project calendar for project/summary formula defaults; leaf calendars remain explicit.
    std::string xmlFormulaCalendarId;
    std::optional<XmlBaselineSet> xmlBaselines;
    bool operator==(const PlanInput &) const;
};
struct XerScheduleSource {
    PlanInput input;
    std::string sourceName, sourceSha256, projectId, importedAt, reportJson;
    std::optional<std::string> statusAt;
    bool operator==(const XerScheduleSource &) const = default;
};
struct XerScheduleLink {
    std::string sourceId, targetId;
    bool create = false; // Empty target without create explicitly excludes this source task.
    bool operator==(const XerScheduleLink &) const = default;
};
struct XerScheduleSubmission {
    XerScheduleSource source;
    std::vector<XerScheduleLink> links;
    bool schedule = true, wbs = true, calendars = true, confirmed = false;
    std::string reporter, reason, recordedAt;
    bool operator==(const XerScheduleSubmission &) const = default;
};
struct XerScheduleAdoption : XerScheduleSubmission {
    std::string id, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    PlanInput before, after;
    bool operator==(const XerScheduleAdoption &) const = default;
};
struct Baseline {
    std::string id, name, approvedBy, reason, createdAt;
    std::string sourceDocumentId;
    std::uint64_t inputRevision = 0, version = 0;
    PlanInput input;
    std::string engineVersion, projectFinish;
    std::vector<TaskSchedule> schedule;
    bool operator==(const Baseline &) const = default;
};
struct ProgressEntry {
    std::string id, taskId, statusAt, recordedAt, reporter, reason, previousEntryId;
    std::string sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    std::optional<std::int64_t> planDurationMinutes;
    std::optional<std::string> actualStart, actualFinish;
    std::int64_t actualDurationMinutes = 0;
    std::int64_t remainingMinutes = 0;
    std::optional<std::int64_t> remainingWorkUnitMinutes = {};
    bool operator==(const ProgressEntry &) const = default;
};
struct LevelingScenario {
    std::string id, name, createdAt, sourceDocumentId;
    std::uint64_t inputRevision = 0;
    std::string policy = "serial-no-split-v1";
    std::string priority = "task-order";
    int horizonWorkDays = 260;
    PlanInput input;
    std::optional<std::string> statusAt;
    std::vector<ProgressEntry> progress;
    std::optional<ProgressPolicySelection> progressPolicy;
    std::vector<MaterialActualEntry> materialActuals;
    std::vector<RemainingWorkPlan> remainingWork;
    bool operator==(const LevelingScenario &) const = default;
};
struct CostEntry {
    std::string id, taskId, statusAt, recordedAt, reporter, reason, previousEntryId, sourceDocumentId, currency;
    std::uint64_t recordedRevision = 0;
    std::optional<std::int64_t> actualMinor;
    std::optional<int> physicalBasisPoints;
    bool operator==(const CostEntry &) const = default;
};
struct LedgerIdLink {
    std::string kind, sourceId, targetId;
    bool operator==(const LedgerIdLink &) const = default;
};
struct LedgerIdSubmission {
    std::vector<LedgerIdLink> links;
    std::string reporter, reason, recordedAt;
    bool confirmed = false;
    bool operator==(const LedgerIdSubmission &) const = default;
};
struct LedgerIdMigration : LedgerIdSubmission {
    std::string id, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    std::shared_ptr<const Project> before, after;
    bool operator==(const LedgerIdMigration &) const;
};
struct CurrencyConversionSubmission {
    CurrencyRate rate;
    std::string reporter, reason, recordedAt;
    bool confirmed = false;
    bool operator==(const CurrencyConversionSubmission &) const = default;
};
struct CurrencyConversion : CurrencyConversionSubmission {
    std::string id, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    std::shared_ptr<const Project> before, after;
    bool operator==(const CurrencyConversion &) const;
};
struct Project : PlanInput {
    std::vector<XmlBaselineMerge> xmlBaselineMerges;
    std::vector<CurrencyConversion> currencyConversions;
    std::vector<LedgerIdMigration> ledgerIdMigrations;
    std::vector<XerScheduleAdoption> xerScheduleAdoptions;
    std::vector<FinancialPeriodEvent> financialPeriodEvents;
    std::vector<ExternalEvmAdoption> externalEvmAdoptions;
    std::optional<XerFinancialSnapshot> xerFinancials;
    std::vector<XerFinancialRevision> xerFinancialRevisions;
    std::vector<MaterialActualEntry> materialActuals;
    std::vector<RemainingWorkPlan> remainingWork;
    std::vector<StepReport> stepReports;
    std::optional<PrintPreferences> printPreferences;
    std::vector<NetworkPosition> networkPositions;
    std::string documentId;
    std::string derivedFrom;
    std::uint64_t revision = 1;
    std::vector<Baseline> baselines;
    std::vector<ProgressEntry> progress;
    std::optional<std::string> statusAt;
    std::vector<LevelingScenario> scenarios;
    std::vector<CostEntry> costEntries;
    std::vector<SavedTaskView> savedTaskViews;
    std::vector<SavedResourceView> savedResourceViews;
    std::vector<PeriodActualEntry> periodActuals;
    std::optional<LevelingSelection> remainingLeveling;
    std::optional<ProgressPolicySelection> progressPolicy;
    std::vector<LevelingDecision> levelingDecisions;
    bool operator==(const Project &) const;
};
// Domain uses ISO civil dates, without a UI locale or implicit timezone.
bool validDate(const std::string &date);
bool validStamp(const std::string &stamp);
bool validUtcStamp(const std::string &stamp);
void validatePlan(const PlanInput &plan);
void validateTaskName(const std::string &name);
void validate(const Project &project);
// Report-date selection, with later appended corrections winning a tie.
const ProgressEntry *progressAsOf(const Project &project, const std::string &taskId);
const CostEntry *costAsOf(const Project &, const std::string &taskId, const std::string &statusAt);
int currencyDecimals(const std::string &currency);
std::vector<Dependency> dependencies(const Task &);
bool isPdm(const PlanInput &);
bool hasConstraints(const PlanInput &);
bool hasAssignmentPeriods(const PlanInput &);
bool hasWorkAssignmentPeriods(const PlanInput &);
bool isEffort(const PlanInput &);
bool usesWork(const Task &);
std::string engineFor(const PlanInput &);
std::string legacyEngineFor(const PlanInput &);
bool supportsEngine(const PlanInput &, const std::string &);
std::string levelingPolicyFor(const PlanInput &);
std::string levelingPolicyFor(const Project &);
std::string legacyLevelingPolicyFor(const PlanInput &);
bool supportsLevelingPolicy(const PlanInput &, const std::string &);
bool supportsLevelingPolicy(const Project &, const std::string &);
void validateLevelingSelection(const PlanInput &, const LevelingSelection &);
// Editing the source schedule invalidates an adoption; history replay restores its exact previous settings.
void invalidateLevelingSelection(Project &proposal, const Project &before);
} // namespace ppm
