#include "domain/LedgerIdentity.h"
#include "domain/CalculationControl.h"
#include "domain/ProjectCurrency.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void bad(const char *s) {
    throw AppError(ErrorCode::LEDGER_ID_INVALID, {s});
}
bool label(const std::string &s, std::size_t max = 1024) {
    return !s.empty() && s.size() <= max && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
struct Mapping {
    std::map<std::string, std::string> tasks, resources;
    std::string task(const std::string &s) const {
        auto i = tasks.find(s);
        return i == tasks.end() ? s : i->second;
    }
    std::string resource(const std::string &s) const {
        auto i = resources.find(s);
        return i == resources.end() ? s : i->second;
    }
    template <class T, class F> void unique(const std::vector<T> &v, F key) const {
        std::set<decltype(key(v.front()))> used;
        for (const auto &x : v)
            if (!used.insert(key(x)).second)
                bad("이관 후 ID/배정/예산 참조가 중복됩니다. 기존 계획의 충돌을 먼저 정리하세요.");
    }
    void remaining(std::vector<RemainingWorkPlan> &v) const {
        for (auto &x : v) {
            x.taskId = task(x.taskId);
            x.resourceId = resource(x.resourceId);
        }
    }
    void plan(PlanInput &p, bool current) const {
        std::set<std::string> taskIds, resourceIds, mergedTasks;
        for (const auto &t : p.tasks)
            taskIds.insert(t.id);
        for (const auto &r : p.resources)
            resourceIds.insert(r.id);
        if (!current) {
            for (const auto &[a, b] : tasks)
                if (taskIds.contains(b))
                    bad("기준선/시나리오가 대상 작업 ID를 이미 사용합니다. 동결된 작업의 관측 귀속을 합칠 수 "
                        "없습니다.");
            for (const auto &[a, b] : resources)
                if (resourceIds.contains(b))
                    bad("기준선/시나리오가 대상 자원 ID를 이미 사용합니다. 동결된 자원의 관측 귀속을 합칠 수 "
                        "없습니다.");
        } else {
            for (const auto &[a, b] : tasks)
                if (taskIds.contains(a) && taskIds.contains(b))
                    mergedTasks.insert(b);
            std::erase_if(p.tasks, [&](const auto &t) {
                auto i = tasks.find(t.id);
                return i != tasks.end() && taskIds.contains(i->second);
            });
            std::erase_if(p.resources, [&](const auto &r) {
                auto i = resources.find(r.id);
                return i != resources.end() && resourceIds.contains(i->second);
            });
        }
        for (auto &t : p.tasks) {
            calculationCheckpoint();
            t.id = task(t.id);
            for (auto &id : t.predecessors)
                id = task(id);
            for (auto &e : t.dependencyRules)
                e.predecessorId = task(e.predecessorId);
        }
        if (p.xmlWbs) {
            if (current)
                std::erase_if(p.xmlWbs->values, [&](const auto &v) {
                    auto i = tasks.find(v.id);
                    return v.kind == "task" && i != tasks.end() && taskIds.contains(i->second);
                });
            for (auto &v : p.xmlWbs->values)
                if (v.kind == "task")
                    v.id = task(v.id);
        }
        for (auto &r : p.resources)
            r.id = resource(r.id);
        for (auto &a : p.assignments) {
            a.taskId = task(a.taskId);
            a.resourceId = resource(a.resourceId);
        }
        if (p.xmlBaselines)
            for (auto &r : p.xmlBaselines->rows) {
                if (r.kind == "task" || r.kind == "assignment")
                    r.id = task(r.id);
                if (r.kind == "resource")
                    r.id = resource(r.id);
                if (r.kind == "assignment")
                    r.resourceId = resource(r.resourceId);
            }
        for (auto &c : p.taskCosts)
            c.taskId = task(c.taskId);
        for (auto &a : p.activities) {
            a.taskId = task(a.taskId);
            a.startTaskId = task(a.startTaskId);
            a.finishTaskId = task(a.finishTaskId);
        }
        for (auto &u : p.resourcePlan.uses)
            u.taskId = task(u.taskId);
        for (auto &e : p.resourcePlan.expenses)
            e.taskId = task(e.taskId);
        for (auto &d : p.resourcePlan.demands) {
            d.taskId = task(d.taskId);
            d.resourceId = resource(d.resourceId);
        }
        unique(p.tasks, [](const auto &t) { return t.id; });
        unique(p.resources, [](const auto &r) { return r.id; });
        unique(p.taskCosts, [](const auto &c) { return c.taskId; });
        unique(p.assignments, [](const auto &a) { return std::pair{a.taskId, a.resourceId}; });
        std::map<std::string, std::size_t> degrees;
        std::map<std::string, std::vector<std::string>> successors;
        std::map<std::string, const Task *> task;
        for (const auto &t : p.tasks) {
            degrees[t.id] = t.predecessors.size();
            task[t.id] = &t;
            for (const auto &pred : t.predecessors)
                successors[pred].push_back(t.id);
        }
        std::vector<std::string> ready;
        for (const auto &[id, n] : degrees)
            if (!n)
                ready.push_back(id);
        std::size_t count = 0;
        while (!ready.empty()) {
            calculationCheckpoint();
            auto id = std::move(ready.back());
            ready.pop_back();
            ++count;
            for (const auto &next : successors[id])
                if (--degrees.at(next) == 0)
                    ready.push_back(next);
        }
        if (count != p.tasks.size())
            bad("ID 이관으로 선행 순환 또는 누락 연결이 생깁니다. 현재 계획 연결을 먼저 정리하세요.");
        for (const auto &a : p.assignments)
            if (current && mergedTasks.contains(a.taskId) && task.contains(a.taskId)) {
                const auto &t = *task.at(a.taskId);
                if (t.taskType == TaskType::FixedDuration && t.durationMinutes &&
                    std::any_of(a.periods.begin(), a.periods.end(),
                                [&](const auto &v) { return v.toMinute > *t.durationMinutes; }))
                    bad("대상 작업의 기간이 기존 배정 구간보다 짧습니다. 계획 구간을 먼저 정리하세요.");
            }
    }
    void financial(XerFinancialSnapshot &s) const {
        for (auto &t : s.tasks)
            t.id = task(t.id);
        for (auto &r : s.resources)
            r.id = resource(r.id);
        for (auto &a : s.assignments) {
            a.taskId = task(a.taskId);
            a.resourceId = resource(a.resourceId);
        }
        for (auto &p : s.periods) {
            p.taskId = task(p.taskId);
            p.resourceId = resource(p.resourceId);
        }
        unique(s.tasks, [](const auto &t) { return t.id; });
        unique(s.resources, [](const auto &r) { return r.id; });
        unique(s.assignments, [](const auto &a) { return std::pair{a.taskId, a.resourceId}; });
    }
};
void currentMatches(const Project &p, const LedgerIdMigration &e) {
    if (!e.after)
        bad("이관 결과 스냅샷이 없습니다.");
    auto actual = p, expected = *e.after;
    clearTransitionHistories(actual);
    expected.documentId = actual.documentId;
    expected.derivedFrom = actual.derivedFrom;
    if (expected.planLeveling || expected.remainingLeveling) {
        if (actual.planLeveling || actual.remainingLeveling ||
            actual.levelingDecisions.size() != expected.levelingDecisions.size() + 1 ||
            !std::equal(expected.levelingDecisions.begin(), expected.levelingDecisions.end(),
                        actual.levelingDecisions.begin()) ||
            actual.levelingDecisions.back().action != "invalidate" ||
            actual.levelingDecisions.back().recordedRevision != e.recordedRevision ||
            actual.levelingDecisions.back().reason != "관측 원장 ID 이관")
            bad("ID 이관의 평준화 해제 이력이 일치하지 않습니다.");
        expected.planLeveling.reset();
        expected.remainingLeveling.reset();
        expected.levelingDecisions = actual.levelingDecisions;
    }
    if (actual != expected)
        bad("이관 직후 현재 문서와 검증한 이관 결과가 일치하지 않습니다.");
}
} // namespace
bool LedgerIdMigration::operator==(const LedgerIdMigration &b) const {
    return static_cast<const LedgerIdSubmission &>(*this) == static_cast<const LedgerIdSubmission &>(b) && id == b.id &&
           sourceDocumentId == b.sourceDocumentId && recordedRevision == b.recordedRevision &&
           bool(before) == bool(b.before) && bool(after) == bool(b.after) &&
           (!before || before == b.before || *before == *b.before) &&
           (!after || after == b.after || *after == *b.after);
}
bool hasLedgerIdentity(const Project &p, const std::string &kind, const std::string &id) {
    if (kind == "task")
        return std::any_of(p.materialActuals.begin(), p.materialActuals.end(),
                           [&](const auto &e) { return e.taskId == id; }) ||
               std::any_of(p.progress.begin(), p.progress.end(), [&](const auto &e) { return e.taskId == id; }) ||
               std::any_of(p.costEntries.begin(), p.costEntries.end(), [&](const auto &e) { return e.taskId == id; }) ||
               std::any_of(p.periodActuals.begin(), p.periodActuals.end(),
                           [&](const auto &e) { return e.taskId == id; }) ||
               std::any_of(p.stepReports.begin(), p.stepReports.end(), [&](const auto &e) { return e.taskId == id; });
    return kind == "resource" && std::any_of(p.periodActuals.begin(), p.periodActuals.end(),
                                             [&](const auto &e) { return e.resourceId == id; });
}
Project migrateLedgerIdentities(const Project &source, const std::vector<LedgerIdLink> &links) {
    if (links.empty() || links.size() > 20000)
        bad("이관할 작업/자원 ID를 1~20,000개 지정하세요.");
    Mapping m;
    std::map<std::string, const Resource *> resource;
    std::set<std::string> tasks, occupied, targets;
    std::set<std::pair<std::string, std::string>> observed;
    for (const auto &v : source.materialActuals)
        if (!v.taskId.empty())
            observed.emplace("task", v.taskId);
    for (const auto &v : source.progress)
        observed.emplace("task", v.taskId);
    for (const auto &v : source.costEntries)
        observed.emplace("task", v.taskId);
    for (const auto &v : source.stepReports)
        observed.emplace("task", v.taskId);
    for (const auto &v : source.periodActuals) {
        observed.emplace("task", v.taskId);
        if (!v.resourceId.empty())
            observed.emplace("resource", v.resourceId);
    }
    for (const auto &t : source.tasks) {
        tasks.insert(t.id);
        occupied.insert(t.id);
    }
    for (const auto &r : source.resources) {
        resource[r.id] = &r;
        occupied.insert(r.id);
    }
    for (const auto &w : source.wbsNodes)
        occupied.insert(w.id);
    for (const auto &c : source.calendars)
        occupied.insert(c.id);
    std::set<std::pair<std::string, std::string>> origins;
    for (const auto &l : links)
        origins.emplace(l.kind, l.sourceId);
    for (const auto &l : links) {
        calculationCheckpoint();
        if ((l.kind != "task" && l.kind != "resource") || !label(l.sourceId) || !label(l.targetId) ||
            l.sourceId == l.targetId || origins.contains({l.kind, l.targetId}) || !targets.insert(l.targetId).second ||
            !observed.contains({l.kind, l.sourceId}))
            bad("원장 기록이 있는 원래 ID와 서로 다른 대상을 일대일로 지정하세요. 순환/연쇄·중복 대상은 허용하지 "
                "않습니다.");
        const bool exists = l.kind == "task" ? tasks.contains(l.targetId) : resource.contains(l.targetId);
        if ((l.kind == "task" ? !tasks.contains(l.sourceId) : !resource.contains(l.sourceId)) ||
            (!exists && occupied.contains(l.targetId)))
            bad("원래 작업/자원이 없거나 새 ID가 다른 객체와 충돌합니다.");
        if (exists && observed.contains({l.kind, l.targetId}))
            bad("대상에 이미 관측 원장이 있습니다. 두 이력을 합산하거나 덮어쓸 수 없습니다.");
        if (l.kind == "resource" && exists && resource.at(l.sourceId)->type != resource.at(l.targetId)->type)
            bad("원장의 인력/장비 단위는 바꾸지 않습니다. 같은 종류의 자원을 지정하세요.");
        if (l.kind == "resource" && exists &&
            std::any_of(source.periodActuals.begin(), source.periodActuals.end(), [&](const auto &e) {
                return e.resourceId == l.sourceId && e.resourceType != resource.at(l.targetId)->type;
            }))
            bad("원장에 기록된 자원 종류와 대상 자원의 종류가 다릅니다. 과거 단위를 변환할 수 없습니다.");
        auto &map = l.kind == "task" ? m.tasks : m.resources;
        if (!map.emplace(l.sourceId, l.targetId).second)
            bad("원래 ID가 중복되었습니다.");
    }
    auto p = source;
    clearTransitionHistories(p);
    m.plan(p, true);
    for (auto &e : p.progress)
        e.taskId = m.task(e.taskId);
    for (auto &e : p.costEntries)
        e.taskId = m.task(e.taskId);
    for (auto &e : p.materialActuals)
        if (!e.taskId.empty())
            e.taskId = m.task(e.taskId);
    for (auto &e : p.periodActuals) {
        e.taskId = m.task(e.taskId);
        e.resourceId = m.resource(e.resourceId);
    }
    for (auto &e : p.stepReports)
        e.taskId = m.task(e.taskId);
    m.remaining(p.remainingWork);
    for (auto &b : p.baselines) {
        m.plan(b.input, false);
        for (auto &row : b.schedule) {
            row.taskId = m.task(row.taskId);
            for (auto &id : row.drivingPredecessors)
                id = m.task(id);
        }
    }
    for (auto &s : p.scenarios) {
        m.plan(s.input, false);
        for (auto &e : s.progress)
            e.taskId = m.task(e.taskId);
        for (auto &e : s.materialActuals)
            if (!e.taskId.empty())
                e.taskId = m.task(e.taskId);
        m.remaining(s.remainingWork);
    }
    for (auto &e : p.financialPeriodEvents) {
        for (auto &v : e.period.values)
            v.taskId = m.task(v.taskId);
        for (auto &t : e.tasks)
            t.taskId = m.task(t.taskId);
    }
    for (auto &e : p.externalEvmAdoptions)
        for (auto &id : e.taskIds)
            id = m.task(id);
    if (p.xerFinancials)
        m.financial(*p.xerFinancials);
    for (auto &r : p.xerFinancialRevisions) {
        m.financial(r.financials);
        if (r.merge)
            for (auto &a : r.merge->assignmentOverrides) {
                a.taskId = m.task(a.taskId);
                a.resourceId = m.resource(a.resourceId);
            }
        if (r.relink) {
            for (auto &l : r.relink->incomingLinks)
                l.targetId = l.kind == "task" ? m.task(l.targetId) : m.resource(l.targetId);
            for (auto &l : r.relink->previousLinks) {
                l.sourceId = l.kind == "task" ? m.task(l.sourceId) : m.resource(l.sourceId);
                l.targetId = l.kind == "task" ? m.task(l.targetId) : m.resource(l.targetId);
            }
        }
    }
    std::set<std::string> positioned;
    for (const auto &v : p.networkPositions)
        positioned.insert(v.taskId);
    std::erase_if(p.networkPositions, [&](const auto &v) {
        auto i = m.tasks.find(v.taskId);
        return i != m.tasks.end() && positioned.contains(i->second);
    });
    for (auto &v : p.networkPositions)
        v.taskId = m.task(v.taskId);
    ++p.revision;
    validate(p);
    return p;
}
std::size_t ledgerIdentityRows(const Project &p) {
    auto plan = [](const PlanInput &v) {
        return v.tasks.size() + v.resources.size() + v.assignments.size() + v.wbsNodes.size() + v.calendars.size();
    };
    std::size_t n = plan(p) + p.progress.size() + p.costEntries.size() + p.periodActuals.size() + p.stepReports.size();
    for (const auto &b : p.baselines)
        n += plan(b.input) + b.schedule.size();
    for (const auto &s : p.scenarios)
        n += plan(s.input) + s.progress.size();
    auto financial = [](const XerFinancialSnapshot &s) {
        return s.tasks.size() + s.resources.size() + s.assignments.size() + s.periods.size();
    };
    if (p.xerFinancials)
        n += financial(*p.xerFinancials);
    for (const auto &r : p.xerFinancialRevisions) {
        n += financial(r.financials);
        if (r.relink)
            n += financial(r.relink->incoming);
    }
    for (const auto &e : p.xerScheduleAdoptions)
        n += plan(e.before) + plan(e.after) + plan(e.source.input);
    for (const auto &e : p.xmlBaselineMerges)
        n += e.incoming.rows.size() + e.after.rows.size() + (e.before ? e.before->rows.size() : 0) + e.links.size();
    for (const auto &e : p.financialPeriodEvents)
        n += e.tasks.size() + e.period.values.size();
    return n;
}
void validateLedgerIdMigrations(const Project &p) {
    if (p.ledgerIdMigrations.size() > 16)
        bad("ID 이관 이력은 최대16건입니다.");
    std::uint64_t revision = 0;
    std::size_t rows = 0;
    std::set<std::string> ids;
    const LedgerIdMigration *previous = nullptr;
    for (const auto &e : p.ledgerIdMigrations) {
        calculationCheckpoint();
        if (!e.before || !e.after || !e.before->ledgerIdMigrations.empty() || !e.after->ledgerIdMigrations.empty() ||
            !e.before->currencyConversions.empty() || !e.after->currencyConversions.empty() || !label(e.id) ||
            !ids.insert(e.id).second || !label(e.reporter) || !label(e.reason, 4096) || !e.confirmed ||
            !validUtcStamp(e.recordedAt) || e.sourceDocumentId != e.before->documentId ||
            e.recordedRevision != e.before->revision + 1 || e.recordedRevision <= revision ||
            e.recordedRevision > p.revision)
            bad("ID 이관 출처·확인·시각·revision·불변 스냅샷을 확인하세요.");
        rows += ledgerIdentityRows(*e.before) + ledgerIdentityRows(*e.after);
        if (rows > 300000)
            bad("ID 이관 전후 스냅샷 한도는 합계300,000행입니다.");
        validate(*e.before);
        if (migrateLedgerIdentities(*e.before, e.links) != *e.after)
            bad("원래 원장·ID 연결과 저장된 이관 결과가 일치하지 않습니다.");
        if (previous && e.before->revision == previous->recordedRevision)
            currentMatches(*e.before, *previous);
        previous = &e;
        revision = e.recordedRevision;
    }
}
void validateLedgerIdCurrent(const Project &p) {
    if (!p.ledgerIdMigrations.empty() && p.ledgerIdMigrations.back().recordedRevision == p.revision)
        currentMatches(p, p.ledgerIdMigrations.back());
}
} // namespace jobs
