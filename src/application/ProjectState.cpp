#include "application/ProjectState.h"

namespace jobs {
DirtyState compareSavedInput(const Project &a, const ProjectState &saved) {
    if (!saved)
        return {};
    const auto &b = *saved;
    DirtyState result;
    result.aligned = a.tasks.size() == b.tasks.size();
    if (result.aligned)
        for (std::size_t i = 0; i < a.tasks.size(); ++i) {
            // Also used after synchronous commit: dirty bookkeeping must not unwind
            // a live edit. Worker callers check cancellation around this pure comparison.
            if (a.tasks[i].id != b.tasks[i].id)
                result.aligned = false;
            if (a.tasks[i] != b.tasks[i])
                ++result.differentTasks;
        }
    // Revision is an audit sequence, not an input-content save point.
    result.other =
        !result.aligned ||
        std::tie(a.name, a.startDate, a.calendar, a.holidays, a.calculationProfile, a.wbsNodes, a.resources,
                 a.assignments, a.currency, a.costProfile, a.taskCosts, a.calendars, a.statusAt, a.documentId,
                 a.derivedFrom, a.baselines, a.progress, a.scenarios, a.costEntries, a.projectFields, a.resourceFields,
                 a.assignmentFields, a.projectAttributes, a.customFields, a.codeTypes, a.savedTaskViews,
                 a.periodActuals, a.planLeveling, a.remainingLeveling, a.progressPolicy, a.levelingDecisions,
                 a.printPreferences, a.networkPositions, a.activities, a.stepReports, a.resourcePlan,
                 a.savedResourceViews, a.remainingWork, a.xerFinancials, a.xerFinancialRevisions,
                 a.externalEvmAdoptions, a.financialPeriodEvents, a.xerScheduleAdoptions, a.ledgerIdMigrations,
                 a.currencyConversions, a.xmlBaselines, a.xmlBaselineMerges, a.xmlFormulaCalendarId,
                 a.materialActuals) !=
            std::tie(b.name, b.startDate, b.calendar, b.holidays, b.calculationProfile, b.wbsNodes, b.resources,
                     b.assignments, b.currency, b.costProfile, b.taskCosts, b.calendars, b.statusAt, b.documentId,
                     b.derivedFrom, b.baselines, b.progress, b.scenarios, b.costEntries, b.projectFields,
                     b.resourceFields, b.assignmentFields, b.projectAttributes, b.customFields, b.codeTypes,
                     b.savedTaskViews, b.periodActuals, b.planLeveling, b.remainingLeveling, b.progressPolicy,
                     b.levelingDecisions, b.printPreferences, b.networkPositions, b.activities, b.stepReports,
                     b.resourcePlan, b.savedResourceViews, b.remainingWork, b.xerFinancials, b.xerFinancialRevisions,
                     b.externalEvmAdoptions, b.financialPeriodEvents, b.xerScheduleAdoptions, b.ledgerIdMigrations,
                     b.currencyConversions, b.xmlBaselines, b.xmlBaselineMerges, b.xmlFormulaCalendarId,
                     b.materialActuals);
    return result;
}
} // namespace jobs
