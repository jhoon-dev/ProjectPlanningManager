#pragma once
#include "domain/Project.h"
#include "domain/XerFinancialMerge.h"
#include "domain/XerSourceRelink.h"
#include <variant>
#include <functional>
#include <type_traits>
#include <utility>

namespace ppm {
struct TaskEdit {
    std::string taskId;
    std::optional<std::string> name;
    std::optional<std::optional<std::int64_t>> duration;
    std::optional<std::vector<Dependency>> edges;
};
struct DurationEdit {
    std::string taskId;
    std::optional<std::int64_t> minutes;
};
struct EffortEdit {
    std::string taskId;
    TaskType type = TaskType::FixedDuration;
    bool workInput = false;
    std::optional<std::int64_t> duration, work;
    std::vector<TaskBreak> breaks;
};
struct AssignmentEdit {
    std::string taskId;
    std::vector<Assignment> assignments;
};
struct AssignmentPeriodsEdit {
    std::string taskId, resourceId;
    std::vector<AssignmentPeriod> periods;
};
struct DatedRemainingWorkEdit {
    RemainingWorkPlan plan;
    bool clearAll = false;
};
struct ProgressEdit {
    ProgressEntry entry;
};
struct ProgressMergeEdit {
    std::vector<ProgressEntry> entries;
};
struct PeriodActualEdit {
    PeriodActualSubmission submission;
};
struct PeriodActualBatchEdit {
    std::vector<PeriodActualSubmission> submissions;
    std::string statusAt;
};
struct XerFinancialEdit {
    XerFinancialSubmission submission;
    std::string (*projectionHash)(const Project &) = nullptr;
};
struct XerFinancialMergeEdit {
    XerFinancialMergeSubmission submission;
    std::string (*projectionHash)(const Project &) = nullptr;
};
struct XerRelinkEdit {
    XerRelinkSubmission submission;
    std::string (*projectionHash)(const Project &) = nullptr;
};
struct XerScheduleEdit {
    XerScheduleSubmission submission;
};
struct XmlBaselineMergeEdit {
    XmlBaselineMergeSubmission submission;
};
struct LedgerIdEdit {
    LedgerIdSubmission submission;
};
struct CurrencyConversionEdit {
    CurrencyConversionSubmission submission;
};
struct FinancialPeriodEdit {
    FinancialPeriodSubmission submission;
};
struct ExternalEvmEdit {
    ExternalEvmSubmission submission;
};
struct BaselineEdit {
    std::string name, approvedBy, reason, createdAt;
};
struct BatchTaskEdit {
    std::vector<TaskEdit> changes;
};
struct GanttTaskEdit {
    std::string taskId;
    bool resize;
    std::string target;
};
struct InsertTasksEdit {
    std::vector<std::string> names;
    std::string beforeId, wbsId;
};
struct RemoveTasksEdit {
    std::vector<std::string> ids;
};
struct DuplicateTasksEdit {
    std::vector<std::string> ids;
};
struct HistoryEdit {
    bool redo = false;
};
struct AdoptLevelingEdit {
    LevelingSelection selection;
};
struct ClearLevelingEdit {};
struct AdoptProgressPolicyEdit {
    ProgressPolicySelection selection;
};
struct ClearProgressPolicyEdit {};
struct ResourcePlanningEdit {
    ResourcePlanningSubmission submission;
};
struct ObjectAttributeEdit {
    FieldScope scope = FieldScope::Project;
    std::vector<std::string> resourceIds;
    std::string key;
    std::optional<std::string> value;
};
using BulkTaskCommand = std::variant<InsertTasksEdit, RemoveTasksEdit, DuplicateTasksEdit, BatchTaskEdit>;
// Internal, data-only UI decoder: never capture widgets or a live Document.
struct DeferredTaskEdit {
    std::function<BulkTaskCommand(const Project &)> decode;
};
using EditCommandValue =
    std::variant<DurationEdit, EffortEdit, AssignmentEdit, AssignmentPeriodsEdit, DatedRemainingWorkEdit, ProgressEdit,
                 ProgressMergeEdit, BaselineEdit, BatchTaskEdit, GanttTaskEdit, InsertTasksEdit, RemoveTasksEdit,
                 DuplicateTasksEdit, DeferredTaskEdit, HistoryEdit, PeriodActualEdit, PeriodActualBatchEdit,
                 XerFinancialEdit, XerFinancialMergeEdit, XerRelinkEdit, ExternalEvmEdit, FinancialPeriodEdit,
                 AdoptLevelingEdit, ClearLevelingEdit, AdoptProgressPolicyEdit, ClearProgressPolicyEdit,
                 ObjectAttributeEdit, XerScheduleEdit, LedgerIdEdit, CurrencyConversionEdit, ResourcePlanningEdit,
                 XmlBaselineMergeEdit>;
// Keep the large variant's special members out of UI translation units. The
// public variant interface remains available to existing typed dispatch.
struct EditCommand : EditCommandValue {
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, EditCommand> &&
                 std::is_constructible_v<EditCommandValue, std::in_place_type_t<std::remove_cvref_t<T>>, T>)
    EditCommand(T &&value) : EditCommandValue(std::in_place_type<std::remove_cvref_t<T>>, std::forward<T>(value)) {}
    template <class T, class... Args>
    explicit EditCommand(std::in_place_type_t<T> type, Args &&...args)
        : EditCommandValue(type, std::forward<Args>(args)...) {}
    EditCommand();
    EditCommand(const EditCommand &);
    EditCommand(EditCommand &&) noexcept(std::is_nothrow_move_constructible_v<EditCommandValue>);
    EditCommand &operator=(const EditCommand &);
    EditCommand &operator=(EditCommand &&) noexcept(std::is_nothrow_move_assignable_v<EditCommandValue>);
    ~EditCommand();
};
enum class EditState { Ready, Applied, NoChange, Cancelled, Stale, Failed };
} // namespace ppm
