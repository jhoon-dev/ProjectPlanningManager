#include "domain/FinancialPeriods.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FINANCIAL_PERIOD_INVALID, {s});
}
bool label(const std::string &s, std::size_t max = 1024) {
    return !s.empty() && s.size() <= max && s.find_first_not_of(' ') != std::string::npos &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void amount(std::optional<std::int64_t> n, bool signedValue = false) {
    if (n && (*n > maxMoney || *n < (signedValue ? -maxMoney : 0)))
        invalid("금액 한도를 확인하세요.");
}
using Locks = std::map<std::string, std::string>;
Locks locks(const Project &p, const std::map<std::string, const FinancialPeriodEvent *> &state) {
    Locks out;
    for (const auto &[id, e] : state) {
        (void)id;
        if (e->action != "close")
            continue;
        auto b = std::find_if(p.baselines.begin(), p.baselines.end(),
                              [&](const auto &v) { return v.id == e->period.baselineId; });
        if (b == p.baselines.end())
            invalid("기준선이 없습니다.");
        for (const auto &t : b->input.tasks)
            out[t.id] = std::max(out[t.id], e->period.toDate);
    }
    return out;
}
void locked(const Locks &l, const std::string &task, const std::string &date) {
    auto i = l.find(task);
    if (i != l.end() && date.substr(0, 10) <= i->second)
        invalid("마감일 이전 실적/원가입니다. 관련 기간을 최신 마감부터 재개방하세요.");
}
struct Fact {
    std::uint64_t revision;
    const std::string *task, *date;
};
using Facts = std::vector<Fact>;
template <class T> void collect(Facts &out, const std::vector<T> &entries) {
    for (const auto &e : entries) {
        calculationCheckpoint();
        out.push_back({e.recordedRevision, &e.taskId, &e.statusAt});
        if constexpr (requires { e.date; })
            out.push_back({e.recordedRevision, &e.taskId, &e.date});
    }
}
Facts facts(const Project &p) {
    Facts out;
    collect(out, p.costEntries);
    collect(out, p.progress);
    collect(out, p.periodActuals);
    collect(out, p.stepReports);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.revision < b.revision; });
    return out;
}
void inspectFacts(const Facts &facts, const Locks &l, std::uint64_t from, std::uint64_t to) {
    if (l.empty())
        return;
    auto it = std::upper_bound(facts.begin(), facts.end(), from,
                               [](auto revision, const auto &f) { return revision < f.revision; });
    for (; it != facts.end() && it->revision < to; ++it) {
        calculationCheckpoint();
        locked(l, *it->task, *it->date);
    }
}
} // namespace
std::map<std::string, const FinancialPeriodEvent *> financialPeriods(const Project &p) {
    std::map<std::string, const FinancialPeriodEvent *> out;
    for (const auto &e : p.financialPeriodEvents)
        out[e.period.id] = &e;
    return out;
}
const FinancialPeriodEvent &financialPeriod(const Project &p, const std::string &id) {
    auto state = financialPeriods(p);
    auto it = state.find(id);
    if (it == state.end())
        invalid("선택한 사용자 기간이 없습니다.");
    return *it->second;
}
void guardFinancialPeriodEdits(const Project &before, const Project &after) {
    const auto active = locks(before, financialPeriods(before));
    if (active.empty())
        return;
    inspectFacts(facts(after), active, before.revision, after.revision + 1);
}
void validateFinancialPeriods(const Project &p) {
    if (p.financialPeriodEvents.empty())
        return;
    if (p.financialPeriodEvents.size() > 512)
        invalid("기간 변경/마감 이력 한도는512건입니다.");
    const auto timeline = facts(p);
    std::map<std::string, const FinancialPeriodEvent *> state;
    std::set<std::string> ids;
    std::uint64_t previous = 0;
    std::size_t references = 0;
    for (const auto &e : p.financialPeriodEvents) {
        calculationCheckpoint();
        inspectFacts(timeline, locks(p, state), previous, e.recordedRevision);
        if (!label(e.id) || !ids.insert(e.id).second || !label(e.period.id) || !label(e.period.name) ||
            !label(e.sourceDocumentId) || !label(e.reporter) || !label(e.reason, 4096) || !e.confirmed ||
            !validUtcStamp(e.recordedAt) || e.recordedRevision <= previous || e.recordedRevision > p.revision ||
            !e.knowledgeRevision || e.knowledgeRevision != e.recordedRevision - 1 || e.currency != p.currency ||
            (e.statusAt && !validStamp(*e.statusAt)) ||
            (e.formula != "budget-remaining-v1" && e.formula != "cpi-v1" && e.formula != "cpi-spi-v1"))
            invalid("메타데이터/확인/revision/추정식을 확인하세요.");
        const auto sameRevision =
            std::lower_bound(timeline.begin(), timeline.end(), e.recordedRevision,
                             [](const auto &fact, auto revision) { return fact.revision < revision; });
        if (sameRevision != timeline.end() && sameRevision->revision == e.recordedRevision)
            invalid("기간 전이와 관측 원장은 별도 revision으로 기록해야 합니다.");
        previous = e.recordedRevision;
        const auto &d = e.period;
        if (!validDate(d.fromDate) || !validDate(d.toDate) || d.fromDate > d.toDate)
            invalid("기간 시작일/종료일을 확인하세요.");
        auto b =
            std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &v) { return v.id == d.baselineId; });
        if (b == p.baselines.end() || b->input.currency != e.currency || b->inputRevision >= e.recordedRevision)
            invalid("승인 기준선/통화를 확인하세요.");
        std::set<std::string> tasks;
        for (const auto &t : b->input.tasks)
            tasks.insert(t.id);
        std::set<std::string> values;
        for (const auto &v : d.values) {
            if (!tasks.contains(v.taskId) || !values.insert(v.taskId).second || (!v.budget && !v.earned))
                invalid("작업별 기간 금액을 확인하세요.");
            amount(v.budget);
            amount(v.earned, true);
            if (e.action == "define" && v.earned && (!e.statusAt || *e.statusAt < d.toDate + " 23:59"))
                invalid("기간 인정 EV는 종료일 이후 상태일에서 입력하세요.");
        }
        const auto old = state.find(d.id);
        if (e.action == "define") {
            if (old != state.end() && old->second->action == "close")
                invalid("마감 기간은 먼저 재개방하세요.");
            for (const auto &[key, v] : state)
                if (key != d.id && v->period.baselineId == d.baselineId && d.fromDate <= v->period.toDate &&
                    v->period.fromDate <= d.toDate)
                    invalid("같은 기준선의 사용자 기간이 겹칩니다.");
        } else if (e.action == "close" || e.action == "reopen") {
            if (old == state.end() || old->second->period != d ||
                ((e.action == "reopen") != (old->second->action == "close")))
                invalid("기간 상태/정의를 확인하세요.");
            if (e.action == "reopen")
                for (const auto &[key, v] : state)
                    if (key != d.id && v->action == "close" && v->period.baselineId == d.baselineId &&
                        v->period.toDate > d.toDate)
                        invalid("뒤 기간의 마감을 먼저 재개방하세요.");
        } else
            invalid("지원하지 않는 기간 전이입니다.");
        if (e.action != "close" && !e.tasks.empty())
            invalid("열린 기간에는 마감 스냅샷을 저장하지 않습니다.");
        if (e.action == "close") {
            if (!e.statusAt || *e.statusAt < d.toDate + " 23:59" || e.tasks.size() != tasks.size())
                invalid("기간 종료까지의 상태일/전체 작업 마감이 필요합니다.");
            std::set<std::string> frozen;
            for (const auto &t : e.tasks) {
                if (!tasks.contains(t.taskId) || !frozen.insert(t.taskId).second)
                    invalid("마감 작업 코호트가 다릅니다.");
                for (const auto *v : {&t.opening, &t.closing}) {
                    amount(v->bac);
                    amount(v->pv);
                    amount(v->ev);
                    amount(v->ac);
                    if (v->acReportId.size() > 4096 || v->evReportId.size() > 4096)
                        invalid("근거 ID 한도 초과");
                }
                if (t.opening.bac != t.closing.bac)
                    invalid("마감 BAC가 일치하지 않습니다.");
                amount(t.dated, true);
            }
        }
        references += d.values.size() + e.tasks.size();
        if (references > 100000)
            invalid("기간 작업 참조 한도는100,000개입니다.");
        state[d.id] = &e;
        if (state.size() > 512)
            invalid("사용자 기간 한도는512개입니다.");
    }
    inspectFacts(timeline, locks(p, state), previous, p.revision + 1);
}
} // namespace jobs
