#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlRollUp.h"
#include "ui/TaskViewProjection.h"
#include "domain/Wbs.h"
#include "calculation/WorkCalendar.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include <algorithm>
#include <unordered_set>
namespace ppm {
TaskViewProjection projectTaskView(const Project &p, const ScheduleResult *result, const TaskViewSpec &spec,
                                   bool calculateSchedule) {
    EvaluationScope evaluation;
    ScheduleFormulaScope formulaSchedule(p, result, ScheduleFormulaMode::Active, calculateSchedule);
    TaskViewProjection out;
    out.rows = wbsTaskRows(p, spec.wbs);
    out.scopeCount = out.rows.size();
    const auto query = spec.query.trimmed();
    const bool valid = result && result->status == CalculationStatus::Valid;
    const bool plan = valid && !result->isForecast;
    const auto &classification = spec.classification;
    auto settings = classification;
    settings.wbsId = spec.wbs;
    settings.query = spec.query.toStdString();
    settings.filter = spec.filter;
    settings.sort = spec.sort;
    validateTaskView(p, settings);
    const auto labels = attributeLabels(p);
    std::unordered_map<std::string, const CustomField *> fields;
    for (const auto &field : p.customFields)
        fields.emplace("field:" + field.id, &field);
    std::vector<std::string> computedKeys;
    std::unordered_map<std::string, std::size_t> computedIndexes;
    auto need = [&](const std::string &key) {
        if (fields.contains(key) && calculatedField(*fields.at(key)) && !computedIndexes.contains(key)) {
            computedIndexes.emplace(key, computedKeys.size());
            computedKeys.push_back(key);
        }
    };
    for (const auto &key : settings.groupBy)
        need(key);
    for (const auto &filter : settings.attributeFilters)
        need(filter.key);
    need(settings.sortKey);
    std::unordered_map<const Task *, std::vector<std::optional<std::string>>> computedRows;
    auto valueOf = [&](const Task &task, const std::string &key) -> std::optional<std::string> {
        if (!computedIndexes.contains(key))
            return attributeValue(task, key);
        auto [it, fresh] = computedRows.try_emplace(&task);
        if (fresh) {
            const auto values = formulaValues(p, FieldScope::Task, &task);
            for (const auto &k : computedKeys) {
                const auto &v = values->at(k);
                it->second.push_back(formulaRawValue(v));
            }
        }
        return it->second[computedIndexes.at(key)];
    };
    auto groupValue = [&](const Task &t, const std::string &key) {
        const auto value = valueOf(t, key);
        return value ? "v" + *value : "m";
    };
    auto compareAttribute = [&](std::size_t a, std::size_t b, const std::string &key) {
        const auto x = valueOf(p.tasks[a], key), y = valueOf(p.tasks[b], key);
        if (bool(x) != bool(y))
            return x ? -1 : 1;
        if (!x)
            return 0;
        if (fields.contains(key) && numericCustomField(fields.at(key)->type)) {
            const auto left = customFieldNumber(p, *fields.at(key), *x),
                       right = customFieldNumber(p, *fields.at(key), *y);
            return left < right ? -1 : left > right ? 1 : 0;
        }
        const auto c = QString::compare(QString::fromStdString(attributeText(labels, key, *x)),
                                        QString::fromStdString(attributeText(labels, key, *y)), Qt::CaseInsensitive);
        return c ? (c < 0 ? -1 : 1) : *x < *y ? -1 : *x > *y ? 1 : 0;
    };
    std::unordered_map<std::string, const TaskSchedule *> dates;
    std::unordered_map<std::string, const TaskSchedule *> durationDates;
    if (valid && spec.sort >= 3 && spec.sort <= 4) {
        const auto &rows = result->isForecast ? result->plannedTasks : result->tasks;
        for (const auto &row : rows)
            durationDates[row.taskId] = &row;
    }
    std::unordered_set<std::string> assigned;
    if (spec.filter || spec.sort >= 3) {
        if (valid)
            for (const auto &row : result->tasks)
                dates[row.taskId] = &row;
        if (spec.filter == 3)
            for (const auto &a : p.assignments)
                assigned.insert(a.taskId);
    }
    std::erase_if(out.rows, [&](std::size_t i) {
        calculationCheckpoint();
        const auto &t = p.tasks[i];
        for (const auto &f : classification.attributeFilters) {
            const auto value = valueOf(t, f.key);
            if (f.operation == "missing") {
                if (value)
                    return true;
            } else if (f.operation == "present") {
                if (!value)
                    return true;
            } else if (f.operation == "equals") {
                if (!value || *value != f.value)
                    return true;
            } else if (!value || !QString::fromStdString(attributeText(labels, f.key, *value))
                                      .contains(QString::fromStdString(f.value), Qt::CaseInsensitive))
                return true;
        }
        if (!query.isEmpty() && !QString::fromUtf8(t.name).contains(query, Qt::CaseInsensitive) &&
            !QString::fromUtf8(t.id).contains(query, Qt::CaseInsensitive))
            return true;
        if (spec.filter == 1)
            return !plan || !dates.contains(t.id) || !dates.at(t.id)->critical;
        if (spec.filter == 2)
            return !(usesWork(t) ? t.workUnitMinutes == 0 : t.durationMinutes == 0) &&
                   !(dates.contains(t.id) && dates.at(t.id)->start == dates.at(t.id)->finish);
        return spec.filter == 3 && assigned.contains(t.id);
    });
    if (spec.sort) {
        struct Key {
            QString name;
            std::optional<std::int64_t> duration;
            std::optional<std::string> start;
        };
        std::vector<Key> keys(p.tasks.size());
        for (auto i : out.rows) {
            calculationCheckpoint();
            const auto &t = p.tasks[i];
            auto &k = keys[i];
            k.name = QString::fromUtf8(t.name);
            k.duration = t.durationMinutes;
            if (dates.contains(t.id))
                k.start = dates.at(t.id)->start;
            if (usesWork(t)) {
                k.duration.reset();
                if (durationDates.contains(t.id)) {
                    const auto &r = *durationDates.at(t.id);
                    WorkCalendar cal(p, t);
                    k.duration = cal.offset(r.finish) - cal.offset(r.start);
                }
            }
        }
        std::size_t comparisons = 0;
        std::stable_sort(out.rows.begin(), out.rows.end(), [&](std::size_t a, std::size_t b) {
            if (++comparisons % 1024 == 0)
                calculationCheckpoint();
            const auto &x = keys[a];
            const auto &y = keys[b];
            const int sort = spec.sort;
            if (sort <= 2) {
                const auto c = QString::compare(x.name, y.name, Qt::CaseInsensitive);
                return sort == 1 ? c < 0 : c > 0;
            }
            if (sort <= 4) {
                if (bool(x.duration) != bool(y.duration))
                    return bool(x.duration);
                if (!x.duration)
                    return false;
                return sort == 3 ? *x.duration < *y.duration : *x.duration > *y.duration;
            }
            if (bool(x.start) != bool(y.start))
                return bool(x.start);
            if (!x.start)
                return false;
            return sort == 5 ? *x.start < *y.start : *x.start > *y.start;
        });
    }
    if (!classification.groupBy.empty() || !classification.sortKey.empty()) {
        std::size_t comparisons = 0;
        std::stable_sort(out.rows.begin(), out.rows.end(), [&](std::size_t a, std::size_t b) {
            if (++comparisons % 1024 == 0)
                calculationCheckpoint();
            for (const auto &key : classification.groupBy) {
                const int c = compareAttribute(a, b, key);
                if (c)
                    return c < 0;
            }
            if (!classification.sortKey.empty()) {
                const auto &key = classification.sortKey;
                const bool x = valueOf(p.tasks[a], key).has_value(), y = valueOf(p.tasks[b], key).has_value();
                if (x != y)
                    return x;
                const int c = compareAttribute(a, b, key);
                return classification.descending ? c > 0 : c < 0;
            }
            return false;
        });
    }
    std::unordered_map<std::string, std::size_t> groups;
    std::vector<RollUpGroup> summaryGroups{{"", {}, {}}};
    for (const auto i : out.rows) {
        calculationCheckpoint();
        const auto &task = p.tasks[i];
        std::vector<std::string> path;
        std::string token;
        for (const auto &key : classification.groupBy) {
            const auto value = groupValue(task, key);
            path.push_back(value);
            const auto parent = token;
            token += std::to_string(value.size()) + ":" + value;
            if (!groups.contains(token)) {
                groups.emplace(token, out.groups.size());
                summaryGroups.push_back({token, parent, {}});
                out.groups.push_back(
                    {path, value == "m" ? "미설정" : attributeText(labels, key, value.substr(1)), 0, 0, 0, {}});
            }
            auto &group = out.groups[groups.at(token)];
            ++group.tasks;
            if (!usesWork(task) && task.durationMinutes)
                group.durationMinutes += *task.durationMinutes;
            else if (valid) {
                if (durationDates.empty()) {
                    const auto &source = result->isForecast ? result->plannedTasks : result->tasks;
                    for (const auto &r : source)
                        durationDates.emplace(r.taskId, &r);
                }
                if (durationDates.contains(task.id)) {
                    WorkCalendar cal(p, task);
                    const auto &r = *durationDates.at(task.id);
                    group.durationMinutes += cal.offset(r.finish) - cal.offset(r.start);
                } else
                    ++group.missingDurations;
            } else
                ++group.missingDurations;
        }
        summaryGroups[token.empty() ? 0 : groups.at(token) + 1].rows.push_back(i);
    }
    if (!out.groups.empty() && hasXmlRollUp(p)) {
        const RollUpValues values(p, FieldScope::Task, std::move(summaryGroups));
        for (auto &g : out.groups)
            g.rollUpText = values.describe(rollUpPath(g.path));
    }
    if (!classification.groupPath.empty())
        std::erase_if(out.rows, [&](std::size_t i) {
            for (std::size_t level = 0; level < classification.groupPath.size(); ++level)
                if (groupValue(p.tasks[i], classification.groupBy[level]) != classification.groupPath[level])
                    return true;
            return false;
        });
    return out;
}
namespace {
void appendRange(std::vector<std::pair<int, int>> &ranges, int row) {
    if (!ranges.empty() && ranges.back().second == row - 1)
        ranges.back().second = row;
    else
        ranges.emplace_back(row, row);
}
} // namespace
void prepareTaskView(BulkView &view, const Project &before, const Project &after, const TaskViewSpec &spec,
                     std::vector<std::size_t> previousRows, std::string token, std::uint64_t generation) {
    auto projected = projectTaskView(after, nullptr, spec, true);
    view.projected = true;
    view.viewToken = std::move(token);
    view.sourceCalculationGeneration = generation;
    view.scopeTaskCount = projected.scopeCount;
    view.summary = summarizeWbs(after, nullptr, spec.wbs);
    view.previousRows = std::move(previousRows);
    view.rows = std::move(projected.rows);
    view.positions.clear();
    std::unordered_set<std::string> oldIds, newIds;
    std::vector<std::string> survivingOld;
    for (auto row : view.previousRows) {
        calculationCheckpoint();
        oldIds.insert(before.tasks.at(row).id);
    }
    for (auto row : view.rows)
        newIds.insert(after.tasks[row].id);
    for (int row = 0; row < int(view.previousRows.size()); ++row) {
        calculationCheckpoint();
        const auto &id = before.tasks[view.previousRows[std::size_t(row)]].id;
        if (!newIds.contains(id))
            appendRange(view.removals, row);
        else
            survivingOld.push_back(id);
    }
    std::unordered_set<std::size_t> changed;
    for (const auto &[first, last] : view.changedRanges)
        for (int row = first; row <= last; ++row)
            changed.insert(std::size_t(row));
    view.changedRanges.clear();
    for (int row = 0; row < int(view.rows.size()); ++row) {
        calculationCheckpoint();
        const auto source = view.rows[std::size_t(row)];
        const auto &id = after.tasks[source].id;
        view.positions.emplace(source, row);
        if (!oldIds.contains(id))
            appendRange(view.insertions, row);
        else {
            const auto position = view.survivorRows.size();
            if (survivingOld[position] != id)
                view.reordered = true;
            view.survivorPositions.emplace(id, int(position));
            view.survivorRows.push_back(source);
        }
        if (changed.contains(source))
            appendRange(view.changedRanges, row);
    }
    for (const auto &id : survivingOld)
        view.survivorOldOrderRows.push_back(std::size_t(view.numbers.at(id) - 1));
    calculationCheckpoint();
}
} // namespace ppm
