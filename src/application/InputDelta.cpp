#include "domain/ResourcePlan.h"
#include "application/InputDelta.h"

namespace jobs {
namespace {
std::size_t optionalBytes(const std::optional<std::string> &value) {
    return value ? value->size() : 0;
}
std::size_t valueBytes(const ValueDelta<std::string> &value) {
    return value.values ? value.values->first.size() + value.values->second.size() : 0;
}
} // namespace
std::size_t inputExtraBytes(const Task &t) {
    std::size_t bytes = t.id.size() + t.name.size() + t.wbsId.size() + t.calendarId.size() +
                        optionalBytes(t.constraintAt) + optionalBytes(t.deadlineAt);
    for (const auto &id : t.predecessors)
        bytes += sizeof(std::string) + id.size();
    for (const auto &edge : t.dependencyRules)
        bytes += sizeof(Dependency) + edge.predecessorId.size();
    for (const auto &pause : t.breaks)
        bytes += sizeof(TaskBreak) + pause.start.size() + pause.finish.size();
    for (const auto &a : t.attributes)
        bytes += sizeof(TaskAttribute) + a.key.size() + a.value.size();
    return bytes;
}
std::size_t inputExtraBytes(const WbsNode &w) {
    auto bytes = w.id.size() + w.name.size() + w.parentId.size();
    for (const auto &a : w.attributes)
        bytes += sizeof(a) + a.key.size() + a.value.size();
    return bytes;
}
std::size_t inputExtraBytes(const Resource &r) {
    std::size_t bytes = r.id.size() + r.name.size() + r.type.size() + r.calendarId.size();
    bytes += optionalBytes(r.availableFrom) + optionalBytes(r.availableTo);
    for (const auto &period : r.availability)
        bytes += sizeof(period) + optionalBytes(period.from) + optionalBytes(period.to);
    for (const auto &day : r.exceptions)
        bytes += sizeof(CapacityDay) + day.date.size();
    for (const auto &a : r.attributes)
        bytes += sizeof(TaskAttribute) + inputExtraBytes(a);
    for (const auto &rate : r.rates)
        bytes += sizeof(Resource::Rate) + rate.effectiveDate.size();
    return bytes;
}
std::size_t inputExtraBytes(const Assignment &a) {
    std::size_t bytes = a.taskId.size() + a.resourceId.size() + a.periods.size() * sizeof(AssignmentPeriod);
    for (const auto &v : a.attributes)
        bytes += sizeof(v) + v.key.size() + v.value.size();
    for (const auto &key : a.rollDownMissing)
        bytes += sizeof(key) + key.size();
    return bytes;
}
std::size_t inputExtraBytes(const TaskCost &c) {
    return c.taskId.size() + c.accrual.size() + c.credit.size();
}
std::size_t inputExtraBytes(const TaskCalendar &c) {
    std::size_t bytes =
        c.id.size() + c.name.size() + c.weekdays.size() * sizeof(int) + c.periods.size() * sizeof(WorkPeriod);
    for (const auto &day : c.holidays)
        bytes += sizeof(std::string) + day.size();
    return bytes;
}
std::size_t inputExtraBytes(const TaskAttribute &a) {
    return a.key.size() + a.value.size();
}
std::size_t inputExtraBytes(const CustomField &f) {
    auto bytes = f.id.size() + f.name.size() + f.unit.size() + f.expression.size();
    if (f.xmlIdentity)
        bytes += f.xmlIdentity->fieldId.size() + f.xmlIdentity->guid.size() + f.xmlIdentity->fieldName.size();
    if (f.xmlFormula) {
        bytes += f.xmlFormula->source.size();
        for (const auto &r : f.xmlFormula->references)
            bytes += sizeof(r) + r.token.size() + r.key.size();
    }
    if (f.xmlRollDown)
        bytes += f.xmlRollDown->assignmentFieldId.size();
    if (f.xmlLookup) {
        const auto &l = *f.xmlLookup;
        bytes += l.tableGuid.size() + l.tableName.size() + l.defaultOptionId.size() + l.fieldGuid.size() +
                 l.fieldName.size();
        for (const auto &m : l.masks)
            bytes += sizeof(m) + m.separator.size();
        for (const auto &v : l.values)
            bytes += sizeof(v) + v.optionId.size() + v.valueId.size() + v.guid.size() + v.value.size() +
                     v.description.size() + v.parentId.size();
    }
    for (const auto &o : f.options)
        bytes += sizeof(FieldOption) + o.id.size() + o.name.size();
    return bytes;
}
std::size_t inputExtraBytes(const ActivityCodeType &t) {
    auto bytes = t.id.size() + t.name.size();
    for (const auto &v : t.values)
        bytes += sizeof(ActivityCode) + v.id.size() + v.name.size() + v.parentId.size();
    return bytes;
}
std::size_t inputExtraBytes(const SavedTaskView &v) {
    const auto &s = v.settings;
    auto bytes = v.id.size() + v.name.size() + s.wbsId.size() + s.query.size() + s.sortKey.size();
    for (const auto &x : s.groupBy)
        bytes += sizeof(std::string) + x.size();
    for (const auto &x : s.groupPath)
        bytes += sizeof(std::string) + x.size();
    for (const auto &x : s.attributeFilters)
        bytes += sizeof(AttributeFilter) + x.key.size() + x.operation.size() + x.value.size();
    for (const auto &x : s.columns)
        bytes += sizeof(ViewColumn) + x.key.size();
    return bytes;
}
std::size_t inputExtraBytes(const SavedResourceView &v) {
    std::size_t n = v.id.size() + v.name.size() + v.settings.query.size() + v.settings.sortKey.size();
    for (const auto &g : v.settings.groupBy)
        n += sizeof(g) + g.size();
    for (const auto &f : v.settings.filters)
        n += sizeof(f) + f.key.size() + f.operation.size() + f.value.size();
    return n;
}
InputDelta::InputDelta(const Project &before, const Project &after)
    : name_(before.name, after.name), startDate_(before.startDate, after.startDate),
      calendar_(before.calendar, after.calendar),
      calculationProfile_(before.calculationProfile, after.calculationProfile),
      currency_(before.currency, after.currency), costProfile_(before.costProfile, after.costProfile),
      statusAt_(before.statusAt, after.statusAt), printPreferences_(before.printPreferences, after.printPreferences),
      networkPositions_(before.networkPositions, after.networkPositions),
      activities_(before.activities, after.activities), resourcePlan_(before.resourcePlan, after.resourcePlan),
      xmlWbs_(before.xmlWbs, after.xmlWbs), tasks_(before.tasks, after.tasks),
      holidays_(before.holidays, after.holidays), wbsNodes_(before.wbsNodes, after.wbsNodes),
      resources_(before.resources, after.resources), assignments_(before.assignments, after.assignments),
      taskCosts_(before.taskCosts, after.taskCosts), calendars_(before.calendars, after.calendars),
      customFields_(before.customFields, after.customFields), projectFields_(before.projectFields, after.projectFields),
      resourceFields_(before.resourceFields, after.resourceFields),
      assignmentFields_(before.assignmentFields, after.assignmentFields),
      projectAttributes_(before.projectAttributes, after.projectAttributes),
      taskSummaryAttributes_(before.taskSummaryAttributes, after.taskSummaryAttributes),
      codeTypes_(before.codeTypes, after.codeTypes), savedTaskViews_(before.savedTaskViews, after.savedTaskViews),
      savedResourceViews_(before.savedResourceViews, after.savedResourceViews),
      remainingWork_(before.remainingWork, after.remainingWork), planLeveling_(before.planLeveling, after.planLeveling),
      remainingLeveling_(before.remainingLeveling, after.remainingLeveling),
      progressPolicy_(before.progressPolicy, after.progressPolicy) {}

void InputDelta::apply(Project &p, bool forward) const {
    name_.apply(p.name, forward);
    startDate_.apply(p.startDate, forward);
    calendar_.apply(p.calendar, forward);
    calculationProfile_.apply(p.calculationProfile, forward);
    currency_.apply(p.currency, forward);
    costProfile_.apply(p.costProfile, forward);
    statusAt_.apply(p.statusAt, forward);
    printPreferences_.apply(p.printPreferences, forward);
    networkPositions_.apply(p.networkPositions, forward);
    activities_.apply(p.activities, forward);
    resourcePlan_.apply(p.resourcePlan, forward);
    xmlWbs_.apply(p.xmlWbs, forward);
    tasks_.apply(p.tasks, forward);
    holidays_.apply(p.holidays, forward);
    wbsNodes_.apply(p.wbsNodes, forward);
    resources_.apply(p.resources, forward);
    assignments_.apply(p.assignments, forward);
    taskCosts_.apply(p.taskCosts, forward);
    calendars_.apply(p.calendars, forward);
    customFields_.apply(p.customFields, forward);
    projectFields_.apply(p.projectFields, forward);
    resourceFields_.apply(p.resourceFields, forward);
    assignmentFields_.apply(p.assignmentFields, forward);
    projectAttributes_.apply(p.projectAttributes, forward);
    taskSummaryAttributes_.apply(p.taskSummaryAttributes, forward);
    codeTypes_.apply(p.codeTypes, forward);
    savedTaskViews_.apply(p.savedTaskViews, forward);
    savedResourceViews_.apply(p.savedResourceViews, forward);
    remainingWork_.apply(p.remainingWork, forward);
    planLeveling_.apply(p.planLeveling, forward);
    remainingLeveling_.apply(p.remainingLeveling, forward);
    progressPolicy_.apply(p.progressPolicy, forward);
}
std::size_t InputDelta::bytes() const {
    const auto selectionBytes = [](const auto &delta) {
        std::size_t n = 0;
        if (delta.values)
            for (const auto *value : {&delta.values->first, &delta.values->second})
                if (*value) {
                    const auto &s = **value;
                    n += sizeof(s) + s.priority.size() + s.policy.size() + s.adoptedAt.size() + s.reason.size() +
                         s.sourceDocumentId.size();
                }
        return n;
    };
    std::size_t policyBytes = 0;
    if (progressPolicy_.values)
        for (const auto *s : {&progressPolicy_.values->first, &progressPolicy_.values->second})
            if (*s)
                policyBytes += sizeof(**s) + (*s)->policy.size() + (*s)->adoptedBy.size() + (*s)->adoptedAt.size() +
                               (*s)->reason.size() + (*s)->sourceDocumentId.size();
    std::size_t bytes = policyBytes + sizeof(InputDelta) + valueBytes(name_) + valueBytes(startDate_) +
                        valueBytes(calendar_) + valueBytes(calculationProfile_) + valueBytes(currency_) +
                        valueBytes(costProfile_);
    if (statusAt_.values)
        bytes += optionalBytes(statusAt_.values->first) + optionalBytes(statusAt_.values->second);
    if (printPreferences_.values)
        for (const auto *s : {&printPreferences_.values->first, &printPreferences_.values->second})
            if (*s)
                bytes += sizeof(PrintPreferences) + (*s)->from.size() + (*s)->to.size();
    if (xmlWbs_.values)
        for (const auto *v : {&xmlWbs_.values->first, &xmlWbs_.values->second})
            if (*v) {
                bytes += sizeof(XmlWbsCodes) + (*v)->prefix.size();
                for (const auto &m : (*v)->masks)
                    bytes += sizeof(m) + m.separator.size();
                for (const auto &c : (*v)->values)
                    bytes += sizeof(c) + c.kind.size() + c.id.size() + c.code.size();
            }
    if (resourcePlan_.values)
        bytes += resourcePlanBytes(resourcePlan_.values->first) + resourcePlanBytes(resourcePlan_.values->second);
    return bytes + remainingWork_.bytes() + savedResourceViews_.bytes() + activities_.bytes() +
           networkPositions_.bytes() + tasks_.bytes() + holidays_.bytes() + wbsNodes_.bytes() + resources_.bytes() +
           assignments_.bytes() + taskCosts_.bytes() + calendars_.bytes() + customFields_.bytes() +
           projectFields_.bytes() + assignmentFields_.bytes() + resourceFields_.bytes() + projectAttributes_.bytes() +
           taskSummaryAttributes_.bytes() + codeTypes_.bytes() + savedTaskViews_.bytes() +
           selectionBytes(planLeveling_) + selectionBytes(remainingLeveling_);
}
} // namespace jobs
