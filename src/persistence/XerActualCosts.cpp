#include "persistence/XerActualCosts.h"
#include "persistence/XerFinancialJson.h"
#include "calculation/WorkCalendar.h"
#include "domain/CalculationControl.h"
#include "domain/Classification.h"
#include "domain/PeriodActuals.h"
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <tuple>
namespace jobs {
namespace {
std::string s(const QString &v) {
    return v.toUtf8().toStdString();
}
QString q(const std::string &v) {
    return QString::fromStdString(v);
}
[[noreturn]] void invalid(const QString &where) {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {s(where)});
}
XerTable &table(XerTables &tables, const char *tableName) {
    const QString name = QString::fromLatin1(tableName);
    auto &t = tables[name];
    t.name = name;
    if (t.rows.size() >
        (name == "TASKRSRC" || name == "TRSRCFIN" || name == "TASKFIN" || name == "PROJCOST" ? 100000u : 10000u))
        invalid(name + "/row limit");
    return t;
}
QString get(const XerTable &t, const XerRow &r, const QString &field) {
    t.used.insert(field);
    auto i = t.fields.indexOf(field);
    return i < 0 ? QString{} : r.cells.at(i);
}
QString id(const XerTable &t, const XerRow &r, const QString &field) {
    const auto text = get(t, r, field);
    bool ok = false;
    const auto n = text.toLongLong(&ok);
    static const QRegularExpression digits("^[0-9]+$");
    if (!ok || n < 0 || n > 2147483647 || !digits.match(text).hasMatch())
        invalid(t.name + "/" + field);
    return QString::number(n);
}
void note(QJsonArray &out, const QString &path, const QString &message) {
    out.append(QJsonObject{{"path", path}, {"message", message}});
}
std::optional<std::int64_t> number(const QString &text, std::int64_t scale, std::int64_t limit, const QString &where,
                                   bool quantized = false) {
    if (text.isEmpty())
        return {};
    static const QRegularExpression pattern("^([0-9]+)(?:\\.([0-9]{1,6}))?$");
    const auto match = pattern.match(text);
    bool ok = false;
    const auto whole = match.captured(1).toLongLong(&ok);
    if (!match.hasMatch() || !ok)
        invalid(where);
    const auto fraction = match.captured(2).leftJustified(6, '0').toLongLong();
    if (whole > limit / scale)
        invalid(where);
    const auto remainder = fraction * scale;
    const auto rounded = (remainder + fieldNumberScale / 2) / fieldNumberScale;
    if ((!quantized && remainder % fieldNumberScale) ||
        (quantized && std::abs(remainder - rounded * fieldNumberScale) > scale / 2))
        invalid(where);
    const auto value = whole * scale + rounded;
    if (value > limit)
        invalid(where);
    return value;
}
void add(std::optional<std::int64_t> &total, std::optional<std::int64_t> value, std::int64_t limit = maxMoney) {
    if (!value)
        return;
    if (total.value_or(0) > limit - *value)
        invalid("sum overflow");
    total = total.value_or(0) + *value;
}
QString decimal(std::int64_t n, std::int64_t scale) {
    // Six decimal XER places; only the representational half-unit error is allowed on import.
    auto micros = (n / scale) * 1000000 + ((n % scale) * 1000000 + scale / 2) / scale;
    return QString::number(micros / 1000000) + "." + QString::number(micros % 1000000).rightJustified(6, '0');
}
QString text(std::optional<std::int64_t> n, std::int64_t scale) {
    return n ? decimal(*n, scale) : QString{};
}
void set(XerTable &t, XerRow &r, const QString &field, const QString &value) {
    auto index = t.fields.indexOf(field);
    if (index < 0) {
        index = t.fields.size();
        t.fields.append(field);
        for (auto &row : t.rows)
            row.cells.append(QString{});
    }
    r.cells[index] = value;
}
void append(XerTable &t, const std::map<QString, QString> &values) {
    for (const auto &[field, value] : values) {
        (void)value;
        if (!t.fields.contains(field)) {
            t.fields.append(field);
            for (auto &row : t.rows)
                row.cells.append(QString{});
        }
    }
    XerRow row;
    for (const auto &field : t.fields) {
        auto it = values.find(field);
        row.cells.append(it == values.end() ? QString{} : it->second);
    }
    t.rows.push_back(std::move(row));
}
QJsonObject summary(const Project &p) {
    std::set<std::string> progress, costs;
    if (p.statusAt) {
        for (const auto &e : p.progress)
            if (e.statusAt <= *p.statusAt)
                progress.insert(e.taskId);
        for (const auto &e : p.costEntries)
            if (e.statusAt <= *p.statusAt)
                costs.insert(e.taskId);
    }
    QJsonArray allocations;
    if (p.statusAt)
        for (const auto &[taskId, a] : actualCostAllocations(p, *p.statusAt)) {
            if (!a.cumulative && !a.costReports)
                continue;
            allocations.append(
                QJsonObject{{"taskId", q(taskId)},
                            {"cumulativeMinor", a.cumulative ? QString::number(*a.cumulative) : QString{}},
                            {"allocatedMinor", QString::number(a.allocated)},
                            {"unallocatedMinor", a.unallocated ? QString::number(*a.unallocated) : QString{}}});
        }
    return {{"contract", "jobs-xer-actual-cost-v1"},
            {"statusAt", p.statusAt ? q(*p.statusAt) : QString{}},
            {"progressReports", int(progress.size())},
            {"costReports", int(costs.size())},
            {"datedActuals", p.statusAt ? int(periodActualsAsOf(p, *p.statusAt).size()) : 0},
            {"allocations", allocations},
            {"currency", q(p.currency)},
            {"costPolicy", "cumulative-includes-period-allocations"},
            {"earnedValue", "native-baseline-required; P6 BCWP/BCWS are not native approvals"}};
}
} // namespace
void captureXerFinancials(Project &p, XerTables &tables, const QString &sourceName, const QString &sha256,
                          const QString &projectId) {
    if (!p.statusAt)
        return;
    XerFinancialSnapshot snapshot;
    snapshot.sourceName = s(sourceName);
    snapshot.sourceSha256 = s(sha256);
    snapshot.projectId = s(projectId);
    snapshot.statusAt = *p.statusAt;
    snapshot.currency = p.currency;
    snapshot.importedAt = s(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"));
    const auto moneyScale = currencyDecimals(p.currency) ? 100LL : 1LL;
    auto money = [&](const auto &t, const auto &r, const char *f) {
        return number(get(t, r, f), moneyScale, maxMoney, t.name + "/" + f);
    };
    auto work = [&](const auto &t, const auto &r, const char *f) {
        return number(get(t, r, f), 6000, maxWork, t.name + "/" + f, true);
    };
    std::map<std::string, const CostEntry *> currentCosts;
    for (const auto &e : p.costEntries)
        if (e.statusAt <= *p.statusAt) {
            auto &old = currentCosts[e.taskId];
            if (!old || old->statusAt <= e.statusAt)
                old = &e;
        }
    for (const auto &t : p.tasks) {
        const auto *c = currentCosts[t.id];
        snapshot.tasks.push_back({t.id, t.name, c ? c->actualMinor : std::nullopt});
    }
    for (const auto &r : p.resources)
        snapshot.resources.push_back({r.id, r.name, r.type});
    bool observed = false;
    std::map<QString, std::pair<std::string, std::string>> assignments;
    const auto &a = table(tables, "TASKRSRC");
    for (const auto &r : a.rows) {
        calculationCheckpoint();
        XerFinancialAssignment entry{"xer-task-" + s(id(a, r, "task_id")),
                                     "xer-resource-" + s(id(a, r, "rsrc_id")),
                                     work(a, r, "act_reg_qty"),
                                     work(a, r, "act_ot_qty"),
                                     money(a, r, "act_reg_cost"),
                                     money(a, r, "act_ot_cost")};
        observed |= entry.regularWork.has_value() || entry.overtimeWork.has_value() || entry.regularMinor.has_value() ||
                    entry.overtimeMinor.has_value();
        assignments[id(a, r, "taskrsrc_id")] = {entry.taskId, entry.resourceId};
        snapshot.assignments.push_back(entry);
    }
    std::map<QString, std::pair<std::string, std::string>> periods;
    const auto &d = table(tables, "FINDATES");
    for (const auto &r : d.rows)
        periods[id(d, r, "fin_dates_id")] = {s(get(d, r, "start_date")), s(get(d, r, "end_date"))};
    const auto &resourcePeriods = table(tables, "TRSRCFIN");
    for (const auto &r : resourcePeriods.rows) {
        calculationCheckpoint();
        const auto &owner = assignments.at(id(resourcePeriods, r, "taskrsrc_id"));
        const auto &range = periods.at(id(resourcePeriods, r, "fin_dates_id"));
        XerFinancialPeriod e;
        e.taskId = owner.first;
        e.resourceId = owner.second;
        e.start = range.first;
        e.finish = range.second;
        e.work = work(resourcePeriods, r, "act_qty");
        e.actualMinor = money(resourcePeriods, r, "act_cost");
        snapshot.periods.push_back(e);
    }
    const auto &taskPeriods = table(tables, "TASKFIN");
    for (const auto &r : taskPeriods.rows) {
        calculationCheckpoint();
        const auto &range = periods.at(id(taskPeriods, r, "fin_dates_id"));
        XerFinancialPeriod e;
        e.taskId = "xer-task-" + s(id(taskPeriods, r, "task_id"));
        e.start = range.first;
        e.finish = range.second;
        e.laborWork = work(taskPeriods, r, "act_work_qty");
        e.equipmentWork = work(taskPeriods, r, "act_equip_qty");
        e.laborMinor = money(taskPeriods, r, "act_work_cost");
        e.equipmentMinor = money(taskPeriods, r, "act_equip_cost");
        e.materialMinor = money(taskPeriods, r, "act_mat_cost");
        e.expenseMinor = money(taskPeriods, r, "act_expense_cost");
        e.bcwp = money(taskPeriods, r, "bcwp");
        e.bcws = money(taskPeriods, r, "bcws");
        snapshot.periods.push_back(e);
    }
    if (!observed && snapshot.periods.empty())
        return;
    snapshot.projectionSha256 = xerFinancialProjectionHash(p);
    validateXerFinancials(snapshot);
    p.xerFinancials = std::move(snapshot);
}
namespace {
void restoreFinancials(const Project &p, XerTables &tables, QJsonArray &diagnostics) {
    if (!p.xerFinancials)
        return;
    if (!xerFinancialsCurrent(p)) {
        note(diagnostics, "PROJECT/xerFinancials",
             "보존된 XER 집계 이후 문서가 변경됐습니다. 현재 원장만 내보내며 원본 집계는 source.ppm의 별도 스냅샷에 "
             "보존합니다.");
        return;
    }
    const auto &snapshot = *activeXerFinancials(p);
    const auto scale = currencyDecimals(p.currency) ? 100LL : 1LL;
    auto &a = table(tables, "TASKRSRC"), &tasks = table(tables, "TASK");
    std::map<std::string, QString> tids;
    for (std::size_t i = 0; i < p.tasks.size(); ++i)
        tids[p.tasks[i].id] = id(tasks, tasks.rows.at(i), "task_id");
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::size_t> indices;
    std::map<Key, QString> aids;
    for (std::size_t i = 0; i < p.assignments.size(); ++i) {
        Key key{p.assignments[i].taskId, p.assignments[i].resourceId};
        indices[key] = i;
        aids[key] = id(a, a.rows.at(i), "taskrsrc_id");
    }
    std::map<std::string, std::optional<std::int64_t>> resourceCosts;
    for (const auto &e : snapshot.assignments) {
        calculationCheckpoint();
        auto &r = a.rows.at(indices.at({e.taskId, e.resourceId}));
        set(a, r, "act_reg_qty", text(e.regularWork, 6000));
        set(a, r, "act_ot_qty", text(e.overtimeWork, 6000));
        set(a, r, "act_reg_cost", text(e.regularMinor, scale));
        set(a, r, "act_ot_cost", text(e.overtimeMinor, scale));
        add(resourceCosts[e.taskId], e.regularMinor);
        add(resourceCosts[e.taskId], e.overtimeMinor);
    }
    auto &dates = table(tables, "FINDATES"), &rp = table(tables, "TRSRCFIN"), &tp = table(tables, "TASKFIN");
    dates.rows.clear();
    rp.rows.clear();
    tp.rows.clear();
    std::map<Key, QString> periodIds;
    std::map<std::tuple<std::string, std::string, std::string>, std::size_t> taskPeriodRows;
    for (const auto &e : snapshot.periods) {
        calculationCheckpoint();
        auto &pid = periodIds[{e.start, e.finish}];
        if (pid.isEmpty()) {
            pid = QString::number(periodIds.size());
            append(dates, {{"fin_dates_id", pid},
                           {"fin_dates_name", "XER preserved " + pid},
                           {"start_date", q(e.start)},
                           {"end_date", q(e.finish)}});
        }
        if (!e.resourceId.empty())
            append(rp, {{"taskrsrc_id", aids.at({e.taskId, e.resourceId})},
                        {"task_id", tids.at(e.taskId)},
                        {"proj_id", "1"},
                        {"fin_dates_id", pid},
                        {"act_qty", text(e.work, 6000)},
                        {"act_cost", text(e.actualMinor, scale)}});
        else {
            const auto [it, inserted] = taskPeriodRows.emplace(std::tuple{e.taskId, e.start, e.finish}, tp.rows.size());
            const std::map<QString, QString> values{{"task_id", tids.at(e.taskId)},
                                                    {"proj_id", "1"},
                                                    {"fin_dates_id", pid},
                                                    {"act_work_qty", text(e.laborWork, 6000)},
                                                    {"act_equip_qty", text(e.equipmentWork, 6000)},
                                                    {"act_work_cost", text(e.laborMinor, scale)},
                                                    {"act_equip_cost", text(e.equipmentMinor, scale)},
                                                    {"act_mat_cost", text(e.materialMinor, scale)},
                                                    {"act_expense_cost", text(e.expenseMinor, scale)},
                                                    {"bcwp", text(e.bcwp, scale)},
                                                    {"bcws", text(e.bcws, scale)}};
            if (inserted)
                append(tp, values);
            else
                for (const auto &[field, value] : values)
                    if (!value.isEmpty())
                        set(tp, tp.rows.at(it->second), field, value);
        }
    }
    auto &expenses = table(tables, "PROJCOST");
    std::map<QString, const XerFinancialTask *> sourceTasks;
    for (const auto &t : snapshot.tasks)
        sourceTasks[tids.at(t.id)] = &t;
    for (auto &r : expenses.rows) {
        const auto *t = sourceTasks.at(id(expenses, r, "task_id"));
        if (t->actualMinor)
            set(expenses, r, "act_cost", decimal(*t->actualMinor - resourceCosts[t->id].value_or(0), scale));
    }
    note(diagnostics, "PROJECT/xerFinancials",
         "현재 문서와 일치하는 보존 스냅샷의 재무기간·정규/초과근무 누계를 재내보냅니다. 외부 BCWP/BCWS는 원본 값이며 "
         "native EVM에 합산하지 않습니다.");
}
} // namespace
void validateXerActualReferences(XerTables &tables) {
    const auto &tasks = table(tables, "TASK"), &assignments = table(tables, "TASKRSRC");
    std::map<QString, QString> owners;
    std::map<QString, std::pair<QString, QString>> assignmentOwners;
    for (const auto &r : tasks.rows)
        owners[id(tasks, r, "task_id")] = id(tasks, r, "proj_id");
    for (const auto &r : assignments.rows) {
        auto tid = id(assignments, r, "task_id"), pid = id(assignments, r, "proj_id");
        if (!owners.contains(tid) || owners.at(tid) != pid)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_072);
        assignmentOwners[id(assignments, r, "taskrsrc_id")] = {tid, pid};
    }
    std::map<QString, std::pair<QString, QString>> periods;
    const auto &dates = table(tables, "FINDATES");
    for (const auto &r : dates.rows) {
        calculationCheckpoint();
        const auto uid = id(dates, r, "fin_dates_id");
        const auto begin = get(dates, r, "start_date"), end = get(dates, r, "end_date");
        if (!validStamp(s(begin)) || !validStamp(s(end)) || end < begin ||
            !periods.emplace(uid, std::pair{begin, end}).second)
            invalid("FINDATES/date or duplicate");
    }
    for (const auto *name : {"PROJCOST", "TASKFIN", "TRSRCFIN"}) {
        const auto &t = table(tables, name);
        std::set<QString> seen;
        for (const auto &r : t.rows) {
            calculationCheckpoint();
            const auto tid = id(t, r, "task_id"), pid = id(t, r, "proj_id");
            if (!owners.contains(tid) || owners.at(tid) != pid)
                invalid(t.name + "/task ownership");
            auto key = name == QString("PROJCOST") ? id(t, r, "cost_item_id") : id(t, r, "fin_dates_id");
            if (t.name != "PROJCOST" && !periods.contains(key))
                invalid(t.name + "/financial period");
            if (t.name == "TRSRCFIN") {
                const auto aid = id(t, r, "taskrsrc_id");
                if (!assignmentOwners.contains(aid) || assignmentOwners.at(aid) != std::pair{tid, pid})
                    invalid("TRSRCFIN/assignment ownership");
                key += "/" + aid;
            } else if (t.name == "TASKFIN")
                key += "/" + tid;
            if (!seen.insert(key).second)
                invalid(t.name + "/duplicate");
        }
    }
}
QJsonObject importXerActualCosts(Project &p, XerTables &tables, QJsonArray &diagnostics) {
    const auto moneyScale = currencyDecimals(p.currency) == 0 ? 1LL : 100LL;
    auto amount = [&](const XerTable &t, const XerRow &r, const QString &f) {
        return number(get(t, r, f), moneyScale, maxMoney, t.name + "/" + f);
    };
    auto work = [&](const XerTable &t, const XerRow &r, const QString &f) {
        return number(get(t, r, f), 6000, maxWork, t.name + "/" + f, true);
    };
    const auto &project = table(tables, "PROJECT");
    const auto status = get(project, project.rows.at(0), "last_recalc_date");
    if (!status.isEmpty()) {
        if (!validStamp(s(status)))
            invalid("PROJECT/last_recalc_date");
        p.statusAt = s(status);
    }
    const auto recorded = s(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"));
    auto provenance = [&](auto &e, const std::string &uid, const std::string &tid) {
        if (!p.statusAt)
            invalid("actuals require PROJECT/last_recalc_date");
        e.id = uid;
        e.taskId = tid;
        e.statusAt = *p.statusAt;
        e.recordedAt = recorded;
        e.reporter = "Primavera XER import";
        e.reason = "Imported observed snapshot; see original.xer/conversion.json";
        e.sourceDocumentId = p.documentId;
        e.recordedRevision = p.revision;
    };
    std::map<std::string, CostEntry> costs;
    auto &tasks = table(tables, "TASK");
    PlanInput actualCalendarPlan = static_cast<const PlanInput &>(p);
    for (const auto &row : tasks.rows) {
        const auto start = get(tasks, row, "act_start_date");
        if (!start.isEmpty() && validStamp(s(start)) && s(start.left(10)) < actualCalendarPlan.startDate)
            actualCalendarPlan.startDate = s(start.left(10));
    }
    for (std::size_t i = 0; i < tasks.rows.size(); ++i) {
        calculationCheckpoint();
        const auto &r = tasks.rows[i];
        const auto &task = p.tasks.at(i);
        const auto state = get(tasks, r, "status_code"), start = get(tasks, r, "act_start_date"),
                   finish = get(tasks, r, "act_end_date");
        const auto remaining = get(tasks, r, "remain_drtn_hr_cnt");
        if ((!start.isEmpty() && !validStamp(s(start))) || (!finish.isEmpty() && !validStamp(s(finish))))
            invalid("TASK/actual dates");
        if (!state.isEmpty() && state != "TK_NotStart" && state != "TK_Active" && state != "TK_Complete")
            invalid("TASK/status_code");
        if ((state == "TK_NotStart" && (!start.isEmpty() || !finish.isEmpty())) ||
            (state == "TK_Active" && (start.isEmpty() || !finish.isEmpty())) ||
            (state == "TK_Complete" && (start.isEmpty() || finish.isEmpty())))
            invalid("TASK/status dates disagree");
        if (state == "TK_Active" || state == "TK_Complete") {
            if (remaining.isEmpty() && state == "TK_Active")
                note(diagnostics, "TASK/" + q(task.id) + "/act_start_date",
                     "잔여 기간이 없어 실적 원장을 만들지 않았습니다. 원본 날짜를 보존합니다.");
            else {
                ProgressEntry e;
                provenance(e, "xer-progress-" + task.id, task.id);
                e.planDurationMinutes = task.durationMinutes;
                e.actualStart = s(start);
                if (!finish.isEmpty())
                    e.actualFinish = s(finish);
                if (start > status || (!finish.isEmpty() && (finish < start || finish > status)))
                    invalid("TASK/future actual dates");
                e.remainingMinutes = number(remaining, 60, 5000000, "TASK/remain_drtn_hr_cnt", true).value_or(0);
                const WorkCalendar calendar(actualCalendarPlan, task);
                e.actualDurationMinutes =
                    calendar.offset(e.actualFinish.value_or(*p.statusAt)) - calendar.offset(*e.actualStart);
                p.progress.push_back(e);
                note(diagnostics, "TASK/" + q(task.id) + "/actualDuration",
                     "실제 소요 기간은 실제 시작~완료/상태일의 작업 달력 근무분입니다. 관측 실제공수와 구분합니다.");
            }
        } else if (state == "TK_NotStart" && !remaining.isEmpty()) {
            const auto remainingMinutes = number(remaining, 60, 5000000, "TASK/remain_drtn_hr_cnt", true);
            if (remainingMinutes != task.durationMinutes) {
                ProgressEntry e;
                provenance(e, "xer-progress-" + task.id, task.id);
                e.planDurationMinutes = task.durationMinutes;
                e.remainingMinutes = *remainingMinutes;
                p.progress.push_back(e);
            }
        } else if (state.isEmpty() && (!start.isEmpty() || !finish.isEmpty())) {
            note(diagnostics, "TASK/" + q(task.id) + "/status_code",
                 "실제 날짜에 활동 상태가 없어 원장을 만들지 않고 원본에 보존합니다.");
        }
        const auto percent = get(tasks, r, "phys_complete_pct");
        if (!percent.isEmpty() && get(tasks, r, "complete_pct_type") == "CP_Phys") {
            auto value = number(percent, 100, 10000, "TASK/phys_complete_pct");
            auto &e = costs[task.id];
            e.physicalBasisPoints = int(*value);
        } else if (!percent.isEmpty())
            note(diagnostics, "TASK/phys_complete_pct",
                 "진척 유형이 CP_Phys가 아니므로 native 물리 진척으로 추정하지 않습니다.");
    }
    // Default hourly price only. Never reprice imported actual costs.
    const auto &rates = table(tables, "RSRCRATE"), &resources = table(tables, "RSRC");
    std::map<QString, std::size_t> resourceIndex;
    for (std::size_t i = 0; i < resources.rows.size(); ++i)
        resourceIndex[id(resources, resources.rows[i], "rsrc_id")] = i;
    const auto &currencies = table(tables, "CURRTYPE");
    std::map<QString, QString> currencyCodes;
    for (const auto &row : currencies.rows)
        currencyCodes[id(currencies, row, "curr_id")] = get(currencies, row, "curr_short_name");
    std::set<QString> pricedResources;
    for (const auto &row : rates.rows)
        if (!get(rates, row, "cost_per_qty").isEmpty())
            pricedResources.insert(id(rates, row, "rsrc_id"));
    for (const auto &r : rates.rows) {
        const auto value = amount(rates, r, "cost_per_qty");
        if (!value && !pricedResources.contains(id(rates, r, "rsrc_id")))
            continue;
        const auto date = get(rates, r, "start_date");
        if (!validStamp(s(date)) || !date.endsWith(" 00:00"))
            invalid("RSRCRATE/intraday price");
        const auto resourcePosition = resourceIndex.at(id(rates, r, "rsrc_id"));
        const auto &resourceRow = resources.rows.at(resourcePosition);
        const auto priceUnit = get(resources, resourceRow, "cost_qty_type");
        if (!priceUnit.isEmpty() && priceUnit != "QT_Hour")
            invalid("RSRC/cost_qty_type requires hourly prices");
        auto currency = get(resources, resourceRow, "curr_id");
        if (!currency.isEmpty())
            currency = id(resources, resourceRow, "curr_id");
        if (!currency.isEmpty() && (!currencyCodes.contains(currency) || currencyCodes.at(currency) != q(p.currency)))
            invalid("RSRCRATE/foreign or unknown currency; no exchange-rate conversion");
        auto &resource = p.resources.at(resourcePosition);
        resource.rates.push_back({s(date.left(10)), value});
    }
    for (auto &r : p.resources)
        std::sort(r.rates.begin(), r.rates.end(),
                  [](const auto &a, const auto &b) { return a.effectiveDate < b.effectiveDate; });
    struct AssignmentTotal {
        std::string task, resource, type;
        std::optional<std::int64_t> cost, work;
    };
    std::map<QString, AssignmentTotal> totals;
    QJsonArray assignmentDetails, expenseDetails, periodDetails;
    const auto &assignments = table(tables, "TASKRSRC");
    for (const auto &r : assignments.rows) {
        calculationCheckpoint();
        const auto aid = id(assignments, r, "taskrsrc_id");
        const auto tid = "xer-task-" + s(id(assignments, r, "task_id"));
        const auto rid = id(assignments, r, "rsrc_id");
        AssignmentTotal total{tid, "xer-resource-" + s(rid), p.resources.at(resourceIndex.at(rid)).type, {}, {}};
        add(total.cost, amount(assignments, r, "act_reg_cost"));
        add(total.cost, amount(assignments, r, "act_ot_cost"));
        add(total.work, work(assignments, r, "act_reg_qty"), maxWork);
        add(total.work, work(assignments, r, "act_ot_qty"), maxWork);
        add(costs[tid].actualMinor, total.cost);
        QJsonObject detail{{"assignmentId", aid},
                           {"taskId", q(tid)},
                           {"resourceId", q(total.resource)},
                           {"actualMinor", total.cost ? q(std::to_string(*total.cost)) : QString{}},
                           {"actualWorkUnitMinutes", total.work ? q(std::to_string(*total.work)) : QString{}}};
        for (const auto *f :
             {"target_cost", "remain_cost", "target_qty", "remain_qty", "act_this_per_cost", "act_this_per_qty"}) {
            const auto value = get(assignments, r, f);
            if (!value.isEmpty()) {
                if (QString(f).contains("cost"))
                    (void)amount(assignments, r, f);
                else
                    (void)work(assignments, r, f);
                detail[f] = value;
            }
        }
        assignmentDetails.append(detail);
        totals.emplace(aid, total);
        if (total.work)
            note(diagnostics, "TASKRSRC/" + aid + "/actual_qty",
                 "누적 실제공수는 보존 스냅샷과 변환 보고에 기록합니다. 날짜가 있는 TRSRCFIN만 기간 원장에 배분하며 "
                 "차액의 날짜를 "
                 "추정하지 않습니다.");
    }
    std::map<std::string, std::optional<std::int64_t>> expenseTotals, plannedExpenses;
    std::map<std::string, std::string> expenseAccrual;
    std::set<std::string> mixedAccrual;
    const auto &expenses = table(tables, "PROJCOST");
    for (const auto &r : expenses.rows) {
        const auto tid = "xer-task-" + s(id(expenses, r, "task_id"));
        const auto actual = amount(expenses, r, "act_cost"), planned = amount(expenses, r, "target_cost");
        add(costs[tid].actualMinor, actual);
        add(expenseTotals[tid], actual);
        add(plannedExpenses[tid], planned);
        const auto accrual = get(expenses, r, "cost_load_type");
        const auto nativeAccrual = accrual == "CL_Start" ? "start" : accrual == "CL_End" ? "finish" : "uniform";
        if (planned) {
            if (expenseAccrual.contains(tid) && expenseAccrual.at(tid) != nativeAccrual)
                mixedAccrual.insert(tid);
            expenseAccrual[tid] = nativeAccrual;
        }
        if (!accrual.isEmpty() && accrual != "CL_Uniform" && accrual != "CL_Start" && accrual != "CL_End")
            note(diagnostics, "PROJCOST/cost_load_type",
                 "알 수 없는 비용 발생 규칙은 원본/보고에 보존하며 native 고정비는 균등 투영합니다.");
        expenseDetails.append(QJsonObject{{"expenseId", id(expenses, r, "cost_item_id")},
                                          {"taskId", q(tid)},
                                          {"name", get(expenses, r, "cost_name")},
                                          {"actualCost", get(expenses, r, "act_cost")},
                                          {"plannedCost", get(expenses, r, "target_cost")},
                                          {"accrual", accrual},
                                          {"remainingCost", get(expenses, r, "remain_cost")}});
        (void)amount(expenses, r, "remain_cost");
    }
    for (const auto &[tid, value] : plannedExpenses)
        if (value)
            p.taskCosts.push_back(
                {tid, value, mixedAccrual.contains(tid) ? "uniform" : expenseAccrual.at(tid), "physical"});
    for (const auto &tid : mixedAccrual)
        note(diagnostics, "PROJCOST/" + q(tid) + "/cost_load_type",
             "같은 작업의 서로 다른 비용 발생 규칙은 합산 고정비의 균등 발생으로 투영합니다. 개별 항목은 보고/원본에 "
             "보존합니다.");
    std::map<QString, std::pair<QString, QString>> periods;
    const auto &dates = table(tables, "FINDATES");
    for (const auto &r : dates.rows)
        periods[id(dates, r, "fin_dates_id")] = {get(dates, r, "start_date"), get(dates, r, "end_date")};
    std::map<QString, std::optional<std::int64_t>> datedCost, datedWork;
    std::map<std::pair<QString, QString>, std::optional<std::int64_t>> periodResourceCost;
    auto entry = [&](const std::string &tid, const std::string &rid, const std::string &type, const QString &pid,
                     std::optional<std::int64_t> units, std::optional<std::int64_t> value) {
        if (!units && !value)
            return;
        const auto [begin, end] = periods.at(pid);
        if (!p.statusAt || end.left(10) > q(*p.statusAt).left(10))
            invalid("financial period exceeds status date");
        if (begin.left(10) != end.left(10)) {
            note(diagnostics, "FINDATES/" + pid,
                 "여러 날짜의 집계는 일별 원장으로 임의 배분하지 않습니다. 보존 스냅샷·원본/변환 보고에 기록합니다.");
            return;
        }
        if (p.periodActuals.size() >= 50000)
            throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
        PeriodActualEntry e;
        provenance(e, "xer-period-" + std::to_string(p.periodActuals.size() + 1), tid);
        e.resourceId = rid;
        e.resourceType = type;
        e.date = s(begin.left(10));
        e.workUnitMinutes = units;
        e.actualMinor = value;
        e.currency = p.currency;
        if (value)
            e.costEntryId = "xer-cost-" + tid;
        p.periodActuals.push_back(e);
    };
    const auto &financial = table(tables, "TRSRCFIN");
    for (const auto &r : financial.rows) {
        calculationCheckpoint();
        const auto aid = id(financial, r, "taskrsrc_id"), pid = id(financial, r, "fin_dates_id");
        const auto &total = totals.at(aid);
        const auto value = amount(financial, r, "act_cost"), units = work(financial, r, "act_qty");
        add(datedCost[aid], value);
        add(datedWork[aid], units, maxWork);
        add(periodResourceCost[{pid, q(total.task)}], value);
        entry(total.task, total.resource, total.type, pid, units, value);
        periodDetails.append(QJsonObject{{"table", "TRSRCFIN"},
                                         {"assignmentId", aid},
                                         {"periodId", pid},
                                         {"start", periods.at(pid).first},
                                         {"finish", periods.at(pid).second},
                                         {"actualCost", get(financial, r, "act_cost")},
                                         {"actualQuantity", get(financial, r, "act_qty")}});
    }
    for (const auto &[aid, total] : totals) {
        if ((datedCost[aid] && (!total.cost || *datedCost[aid] > *total.cost)) ||
            (datedWork[aid] && (!total.work || *datedWork[aid] > *total.work)))
            invalid("TRSRCFIN exceeds cumulative assignment actual");
    }
    std::map<std::string, std::optional<std::int64_t>> datedExpenses;
    const auto &taskFinancial = table(tables, "TASKFIN");
    for (const auto &r : taskFinancial.rows) {
        calculationCheckpoint();
        const auto tid = "xer-task-" + s(id(taskFinancial, r, "task_id"));
        const auto pid = id(taskFinancial, r, "fin_dates_id");
        const auto value = amount(taskFinancial, r, "act_expense_cost");
        add(datedExpenses[tid], value);
        entry(tid, "", "", pid, {}, value);
        // TASKFIN resource amounts summarize TRSRCFIN; they are never an additional AC.
        QJsonObject detail{{"table", "TASKFIN"},
                           {"taskId", q(tid)},
                           {"periodId", pid},
                           {"start", periods.at(pid).first},
                           {"finish", periods.at(pid).second}};
        std::optional<std::int64_t> resourceCost;
        for (const auto *f : {"act_work_cost", "act_equip_cost", "act_mat_cost", "bcwp", "bcws", "act_expense_cost"}) {
            auto n = amount(taskFinancial, r, f);
            detail[f] = get(taskFinancial, r, f);
            if (QString(f) == "act_work_cost" || QString(f) == "act_equip_cost" || QString(f) == "act_mat_cost")
                add(resourceCost, n);
        }
        for (const auto *f : {"act_work_qty", "act_equip_qty"}) {
            (void)work(taskFinancial, r, f);
            detail[f] = get(taskFinancial, r, f);
        }
        if (resourceCost && *resourceCost != periodResourceCost[{pid, q(tid)}].value_or(0))
            note(diagnostics, "TASKFIN/resource_cost",
                 "작업별 과거 기간 합계와 자원 명세가 다릅니다. 원장을 중복 생성하지 않고 보고에 보존합니다.");
        periodDetails.append(detail);
    }
    for (const auto &[tid, total] : datedExpenses)
        if (total && (!expenseTotals[tid] || *total > *expenseTotals[tid]))
            invalid("TASKFIN expense exceeds cumulative expense");
    for (auto &[tid, e] : costs)
        if (e.actualMinor || e.physicalBasisPoints) {
            provenance(e, "xer-cost-" + tid, tid);
            e.currency = p.currency;
            p.costEntries.push_back(e);
        }
    auto result = summary(p);
    result["assignmentTotals"] = assignmentDetails;
    result["expenses"] = expenseDetails;
    result["financialPeriods"] = periodDetails;
    note(diagnostics, "PROJECT/actual-costs",
         "실적/원가는 상태일의 새 관측 스냅샷입니다. 과거 정정·승인·기준선/P6 BCWP·BCWS는 재현하지 않습니다. 누계 "
         "AC에는 기간 배분액이 포함됩니다.");
    return result;
}
QJsonObject exportXerActualCosts(const Project &p, XerTables &tables, QJsonArray &diagnostics) {
    const auto moneyScale = currencyDecimals(p.currency) == 0 ? 1LL : 100LL;
    auto &project = table(tables, "PROJECT");
    set(project, project.rows.at(0), "last_recalc_date", p.statusAt ? q(*p.statusAt) : QString{});
    std::map<std::string, QString> taskIds, resourceIds;
    std::map<std::string, const ProgressEntry *> currentProgress;
    std::map<std::string, const CostEntry *> currentCosts;
    if (p.statusAt) {
        for (const auto &entry : p.progress)
            if (entry.statusAt <= *p.statusAt) {
                auto &old = currentProgress[entry.taskId];
                if (!old || old->statusAt <= entry.statusAt)
                    old = &entry;
            }
        for (const auto &entry : p.costEntries)
            if (entry.statusAt <= *p.statusAt) {
                auto &old = currentCosts[entry.taskId];
                if (!old || old->statusAt <= entry.statusAt)
                    old = &entry;
            }
    }
    auto &tasks = table(tables, "TASK");
    for (const auto *field : {"act_start_date", "act_end_date", "phys_complete_pct"}) {
        if (tasks.fields.contains(field))
            continue;
        tasks.fields.append(field);
        for (auto &row : tasks.rows)
            row.cells.append(QString{});
    }
    for (std::size_t i = 0; i < p.tasks.size(); ++i) {
        calculationCheckpoint();
        auto &row = tasks.rows.at(i);
        const auto &task = p.tasks[i];
        taskIds[task.id] = id(tasks, row, "task_id");
        const auto *progress = currentProgress[task.id];
        const auto *cost = currentCosts[task.id];
        set(tasks, row, "status_code",
            progress ? (progress->actualFinish  ? "TK_Complete"
                        : progress->actualStart ? "TK_Active"
                                                : "TK_NotStart")
                     : "TK_NotStart");
        set(tasks, row, "remain_drtn_hr_cnt",
            progress ? decimal(progress->remainingMinutes, 60) : get(tasks, row, "target_drtn_hr_cnt"));
        set(tasks, row, "act_start_date", progress && progress->actualStart ? q(*progress->actualStart) : QString{});
        set(tasks, row, "act_end_date", progress && progress->actualFinish ? q(*progress->actualFinish) : QString{});
        set(tasks, row, "complete_pct_type", cost && cost->physicalBasisPoints ? "CP_Phys" : "CP_Drtn");
        set(tasks, row, "phys_complete_pct",
            cost && cost->physicalBasisPoints ? decimal(*cost->physicalBasisPoints, 100) : QString{});
        if (progress)
            note(diagnostics, "TASK/" + q(task.id) + "/actualDuration",
                 "실제 날짜·잔여 기간을 내보냅니다. native 실제 소요 기간은 P6 별도 필드로 보존하지 않으며 날짜 "
                 "달력에서 재계산될 수 있습니다.");
    }
    auto &resources = table(tables, "RSRC");
    if (!resources.fields.contains("cost_qty_type")) {
        resources.fields.append("cost_qty_type");
        for (auto &row : resources.rows)
            row.cells.append("QT_Hour");
    }
    for (std::size_t i = 0; i < p.resources.size(); ++i)
        resourceIds[p.resources[i].id] = id(resources, resources.rows.at(i), "rsrc_id");
    auto &rates = table(tables, "RSRCRATE");
    rates.rows.clear();
    rates.fields = {"rsrc_rate_id", "rsrc_id", "start_date", "max_qty_per_hr", "cost_per_qty"};
    int next = 1;
    for (const auto &resource : p.resources) {
        std::map<std::string, std::optional<std::int64_t>> prices;
        for (const auto &rate : resource.rates) {
            prices[rate.effectiveDate] = rate.hourlyMinor;
            if (!rate.hourlyMinor)
                note(diagnostics, "RSRCRATE/" + q(resource.id),
                     "미정 단가 행은 외부 빈 값으로 기록합니다. 외부 reader는 단가 삭제/미설정을 구분하지 않을 수 "
                     "있으며 source.ppm에 보존합니다.");
        }
        if (resource.maxUnits && (prices.empty() || prices.begin()->first > p.startDate))
            prices.emplace(p.startDate, std::nullopt);
        for (const auto &[date, price] : prices)
            append(rates, {{"rsrc_rate_id", QString::number(next++)},
                           {"rsrc_id", resourceIds.at(resource.id)},
                           {"start_date", q(date) + " 00:00"},
                           {"max_qty_per_hr", resource.maxUnits ? decimal(*resource.maxUnits, 100) : QString{}},
                           {"cost_per_qty", text(price, moneyScale)}});
    }
    struct Total {
        std::optional<std::int64_t> cost, work;
    };
    using Pair = std::pair<std::string, std::string>;
    std::map<Pair, Total> totals;
    std::map<std::string, std::optional<std::int64_t>> resourceCosts, directCosts;
    auto &dates = table(tables, "FINDATES"), &financial = table(tables, "TRSRCFIN"),
         &taskFinancial = table(tables, "TASKFIN");
    // Keep identical empty table schemas so combined plan/actual projects remain composable.
    dates.fields = {"fin_dates_id", "fin_dates_name", "start_date", "end_date"};
    financial.fields = {"taskrsrc_id", "task_id", "proj_id", "fin_dates_id", "act_qty", "act_cost"};
    taskFinancial.fields = {"task_id",       "proj_id",        "fin_dates_id", "act_work_qty",     "act_equip_qty",
                            "act_work_cost", "act_equip_cost", "act_mat_cost", "act_expense_cost", "bcwp",
                            "bcws"};
    auto &assignments = table(tables, "TASKRSRC");
    for (const auto *field : {"act_reg_qty", "act_ot_qty", "act_reg_cost", "act_ot_cost", "cost_qty_link_flag",
                              "act_start_date", "act_end_date"}) {
        if (assignments.fields.contains(field))
            continue;
        assignments.fields.append(field);
        for (auto &row : assignments.rows)
            row.cells.append(QString{});
    }
    std::map<Pair, QString> assignmentIds;
    for (std::size_t i = 0; i < p.assignments.size(); ++i) {
        const auto &a = p.assignments[i];
        assignmentIds[{a.taskId, a.resourceId}] = id(assignments, assignments.rows.at(i), "taskrsrc_id");
    }
    std::map<std::string, QString> periodIds;
    struct PeriodTotal {
        std::optional<std::int64_t> laborWork, equipWork, laborCost, equipCost, expense;
    };
    std::map<std::pair<std::string, std::string>, PeriodTotal> taskPeriods;
    const auto current =
        p.statusAt ? periodActualsAsOf(p, *p.statusAt) : std::map<PeriodActualKey, const PeriodActualEntry *>{};
    for (const auto &[task, allocation] :
         p.statusAt ? actualCostAllocations(p, *p.statusAt) : std::map<std::string, ActualCostAllocation>{})
        if (allocation.costReports && !allocation.cumulative)
            invalid("TASK/" + q(task) + "/dated cost requires an explicit cumulative AC before export");
    for (const auto &[key, e] : current) {
        (void)key;
        calculationCheckpoint();
        const Pair pair{e->taskId, e->resourceId};
        if (!e->resourceId.empty() && !assignmentIds.contains(pair)) {
            note(diagnostics, "TASKRSRC/actuals/" + q(e->taskId) + "/" + q(e->resourceId),
                 "현재 배정이 없는 과거 자원 실적은 작업 미배분 원가와 source.ppm에 보존합니다. 계획 배정을 생성하지 "
                 "않습니다.");
            continue;
        }
        auto &period = periodIds[e->date];
        if (period.isEmpty()) {
            period = QString::number(periodIds.size());
            append(dates, {{"fin_dates_id", period},
                           {"fin_dates_name", "PPM " + q(e->date)},
                           {"start_date", q(e->date) + " 00:00"},
                           {"end_date", q(e->date) + " 23:59"}});
        }
        auto &task = taskPeriods[{e->taskId, e->date}];
        if (e->resourceId.empty()) {
            add(task.expense, e->actualMinor);
            add(directCosts[e->taskId], e->actualMinor);
        } else {
            auto &total = totals[pair];
            add(total.cost, e->actualMinor);
            add(total.work, e->workUnitMinutes, maxWork);
            add(resourceCosts[e->taskId], e->actualMinor);
            const bool equipment = e->resourceType == "equipment";
            add(equipment ? task.equipCost : task.laborCost, e->actualMinor);
            add(equipment ? task.equipWork : task.laborWork, e->workUnitMinutes, maxWork);
            append(financial, {{"taskrsrc_id", assignmentIds.at(pair)},
                               {"task_id", taskIds.at(e->taskId)},
                               {"proj_id", "1"},
                               {"fin_dates_id", period},
                               {"act_qty", text(e->workUnitMinutes, 6000)},
                               {"act_cost", text(e->actualMinor, moneyScale)}});
        }
    }
    for (std::size_t i = 0; i < p.assignments.size(); ++i) {
        auto &row = assignments.rows.at(i);
        const auto &a = p.assignments[i];
        const auto &total = totals[{a.taskId, a.resourceId}];
        if (const auto *progress = currentProgress[a.taskId]) {
            // Remaining duration is supported; assignment-level remaining work is not inferred from it.
            set(assignments, row, "remain_qty", progress->actualFinish ? "0.000000" : QString{});
        }
        set(assignments, row, "act_reg_qty", text(total.work, 6000));
        set(assignments, row, "act_ot_qty", {});
        set(assignments, row, "act_reg_cost", text(total.cost, moneyScale));
        set(assignments, row, "act_ot_cost", {});
        set(assignments, row, "cost_qty_link_flag", "N");
        set(assignments, row, "act_start_date", {});
        set(assignments, row, "act_end_date", {});
    }
    for (const auto &[key, total] : taskPeriods)
        append(taskFinancial, {{"task_id", taskIds.at(key.first)},
                               {"proj_id", "1"},
                               {"fin_dates_id", periodIds.at(key.second)},
                               {"act_work_qty", text(total.laborWork, 6000)},
                               {"act_equip_qty", text(total.equipWork, 6000)},
                               {"act_work_cost", text(total.laborCost, moneyScale)},
                               {"act_equip_cost", text(total.equipCost, moneyScale)},
                               {"act_expense_cost", text(total.expense, moneyScale)}});
    auto &expenses = table(tables, "PROJCOST");
    expenses.fields = {"cost_item_id", "proj_id",        "task_id",
                       "cost_name",    "cost_load_type", "target_cost",
                       "act_cost",     "remain_cost",    "auto_compute_act_flag"};
    std::map<std::string, std::optional<std::int64_t>> planned;
    std::map<std::string, QString> accruals;
    for (const auto &cost : p.taskCosts) {
        planned[cost.taskId] = cost.fixedMinor;
        accruals[cost.taskId] = cost.accrual == "start"    ? "CL_Start"
                                : cost.accrual == "finish" ? "CL_End"
                                                           : "CL_Uniform";
        if (cost.credit != "physical")
            note(diagnostics, "PROJCOST/" + q(cost.taskId) + "/credit",
                 "고정비의 native 완료 인정 규칙은 외부 EVM에 이식하지 않으며 source.ppm에 보존합니다.");
    }
    next = 1;
    for (const auto &task : p.tasks) {
        const auto *cost = currentCosts[task.id];
        std::optional<std::int64_t> actual;
        if (cost && cost->actualMinor)
            actual = *cost->actualMinor - resourceCosts[task.id].value_or(0);
        else
            actual = directCosts[task.id];
        if (actual && *actual < 0)
            invalid("resource actuals exceed cumulative AC");
        if (!actual && !planned[task.id])
            continue;
        append(expenses, {{"cost_item_id", QString::number(next++)},
                          {"proj_id", "1"},
                          {"task_id", taskIds.at(task.id)},
                          {"cost_name", "PPM direct/unallocated cost"},
                          {"cost_load_type", accruals.contains(task.id) ? accruals.at(task.id) : "CL_Uniform"},
                          {"target_cost", text(planned[task.id], moneyScale)},
                          {"act_cost", text(actual, moneyScale)},
                          {"auto_compute_act_flag", "N"}});
    }
    note(diagnostics, "PROJECT/actual-costs",
         "상태일의 유효 실적만 투영합니다. 원장 정정 이력·기준선/EVM은 source.ppm에 보존합니다. 배정 누계+직접/미배분 "
         "비용=누계 AC이며 TASKFIN은 추가 원가가 아닙니다.");
    if (!currentProgress.empty())
        note(diagnostics, "TASKRSRC/remain_qty",
             "배정별 잔여 공수는 완료 작업의 0만 확정합니다. 그 외는 공란이며 실제 날짜·잔여 기간에서 관측 공수를 "
             "추정하지 않습니다.");
    restoreFinancials(p, tables, diagnostics);
    if (!p.financialPeriodEvents.empty())
        note(diagnostics, "PROJECT/FinancialPeriods",
             "사용자 기간·마감 이력은 XER에 매핑하지 않습니다. source.ppm에 보존합니다.");
    if (!p.externalEvmAdoptions.empty())
        note(diagnostics, "PROJECT/ExternalEvmAdoptions",
             "외부 XER EV/PV native 채택/철회 정책은 XER에 매핑하지 않습니다. source.ppm에 전체 이력을 보존합니다.");
    auto result = summary(p);
    result["financialAdjustmentCount"] = int(p.xerFinancialRevisions.size());
    result["sourceRelinkCount"] = int(std::count_if(p.xerFinancialRevisions.begin(), p.xerFinancialRevisions.end(),
                                                    [](const auto &r) { return r.relink.has_value(); }));
    result["scheduleAdoptionCount"] = int(p.xerScheduleAdoptions.size());
    result["ledgerIdMigrationCount"] = int(p.ledgerIdMigrations.size());
    result["projectCurrencyConversionCount"] = int(p.currencyConversions.size());
    if (!p.currencyConversions.empty())
        note(diagnostics, "PROJECT/CurrencyConversions",
             "현재 통화의 환산 투영을 내보냅니다. 원래 제출 금액·환율·전후 문서 이력은 source.ppm에 보존합니다.");
    if (!p.ledgerIdMigrations.empty())
        note(diagnostics, "PROJECT/LedgerIdMigrations",
             "관측 원장 ID 이관의 원본·연결·전후 이력은 source.ppm에 보존합니다.");
    if (!p.xerScheduleAdoptions.empty())
        note(diagnostics, "PROJECT/XerScheduleAdoptions",
             "일정·WBS·달력 채택 원본과 연결/전후 이력은 source.ppm에 보존합니다.");
    result["sourceConversionCount"] =
        int(std::count_if(p.xerFinancialRevisions.begin(), p.xerFinancialRevisions.end(), [](const auto &r) {
            return r.relink && (r.relink->incomingConversion || r.relink->previousConversion);
        }));
    result["nativeEvmAdoptionCount"] = int(p.externalEvmAdoptions.size());
    result["nativeFinancialPeriodEventCount"] = int(p.financialPeriodEvents.size());
    result["financialMergeCount"] = int(std::count_if(p.xerFinancialRevisions.begin(), p.xerFinancialRevisions.end(),
                                                      [](const auto &r) { return r.merge.has_value(); }));
    result["preservedFinancials"] =
        p.xerFinancials ? (xerFinancialsCurrent(p) ? "reexported" : "stale-source-only") : "absent";
    return result;
}
} // namespace jobs
