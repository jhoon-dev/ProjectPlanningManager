#include "persistence/XmlPeriodAllocation.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "application/PeriodActualEditing.h"
#include "calculation/WorkCalendar.h"
#include "domain/ActualProvenance.h"
#include "domain/CalculationControl.h"
#include "domain/PeriodActuals.h"
#include "domain/FinancialPeriods.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <set>
namespace ppm {
namespace {
using Pair = std::pair<std::string, std::string>;
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::XML_PROGRESS_INVALID, {s});
}
QString qs(const std::string &s) {
    return QString::fromStdString(s);
}
std::string hash(const QByteArray &s) {
    return QCryptographicHash::hash(s, QCryptographicHash::Sha256).toHex().toStdString();
}
std::string hash(const QJsonObject &o) {
    return hash(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonValue number(std::optional<std::int64_t> n) {
    return n ? QJsonValue(double(*n)) : QJsonValue(QJsonValue::Null);
}
std::int64_t minute(const std::string &s) {
    return QDate(1970, 1, 1).daysTo(QDate::fromString(qs(s.substr(0, 10)), Qt::ISODate)) * 1440 +
           std::stoi(s.substr(11, 2)) * 60 + std::stoi(s.substr(14, 2));
}
struct Day {
    std::string date, start, finish;
    std::int64_t weight = 0, amount = 0, remainder = 0;
};
struct Cell {
    std::optional<std::int64_t> work, cost;
    std::string parts;
};
struct Group {
    Pair target;
    std::vector<const XmlActualInterval *> records;
    std::map<std::string, Cell> cells;
    QJsonArray ranges;
    std::string marker;
    bool applied = false;
};
QJsonArray manifest(const std::vector<PeriodActualSubmission> &batches) {
    QJsonArray result;
    for (const auto &s : batches) {
        QJsonArray rows, totals;
        for (const auto &e : s.entries)
            rows.append(QJsonObject{{"task", qs(e.taskId)},
                                    {"resource", qs(e.resourceId)},
                                    {"date", qs(e.date)},
                                    {"work", number(e.workUnitMinutes)},
                                    {"cost", number(e.actualMinor)}});
        for (const auto &v : s.totals)
            totals.append(QJsonObject{{"task", qs(v.taskId)}, {"cost", number(v.actualMinor)}});
        result.append(QJsonObject{{"reporter", qs(s.reporter)},
                                  {"reason", qs(s.reason)},
                                  {"recordedAt", qs(s.recordedAt)},
                                  {"entries", rows},
                                  {"totals", totals}});
    }
    return result;
}
void add(std::optional<std::int64_t> &to, std::int64_t n, std::int64_t limit) {
    if (to.value_or(0) > limit - n)
        invalid("선택 구간의 날짜별 합계가 한도를 초과합니다.");
    to = to.value_or(0) + n;
}
std::vector<Day> days(const XmlActualInterval &r) {
    std::vector<Day> result;
    auto date = QDate::fromString(qs(r.start.substr(0, 10)), Qt::ISODate);
    for (; date.toString(Qt::ISODate).toStdString() + " 00:00" < r.finish; date = date.addDays(1)) {
        calculationCheckpoint();
        if (!date.isValid() || result.size() == 366)
            invalid("한 구간은 최대366개 날짜까지 배분할 수 있습니다.");
        const auto name = date.toString(Qt::ISODate).toStdString();
        const auto next = date.addDays(1);
        if (!next.isValid() || !validDate(next.toString(Qt::ISODate).toStdString()))
            invalid("다음 날짜 경계를 표현할 수 없습니다.");
        result.push_back({name, std::max(r.start, name + " 00:00"),
                          std::min(r.finish, next.toString(Qt::ISODate).toStdString() + " 00:00")});
    }
    return result;
}
void apportion(std::vector<Day> &rows, std::int64_t amount, std::int64_t total) {
    std::int64_t assigned = 0;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto &d = rows[i];
        const auto small = (amount % total) * d.weight;
        d.amount = (amount / total) * d.weight + small / total;
        d.remainder = small % total;
        assigned += d.amount;
        if (d.weight)
            order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        return rows[a].remainder != rows[b].remainder ? rows[a].remainder > rows[b].remainder
                                                      : rows[a].date < rows[b].date;
    });
    const auto extra = amount - assigned;
    if (extra < 0 || std::size_t(extra) > order.size())
        invalid("정수 배분의 잔여를 확인하세요.");
    for (std::int64_t i = 0; i < extra; ++i)
        ++rows[order[std::size_t(i)]].amount;
}
} // namespace
XmlActualAllocation prepareXmlActualAllocation(const Project &p, const ProjectXmlExchange &input,
                                               const XmlProgressOptions &options,
                                               const std::vector<XmlPeriodMapping> &mappings,
                                               const XmlAllocationPolicy &policy) {
    EvaluationScope scope;
    calculationCheckpoint();
    if (!p.statusAt || options.statusAt != *p.statusAt || !options.confirmed || !policy.rangeTotalsConfirmed ||
        mappings.empty() || mappings.size() > 1000 || !validUtcStamp(options.recordedAt) ||
        qs(options.reporter).trimmed().isEmpty() || options.reporter.size() > 512 ||
        qs(options.reason).trimmed().isEmpty() || options.reason.size() > 2048 ||
        (policy.basis != XmlAllocationBasis::ElapsedMinutes &&
         policy.basis != XmlAllocationBasis::WorkingIntersectionMinutes))
        invalid("현재 상태일·배분 기준·구간 전체 총량/비례 추정 확인·작성자/사유·선택 구간이 필요합니다.");
    validate(p);
    const auto sha = hash(input.xml);
    if (!input.importing || input.report["source"].toObject()["sha256"].toString().toStdString() != sha)
        invalid("원본 XML 해시가 변경되었습니다.");
    auto original = importProjectXml(input.xml, input.report["source"].toObject()["name"].toString());
    original.project.documentId = input.project.documentId;
    if (input.project != original.project || input.actualIntervals != original.actualIntervals ||
        input.periodCandidates != original.periodCandidates || input.actualCurrency != original.actualCurrency)
        invalid("변경되지 않은 원본 구간 검토본이 필요합니다.");
    const auto basis = policy.basis == XmlAllocationBasis::ElapsedMinutes
                           ? std::string("elapsed-minute-v1")
                           : std::string("working-intersection-minute-v1");
    std::map<std::string, const XmlActualInterval *> intervals;
    std::map<std::string, std::vector<const XmlActualInterval *>> byAssignment;
    for (const auto &r : original.actualIntervals) {
        intervals.emplace(r.key, &r);
        byAssignment[r.assignmentUid].push_back(&r);
    }
    std::map<std::string, const Task *> tasks;
    std::map<std::string, const Resource *> resources;
    std::set<Pair> assignments;
    for (const auto &t : p.tasks)
        tasks[t.id] = &t;
    for (const auto &r : p.resources)
        resources[r.id] = &r;
    for (const auto &a : p.assignments)
        assignments.emplace(a.taskId, a.resourceId);
    std::map<std::string, Group> groups;
    std::set<std::string> selected;
    std::map<std::string, std::string> taskMap, resourceMap;
    for (const auto &m : mappings) {
        if (!selected.insert(m.key).second || !intervals.contains(m.key) ||
            !assignments.contains({m.taskId, m.resourceId}) || !resources.contains(m.resourceId) ||
            (resources.at(m.resourceId)->type != "labor" && resources.at(m.resourceId)->type != "equipment"))
            invalid("선택 구간/현재 노동·장비 배정의 중복·연결을 확인하세요.");
        const auto &r = *intervals.at(m.key);
        if (!r.multipleDays || !r.issue.empty() || (!r.workUnitMinutes && !r.actualMinor))
            invalid("정상적인 명시 다일 구간만 비례 배분할 수 있습니다: " + r.key + " " + r.issue);
        if ((!r.wholeDays && r.finish > *p.statusAt))
            invalid("상태일 이후 시각까지인 구간 전체를 일부 잘라 배분하지 않습니다.");
        if ((taskMap.contains(r.taskUid) && taskMap.at(r.taskUid) != m.taskId) ||
            (resourceMap.contains(r.resourceUid) && resourceMap.at(r.resourceUid) != m.resourceId))
            invalid("동일 원본 작업/자원 UID의 native 연결이 다릅니다.");
        taskMap[r.taskUid] = m.taskId;
        resourceMap[r.resourceUid] = m.resourceId;
        auto &g = groups[r.assignmentUid];
        if (!g.records.empty() && g.target != Pair{m.taskId, m.resourceId})
            invalid("동일 원본 배정의 연결이 다릅니다.");
        g.target = {m.taskId, m.resourceId};
        g.records.push_back(&r);
        if (g.records.size() > 32)
            invalid("한 원본 배정의 선택은 최대32구간입니다.");
    }
    std::map<std::string, std::unique_ptr<WorkCalendar>> taskCalendars, resourceCalendars;
    std::size_t touched = 0;
    QJsonArray rangeReport, dayReport;
    std::map<PeriodActualKey, std::string> owners;
    const auto current = periodActualsAsOf(p, *p.statusAt);
    std::map<std::string, std::int64_t> sourceTotals;
    for (auto &[uid, g] : groups) {
        calculationCheckpoint();
        std::sort(g.records.begin(), g.records.end(), [](auto a, auto b) { return a->sourceIndex < b->sourceIndex; });
        std::string indices;
        for (const auto *r : g.records) {
            for (const auto *other : byAssignment.at(uid)) {
                if (!other->issue.empty())
                    invalid("같은 원본 배정의 오류 구간을 먼저 검토하세요: " + other->key);
                if (other->key != r->key && other->type == r->type && other->start < r->finish &&
                    r->start < other->finish)
                    invalid("같은 종류의 원본 구간이 겹치거나 중복되어 총량을 배분할 수 없습니다.");
            }
            auto rows = days(*r);
            touched += rows.size();
            if (touched > 1000)
                invalid("선택 구간은 합계1000개 날짜 버킷까지 검토할 수 있습니다.");
            if (rows.empty() || rows.back().date > p.statusAt->substr(0, 10))
                invalid("상태일 이후 날짜의 실적을 배분할 수 없습니다.");
            if (policy.basis == XmlAllocationBasis::ElapsedMinutes) {
                for (auto &d : rows)
                    d.weight = minute(d.finish) - minute(d.start);
            } else {
                if (!taskCalendars.contains(g.target.first))
                    taskCalendars[g.target.first] = std::make_unique<WorkCalendar>(p, *tasks.at(g.target.first));
                if (!resourceCalendars.contains(g.target.second)) {
                    Task t;
                    t.calendarId = resources.at(g.target.second)->calendarId;
                    resourceCalendars[g.target.second] = std::make_unique<WorkCalendar>(p, t);
                }
                std::vector<std::pair<std::string, std::string>> a, b;
                try {
                    a = taskCalendars.at(g.target.first)->segments(r->start, r->finish);
                    b = resourceCalendars.at(g.target.second)->segments(r->start, r->finish);
                } catch (const AppError &) {
                    invalid(
                        "근무분 기준 구간이 현재 계획 달력의 지원 범위를 벗어납니다. 경과분 기준을 별도로 검토하세요.");
                }
                std::size_t i = 0, j = 0;
                while (i < a.size() && j < b.size()) {
                    const auto lo = std::max(a[i].first, b[j].first), hi = std::min(a[i].second, b[j].second);
                    if (lo < hi)
                        for (auto &d : rows) {
                            const auto s = std::max(lo, d.start), e = std::min(hi, d.finish);
                            if (s < e)
                                d.weight += minute(e) - minute(s);
                        }
                    if (a[i].second < b[j].second)
                        ++i;
                    else
                        ++j;
                }
            }
            std::int64_t total = 0;
            for (const auto &d : rows)
                total += d.weight;
            if (total <= 0)
                invalid("배분 가중분이0인 구간입니다. 0/0 배분이나 근무일 생성을 하지 않습니다.");
            const auto amount = (r->type == 2 ? r->workUnitMinutes : r->actualMinor).value();
            apportion(rows, amount, total);
            QJsonArray weights;
            for (const auto &d : rows) {
                weights.append(QJsonObject{{"date", qs(d.date)},
                                           {"weight", double(d.weight)},
                                           {"amount", double(d.amount)},
                                           {"remainder", double(d.remainder)}});
                if (!d.weight)
                    continue;
                auto &cell = g.cells[d.date];
                if (r->type == 2)
                    add(cell.work, d.amount, maxWork);
                else
                    add(cell.cost, d.amount, maxMoney);
                if (!cell.parts.empty())
                    cell.parts += ",";
                cell.parts += std::to_string(r->sourceIndex) + (r->type == 2 ? "w=" : "c=") + std::to_string(d.weight) +
                              "/" + std::to_string(total) + ":" + std::to_string(d.amount);
            }
            QJsonObject rr{{"key", qs(r->key)},        {"record", double(r->sourceIndex)},
                           {"type", r->type},          {"unit", r->unit},
                           {"start", qs(r->start)},    {"finish", qs(r->finish)},
                           {"amount", double(amount)}, {"totalWeight", double(total)},
                           {"days", weights}};
            g.ranges.append(rr);
            if (!indices.empty())
                indices += ",";
            indices += std::to_string(r->sourceIndex);
        }
        const auto inputHash = hash(QJsonObject{{"basis", qs(basis)}, {"ranges", g.ranges}});
        g.marker = std::string(xmlProrataPrefix) + sha + " assignment=" + uid + " basis=" + basis +
                   " input=" + inputHash + " records=" + indices + "]";
        const auto prefix = std::string(xmlProrataPrefix) + sha + " assignment=" + uid + " ";
        std::set<std::string> markers, datesSeen;
        std::set<Pair> targets;
        for (const auto &old : p.periodActuals)
            if (old.statusAt == *p.statusAt && old.reason.starts_with(prefix)) {
                const auto end = old.reason.find(']');
                if (end == std::string::npos)
                    invalid("기존 배분 출처가 불완전합니다.");
                markers.insert(old.reason.substr(0, end + 1));
                targets.emplace(old.taskId, old.resourceId);
                datesSeen.insert(old.date);
            }
        if (!markers.empty()) {
            if (markers != std::set<std::string>{g.marker} || targets != std::set<Pair>{g.target})
                invalid("이 원본 배정은 같은 상태일에 다른 선택/기준/가중치/대상으로 이미 배분됐습니다. 기존 정정은 "
                        "유지하며 새 상태일 또는 명시 정정을 검토하세요.");
            std::set<std::string> expected;
            for (const auto &[date, c] : g.cells) {
                (void)c;
                expected.insert(date);
            }
            if (datesSeen != expected)
                invalid("기존 배분의 날짜 구성과 원본 검토가 다릅니다.");
            g.applied = true;
        }
        if (!g.applied)
            for (const auto *r : g.records)
                if (r->actualMinor) {
                    if (original.actualCurrency != p.currency || !r->taskActualMinor || !r->totalIssue.empty())
                        invalid("원가 배분에는 같은 통화의 명시 작업 누계 AC가 필요합니다: " + r->totalIssue);
                    if (sourceTotals.contains(g.target.first) && sourceTotals.at(g.target.first) != *r->taskActualMinor)
                        invalid("같은 작업의 원본 누계 AC가 다릅니다.");
                    sourceTotals[g.target.first] = *r->taskActualMinor;
                }
    }
    XmlActualAllocation result;
    for (auto &[uid, g] : groups) {
        for (const auto &r : g.ranges) {
            auto rr = r.toObject();
            rr["assignmentUid"] = qs(uid);
            rr["taskId"] = qs(g.target.first);
            rr["resourceId"] = qs(g.target.second);
            rr["alreadyApplied"] = g.applied;
            rangeReport.append(rr);
        }
        for (const auto &[date, c] : g.cells) {
            const PeriodActualKey key{g.target.first, g.target.second, date};
            if (owners.contains(key))
                invalid("여러 원본 배정이 같은 native 배정·날짜에 중복 연결됩니다.");
            owners[key] = uid;
            const auto *old = current.contains(key) ? current.at(key) : nullptr;
            const auto work = g.applied ? (old ? old->workUnitMinutes : std::nullopt)
                                        : (c.work ? c.work
                                           : old  ? old->workUnitMinutes
                                                  : std::nullopt);
            const auto cost = g.applied ? (old ? old->actualMinor : std::nullopt)
                                        : (c.cost ? c.cost
                                           : old  ? old->actualMinor
                                                  : std::nullopt);
            const auto head = g.marker + " date=" + date + " work=" + (c.work ? "estimated" : "preserved") +
                              " cost=" + (c.cost ? "estimated" : "preserved") + " parts=" + c.parts +
                              " src=" + original.actualCurrency + " ";
            if (g.applied) {
                bool proof = false;
                for (const auto &e : p.periodActuals)
                    if (e.statusAt == *p.statusAt && e.taskId == g.target.first && e.resourceId == g.target.second &&
                        e.date == date && e.reason.starts_with(head) && (!c.work || e.workUnitMinutes == c.work) &&
                        (!c.cost || p.currency != original.actualCurrency || e.actualMinor == c.cost))
                        proof = true;
                if (!proof)
                    invalid("기존 배분의 정수 수량/가중치 근거가 원본과 다릅니다.");
            }
            dayReport.append(QJsonObject{{"assignmentUid", qs(uid)},
                                         {"taskId", qs(g.target.first)},
                                         {"resourceId", qs(g.target.second)},
                                         {"date", qs(date)},
                                         {"allocatedWork", number(c.work)},
                                         {"allocatedCostSourceMinor", number(c.cost)},
                                         {"oldWork", number(old ? old->workUnitMinutes : std::nullopt)},
                                         {"oldCostNativeMinor", number(old ? old->actualMinor : std::nullopt)},
                                         {"adoptedWork", number(work)},
                                         {"adoptedCostNativeMinor", number(cost)},
                                         {"parts", qs(c.parts)},
                                         {"alreadyApplied", g.applied}});
            if (g.applied)
                continue;
            PeriodActualSubmission s;
            s.reporter = options.reporter;
            s.recordedAt = options.recordedAt;
            s.reason =
                head + input.report["source"].toObject()["name"].toString().toStdString() + " · " + options.reason;
            if (s.reason.size() > 4096)
                invalid("배분 출처/사유가 너무 깁니다. 구간 수나 사유 길이를 줄이세요.");
            PeriodActualEntry e;
            e.taskId = g.target.first;
            e.resourceId = g.target.second;
            e.date = date;
            e.workUnitMinutes = work;
            e.actualMinor = cost;
            s.entries.push_back(e);
            if (cost) {
                std::optional<std::int64_t> total;
                if (sourceTotals.contains(e.taskId))
                    total = sourceTotals.at(e.taskId);
                else if (const auto *ac = costAsOf(p, e.taskId, *p.statusAt))
                    total = ac->actualMinor;
                s.totals.push_back({e.taskId, total});
            }
            result.batches.push_back(std::move(s));
        }
    }
    if (result.batches.size() > 1000)
        invalid("한 번의 배분은1000개 native 날짜까지입니다.");
    auto trial = p;
    std::set<std::string> used;
    for (const auto &e : p.periodActuals)
        used.insert(e.id);
    for (const auto &e : p.costEntries)
        used.insert(e.id);
    int next = 0;
    const auto newId = [&] {
        std::string id;
        do {
            id = "xml-prorata-preview-" + sha + "-" + std::to_string(++next);
        } while (used.contains(id));
        used.insert(id);
        return id;
    };
    for (const auto &s : result.batches) {
        calculationCheckpoint();
        appendPeriodActuals(trial, s, newId);
    }
    if (!result.batches.empty())
        ++trial.revision;
    validate(trial);
    guardFinancialPeriodEdits(p, trial);
    QJsonArray balances;
    const auto before = actualCostAllocations(p, *p.statusAt), after = actualCostAllocations(trial, *p.statusAt);
    std::set<std::string> affected;
    for (const auto &[uid, g] : groups) {
        (void)uid;
        affected.insert(g.target.first);
    }
    for (const auto &id : affected) {
        const auto &a = after.at(id);
        balances.append(QJsonObject{{"taskId", qs(id)},
                                    {"cumulative", number(a.cumulative)},
                                    {"beforeAllocated", double(before.at(id).allocated)},
                                    {"afterAllocated", double(a.allocated)},
                                    {"unallocated", number(a.unallocated)}});
    }
    result.report = {{"contract", "xml-proportional-actuals-v1"},
                     {"stage", "preview-before-adoption"},
                     {"sourceSha256", qs(sha)},
                     {"nativeInputSha256", qs(hash(json::encodeProject(p)))},
                     {"documentId", qs(p.documentId)},
                     {"revision", QString::number(p.revision)},
                     {"statusAt", qs(*p.statusAt)},
                     {"reporter", qs(options.reporter)},
                     {"reason", qs(options.reason)},
                     {"recordedAt", qs(options.recordedAt)},
                     {"basis", qs(basis)},
                     {"rangeTotalsConfirmed", true},
                     {"classification", "proportional-estimate-not-measured"},
                     {"rounding", "largest-remainder-date-ascending"},
                     {"zeroWeightPolicy", "omit-not-reported-zero"},
                     {"sourceCurrency", qs(original.actualCurrency)},
                     {"nativeCurrency", qs(p.currency)},
                     {"ranges", rangeReport},
                     {"days", dayReport},
                     {"costReconciliation", balances},
                     {"submissions", manifest(result.batches)}};
    return result;
}
std::vector<PeriodActualSubmission> confirmXmlActualAllocation(const Project &p, const ProjectXmlExchange &source,
                                                               const XmlProgressOptions &o,
                                                               const std::vector<XmlPeriodMapping> &m,
                                                               const XmlAllocationPolicy &policy,
                                                               const XmlActualAllocation &preview) {
    auto replay = prepareXmlActualAllocation(p, source, o, m, policy);
    if (replay.report != preview.report || manifest(preview.batches) != manifest(replay.batches))
        invalid("배분 미리보기/원본/현재 문서가 재검증과 다릅니다. 다시 검토하세요.");
    return std::move(replay.batches);
}
QString publishXmlActualAllocation(const Project &p, const ProjectXmlExchange &source, const XmlProgressOptions &o,
                                   const std::vector<XmlPeriodMapping> &m, const XmlAllocationPolicy &policy,
                                   const XmlActualAllocation &preview, const QString &destination) {
    confirmXmlActualAllocation(p, source, o, m, policy, preview);
    calculationCheckpoint();
    const QFileInfo target(destination);
    const auto parent = target.dir().canonicalPath();
    if (parent.isEmpty() || target.fileName().isEmpty() || target.fileName() == "." || target.fileName() == ".." ||
        target.exists() || target.isSymLink())
        invalid("존재하는 상위 폴더 아래 새 배분 검토 폴더를 지정하세요.");
    QDir directory(parent);
    QTemporaryDir stage(directory.filePath(".ppm-prorata-XXXXXX"));
    if (!stage.isValid())
        invalid("배분 검토 임시 폴더를 만들 수 없습니다.");
    const auto write = [&](const QString &name, const QByteArray &bytes) {
        calculationCheckpoint();
        QSaveFile f(stage.filePath(name));
        f.setDirectWriteFallback(false);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
            invalid("검토 파일 저장에 실패했습니다.");
        QFile read(stage.filePath(name));
        if (!read.open(QIODevice::ReadOnly) || read.readAll() != bytes)
            invalid("저장한 검토 파일이 다릅니다.");
    };
    JsonProjectStore store;
    store.save(stage.filePath("source.ppm").toStdString(), p, {});
    if (store.load(stage.filePath("source.ppm").toStdString()).project != p)
        invalid("원본 native 사본이 다릅니다.");
    write("original.xml", source.xml);
    auto report = preview.report;
    QFile native(stage.filePath("source.ppm"));
    if (!native.open(QIODevice::ReadOnly))
        invalid("원본 사본 해시를 확인할 수 없습니다.");
    report["sourceNativeFileSha256"] = qs(hash(native.readAll()));
    native.close();
    write("allocation.json", QJsonDocument(report).toJson());
    calculationCheckpoint();
    const auto final = directory.filePath(target.fileName());
    if (QFileInfo::exists(final) || !directory.rename(QFileInfo(stage.path()).fileName(), target.fileName()))
        invalid("새 검토 폴더를 게시할 수 없습니다.");
    stage.setAutoRemove(false);
    return final;
}
} // namespace ppm
