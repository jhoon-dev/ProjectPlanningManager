#include "domain/ExternalEvm.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::EXTERNAL_EVM_INVALID, {message});
}
bool label(const std::string &s, std::size_t limit = 4096) {
    return !s.empty() && s.size() <= limit && s.find_first_not_of(' ') != std::string::npos &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::optional<std::string> shifted(const std::string &s, int n) {
    using namespace std::chrono;
    auto at = sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                       unsigned(std::stoi(s.substr(8, 2)))} +
              hours{std::stoi(s.substr(11, 2))} + minutes{std::stoi(s.substr(14, 2)) + n};
    auto d = floor<days>(at);
    year_month_day ymd{d};
    if (int(ymd.year()) < 1 || int(ymd.year()) > 9999)
        return {};
    auto minute = duration_cast<minutes>(at - d).count();
    char result[32];
    std::snprintf(result, sizeof(result), "%04d-%02u-%02u %02d:%02d", int(ymd.year()), unsigned(ymd.month()),
                  unsigned(ymd.day()), int(minute / 60), int(minute % 60));
    return result;
}
using PointKey = std::pair<std::string, bool>;
using PointValues = std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>;
using Points = std::map<PointKey, PointValues>;
void put(Points &out, const PointKey &at, PointValues values) {
    auto [it, inserted] = out.emplace(at, values);
    if (!inserted) {
        auto merge = [](auto &a, auto b) {
            if (a && b && a != b)
                invalid("연속 기간의 마감과 다음 개시 누계가 다릅니다.");
            if (!a)
                a = b;
        };
        merge(it->second.first, values.first);
        merge(it->second.second, values.second);
    }
}
std::optional<std::int64_t> closing(std::optional<std::int64_t> opening, std::optional<std::int64_t> amount) {
    if (!opening || !amount)
        return {};
    if (*opening > maxMoney - *amount)
        invalid("개시 누계와 기간 금액의 합계가 지원 한도를 넘습니다.");
    return *opening + *amount;
}
void addPoints(Points &points, const XerFinancialSnapshot &source, const ExternalEvmRow &r, bool ev, bool pv) {
    const auto &period = source.periods.at(r.periodIndex);
    const auto [begin, end] = externalEvmBounds(source, period);
    PointValues start{ev ? r.openingEv : std::nullopt, pv ? r.openingPv : std::nullopt};
    PointValues finish{ev ? closing(r.openingEv, period.bcwp) : std::nullopt,
                       pv ? closing(r.openingPv, period.bcws) : std::nullopt};
    put(points, {begin, false}, start);
    if (auto at = shifted(begin, -1))
        put(points, {*at, true}, start);
    put(points, {end, true}, finish);
    if (auto at = shifted(end, 1))
        put(points, {*at, false}, finish);
}
} // namespace
std::pair<std::string, std::string> externalEvmBounds(const XerFinancialSnapshot &s, const XerFinancialPeriod &r) {
    return {r.start.substr(0, 10) + " 00:00", std::min(r.finish.substr(0, 10) + " 23:59", s.statusAt)};
}
const XerFinancialSnapshot *externalEvmSource(const Project &p, const std::string &id) {
    if (id.empty())
        return p.xerFinancials ? &*p.xerFinancials : nullptr;
    for (const auto &r : p.xerFinancialRevisions)
        if (r.id == id)
            return &r.financials;
    return nullptr;
}
std::string latestExternalEvmSourceId(const Project &p) {
    return p.xerFinancialRevisions.empty() ? std::string{} : p.xerFinancialRevisions.back().id;
}
ExternalEvmSubmission externalEvmDraft(const Project &p, const std::string &baselineId) {
    ExternalEvmSubmission out;
    out.baselineId = baselineId;
    out.financialRevisionId = latestExternalEvmSourceId(p);
    if (const auto *s = externalEvmSource(p, out.financialRevisionId)) {
        out.sourceSha256 = s->sourceSha256;
        out.sourceProjectionSha256 = s->projectionSha256;
    }
    return out;
}
void validateExternalEvmAdoptions(const Project &p) {
    if (p.externalEvmAdoptions.size() > 128)
        invalid("채택/철회 이력은 최대128건입니다.");
    std::set<std::string> ids, current;
    for (const auto &t : p.tasks)
        current.insert(t.id);
    std::map<std::pair<std::string, std::string>, const ExternalEvmAdoption *> active;
    std::size_t rowCount = 0;
    std::uint64_t previousRevision = 0;
    for (const auto &a : p.externalEvmAdoptions) {
        calculationCheckpoint();
        if (!label(a.id, 1024) || !ids.insert(a.id).second || !label(a.reporter, 1024) || !label(a.reason) ||
            !label(a.sourceDocumentId, 1024) || !validUtcStamp(a.recordedAt) || !a.confirmed ||
            a.recordedRevision <= previousRevision || a.recordedRevision > p.revision || a.taskIds.empty() ||
            a.taskIds.size() > 10000 ||
            (a.withdrawn ? (a.useEv || a.usePv || !a.rows.empty()) : (!a.useEv && !a.usePv)))
            invalid("채택 메타데이터·확인·revision·대상 범위를 확인하세요.");
        previousRevision = a.recordedRevision;
        rowCount += a.rows.size() + a.taskIds.size();
        if (rowCount > 100000)
            invalid("채택 작업/기간 참조의 전체 한도는100,000개입니다.");
        const auto *s = externalEvmSource(p, a.financialRevisionId);
        std::string expected;
        for (const auto &revision : p.xerFinancialRevisions)
            if (revision.recordedRevision < a.recordedRevision)
                expected = revision.id;
        if (!s || a.financialRevisionId != expected || a.sourceSha256 != s->sourceSha256 ||
            a.sourceProjectionSha256 != s->projectionSha256 || a.sourceStatusAt != s->statusAt ||
            a.currency != s->currency || a.currency != p.currency)
            invalid("채택 당시 최신 XER 집계와 파일·투영 지문/상태일·통화가 일치하지 않습니다.");
        if ((a.nativeStatusAt && !validStamp(*a.nativeStatusAt)) ||
            (!a.withdrawn && (!a.nativeStatusAt || *a.nativeStatusAt < a.sourceStatusAt)))
            invalid("채택 당시 native 상태일과 원본 상태일을 확인하세요.");
        const auto baseline =
            std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return b.id == a.baselineId; });
        if (baseline == p.baselines.end() || baseline->inputRevision >= a.recordedRevision ||
            baseline->input.currency != a.currency)
            invalid("선택 기준선과 채택 통화/기록 시점을 확인하세요.");
        std::set<std::string> baselineTasks, selected;
        for (const auto &t : baseline->input.tasks)
            baselineTasks.insert(t.id);
        for (const auto &id : a.taskIds) {
            if (!current.contains(id) || !baselineTasks.contains(id) || !selected.insert(id).second)
                invalid("현재 문서와 기준선에 모두 존재하는 고유 작업 ID가 필요합니다.");
            const auto key = std::pair{a.baselineId, id};
            if (a.withdrawn && (!active.contains(key) || active.at(key)->withdrawn))
                invalid("채택 상태인 작업만 철회할 수 있습니다.");
        }
        std::set<std::size_t> used;
        std::set<std::string> seen;
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> ranges;
        std::map<std::string, Points> points;
        for (const auto &r : a.rows) {
            calculationCheckpoint();
            if (r.periodIndex >= s->periods.size() || !used.insert(r.periodIndex).second)
                invalid("재무기간 참조가 누락되거나 중복되었습니다.");
            const auto &period = s->periods[r.periodIndex];
            if (!period.resourceId.empty() || !selected.contains(period.taskId) ||
                !((a.useEv && period.bcwp) || (a.usePv && period.bcws)))
                invalid("선택 작업의 TASKFIN EV/PV 관측 기간을 선택하세요. 명시0은 관측값입니다.");
            const auto check = [](auto opening, bool use, auto amount) {
                if ((opening && (*opening < 0 || *opening > maxMoney)) || (!use && opening) ||
                    (use && amount && !opening))
                    invalid("채택할 관측값의 개시 누계를 명시하세요. 미보고와0은 다릅니다.");
            };
            check(r.openingEv, a.useEv, period.bcwp);
            check(r.openingPv, a.usePv, period.bcws);
            const auto range = externalEvmBounds(*s, period);
            if (range.first > range.second)
                invalid("원본 상태일 이전에 닫힌 기간이 필요합니다.");
            ranges[period.taskId].push_back(range);
            seen.insert(period.taskId);
            addPoints(points[period.taskId], *s, r, a.useEv, a.usePv);
        }
        if (!a.withdrawn && seen != selected)
            invalid("모든 선택 작업에 EV/PV 관측 기간이 필요합니다.");
        for (auto &[id, list] : ranges) {
            (void)id;
            std::sort(list.begin(), list.end());
            for (std::size_t i = 1; i < list.size(); ++i)
                if (list[i].first <= list[i - 1].second)
                    invalid("같은 작업의 채택 기간이 겹칩니다.");
        }
        for (const auto &id : a.taskIds)
            active[{a.baselineId, id}] = &a;
    }
}
ExternalEvmAdoption prepareExternalEvmAdoption(const Project &p, ExternalEvmSubmission s, std::string id) {
    if (!p.statusAt && !s.withdrawn)
        invalid("상태일을 설정한 뒤 채택/철회하세요.");
    if (s.financialRevisionId != latestExternalEvmSourceId(p))
        invalid("XER 집계가 변경되었습니다. 최신 집계로 다시 검토하세요.");
    const auto *source = externalEvmSource(p, s.financialRevisionId);
    if (!source || (!s.withdrawn && *p.statusAt < source->statusAt))
        invalid("현재 상태일은 XER 관측 상태일 이상이어야 합니다.");
    ExternalEvmAdoption a;
    static_cast<ExternalEvmSubmission &>(a) = std::move(s);
    a.id = std::move(id);
    a.sourceDocumentId = p.documentId;
    a.recordedRevision = p.revision + 1;
    a.currency = source->currency;
    a.sourceStatusAt = source->statusAt;
    a.nativeStatusAt = p.statusAt;
    auto candidate = p;
    ++candidate.revision;
    candidate.externalEvmAdoptions.push_back(a);
    validateExternalEvmAdoptions(candidate);
    return a;
}
ExternalEvmIndex::ExternalEvmIndex(const Project &p, const std::string &baselineId) {
    for (const auto &a : p.externalEvmAdoptions)
        if (a.baselineId == baselineId)
            for (const auto &id : a.taskIds) {
                calculationCheckpoint();
                tasks_[id] = Series{&a, {}};
            }
    for (const auto &a : p.externalEvmAdoptions) {
        if (a.baselineId != baselineId)
            continue;
        if (a.withdrawn)
            continue;
        const auto *source = externalEvmSource(p, a.financialRevisionId);
        for (const auto &r : a.rows)
            if (tasks_.at(source->periods[r.periodIndex].taskId).adoption == &a) {
                calculationCheckpoint();
                addPoints(tasks_.at(source->periods[r.periodIndex].taskId).points, *source, r, a.useEv, a.usePv);
            }
    }
}
ExternalEvmPoint ExternalEvmIndex::at(const std::string &id, const std::string &stamp, bool inclusive) const {
    if (!tasks_.contains(id) || tasks_.at(id).adoption->withdrawn)
        return {};
    const auto &series = tasks_.at(id);
    ExternalEvmPoint out{series.adoption->useEv, series.adoption->usePv, {}, {}, series.adoption->id};
    auto point = series.points.find({stamp, inclusive});
    if (point != series.points.end()) {
        out.ev = point->second.first;
        out.pv = point->second.second;
    }
    return out;
}
} // namespace ppm
