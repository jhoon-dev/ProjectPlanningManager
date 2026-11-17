#include "calculation/BaselineFormulaValues.h"
#include "calculation/BaselineCostFormulaValues.h"
#include "calculation/Resources.h"
#include "calculation/WorkCalendar.h"
#include "domain/Classification.h"
#include "domain/XmlBaselines.h"
#include <set>
#include <algorithm>
namespace jobs {
namespace {
void baselineValues(const Project &p, const Baseline *selected, std::uint64_t version,
                    const std::set<std::string> &references, XmlScheduleValues &out) {
    if (!selected) {
        out.baselineMissing = true;
        out.baselineIssue = "대응하는 native 기준선 v" + std::to_string(version) + "이 없습니다.";
        return;
    }
    try {
        const auto &b = *selected;
        if (b.version != version || b.id.empty() || !b.inputRevision || b.inputRevision >= p.revision ||
            b.sourceDocumentId.empty() || !supportsEngine(b.input, b.engineVersion))
            throw AppError(ErrorCode::FORMULA_INVALID, {"최초 기준선의 버전·출처·엔진을 확인하세요."});
        out.baselineCaption += " · " + b.id + " · " + b.name + " · 원본 rev " + std::to_string(b.inputRevision);
        const bool duration = references.contains("builtin:baselineduration");
        const bool work = references.contains("builtin:baselinework");
        if (duration) {
            auto calendar = std::make_shared<PlanInput>();
            calendar->holidays = b.input.holidays;
            calendar->calendars = b.input.calendars;
            calendar->xmlFormulaCalendarId = b.input.xmlFormulaCalendarId;
            out.baselineDurationCalendar = std::move(calendar);
        }
        // Only the frozen input and stored plan dates contribute. Current calendars, rates, progress and
        // transient comparison selections must not change baseline formula values.
        std::map<std::string, const Task *> tasks;
        for (const auto &t : b.input.tasks)
            if (!tasks.emplace(t.id, &t).second)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 작업 ID가 중복됩니다."});
        for (const auto &row : b.schedule) {
            calculationCheckpoint();
            if (!tasks.contains(row.taskId) || out.baselineTasks.contains(row.taskId) || !validStamp(row.start) ||
                !validStamp(row.finish) || row.start > row.finish || row.completedActual || row.remainingStart)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 일정의 작업·날짜를 확인하세요."});
            const auto start = normalizeFieldDateTime(row.start), finish = normalizeFieldDateTime(row.finish);
            out.baselineTasks.emplace(row.taskId, std::pair{start, finish});
            if (!out.baselineStart || start < *out.baselineStart)
                out.baselineStart = start;
            if (!out.baselineFinish || finish > *out.baselineFinish)
                out.baselineFinish = finish;
            if (duration) {
                WorkCalendar calendar(b.input, *tasks.at(row.taskId));
                const auto minutes = calendar.offset(row.finish) - calendar.offset(row.start);
                if (minutes < 0 || minutes > 5000000)
                    throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 기간 범위를 초과했습니다."});
                out.baselineDurations[row.taskId] = minutes;
            }
        }
        if (tasks.empty() || out.baselineTasks.size() != tasks.size() || !validStamp(b.projectFinish) ||
            out.baselineFinish != normalizeFieldDateTime(b.projectFinish))
            throw AppError(ErrorCode::FORMULA_INVALID, {"전체 작업과 완료일이 일치하는 기준선이 필요합니다."});
        if (references.contains("builtin:baselinecost"))
            addBaselineCostFormulaValues(p, b, out);
        if (!work)
            return;
        Project frozen;
        static_cast<PlanInput &>(frozen) = b.input;
        frozen.documentId = b.sourceDocumentId;
        frozen.revision = b.inputRevision;
        ScheduleResult result;
        result.documentId = frozen.documentId;
        result.inputRevision = frozen.revision;
        result.engineVersion = b.engineVersion;
        result.profile = frozen.calculationProfile;
        result.status = CalculationStatus::Valid;
        result.tasks = b.schedule;
        result.projectFinish = b.projectFinish;
        for (const auto &t : frozen.tasks)
            out.baselineTaskWork[t.id] = 0;
        for (const auto &r : frozen.resources)
            out.baselineResourceWork[r.id] = 0;
        for (const auto &a : frozen.assignments)
            out.baselineAssignmentWork[{a.taskId, a.resourceId}] = 0;
        out.baselineWork = 0;
        auto add = [](std::int64_t &sum, std::int64_t value) {
            if (value < 0 || value > maxWork || sum > maxWork - value)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 공수 합계 범위를 초과했습니다."});
            sum += value;
        };
        const auto civil = [](const std::string &stamp) {
            using namespace std::chrono;
            const auto date = year{std::stoi(stamp.substr(0, 4))} / month{unsigned(std::stoi(stamp.substr(5, 2)))} /
                              day{unsigned(std::stoi(stamp.substr(8, 2)))};
            return sys_days{date}.time_since_epoch().count() * 1440 + std::stoi(stamp.substr(11, 2)) * 60 +
                   std::stoi(stamp.substr(14, 2));
        };
        for (const auto &span : resourceOccupancies(frozen, result)) {
            calculationCheckpoint();
            const auto key = std::pair{span.taskId, span.resourceId};
            if (!out.baselineAssignmentWork.contains(key) || span.units < 0 || !validStamp(span.start) ||
                !validStamp(span.finish) || span.start > span.finish)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 배정 구간을 확인하세요."});
            const auto length = civil(span.finish) - civil(span.start);
            if (span.units && length > maxWork / span.units)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 공수 구간 범위를 초과했습니다."});
            const auto amount = length * span.units;
            if (amount > 0) {
                const auto start = normalizeFieldDateTime(span.start), finish = normalizeFieldDateTime(span.finish);
                auto [it, inserted] = out.baselineAssignmentDates.emplace(key, std::pair{start, finish});
                if (!inserted) {
                    it->second.first = std::min(it->second.first, start);
                    it->second.second = std::max(it->second.second, finish);
                }
            }
            add(*out.baselineWork, amount);
            add(out.baselineTaskWork.at(span.taskId), amount);
            add(out.baselineResourceWork.at(span.resourceId), amount);
            add(out.baselineAssignmentWork.at(key), amount);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.baselineTasks.clear();
        out.baselineStart.reset();
        out.baselineFinish.reset();
        out.baselineDurations.clear();
        out.baselineTaskWork.clear();
        out.baselineResourceWork.clear();
        out.baselineAssignmentWork.clear();
        out.baselineAssignmentDates.clear();
        out.baselineWork.reset();
        out.baselineIssue = e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e); error && !error->diagnostic().arguments.empty())
            out.baselineIssue = error->diagnostic().arguments.front();
    }
}
} // namespace
namespace {
bool externalValues(const Project &p, int slot, XmlScheduleValues &out) {
    if (!p.xmlBaselines)
        return false;
    bool present = false;
    for (const auto &r : p.xmlBaselines->rows) {
        calculationCheckpoint();
        if (r.number != std::max(0, slot))
            continue;
        present = true;
        out.externalBaselineRows.emplace(xmlBaselineRowKey(r.kind, r.id, r.resourceId), r);
        if (r.kind == "task" && r.start && r.finish)
            out.baselineTasks.emplace(r.id, std::pair{*r.start, *r.finish});
        if (r.kind == "project") {
            out.baselineStart = r.start;
            out.baselineFinish = r.finish;
        }
    }
    if (!present)
        return false;
    const auto &source = xmlBaselineOrigin(*p.xmlBaselines, std::max(0, slot));
    out.externalBaseline = true;
    out.baselineCaption = "외부 XML Baseline" + std::to_string(std::max(0, slot)) + " · " + source.sourceName + " · " +
                          source.sourceSha256.substr(0, 12);
    auto calendar = std::make_shared<PlanInput>();
    TaskCalendar c;
    c.id = "xml-baseline-calendar";
    c.name = "채택 당시 프로젝트 달력";
    c.weekdays = source.weekdays;
    c.holidays = source.holidays;
    c.periods.clear();
    for (auto [a, b] : source.periods)
        c.periods.push_back({a, b});
    calendar->calendars.push_back(c);
    calendar->xmlFormulaCalendarId = c.id;
    out.baselineDurationCalendar = std::move(calendar);
    return true;
}
void selectedValues(const Project &p, int slot, const std::set<std::string> &refs, XmlScheduleValues &values) {
    if (externalValues(p, slot, values))
        return;
    const auto version = std::uint64_t(slot < 0 ? 1 : slot + 1);
    values.baselineCaption = slot < 0 ? "최초 native 기준선(v1)"
                                      : "Baseline" + std::to_string(slot) + " · native v" + std::to_string(version);
    const Baseline *selected = nullptr;
    bool duplicate = false;
    if (slot < 0)
        selected = p.baselines.empty() ? nullptr : &p.baselines.front();
    else
        for (const auto &b : p.baselines)
            if (b.version == version) {
                duplicate = selected != nullptr;
                if (duplicate)
                    break;
                selected = &b;
            }
    if (duplicate)
        values.baselineIssue = "기준선 버전이 중복됩니다.";
    else
        baselineValues(p, selected, version, refs, values);
}
} // namespace
std::shared_ptr<const XmlScheduleValues> baselineExchangeValues(const Project &p, int slot) {
    if (slot < 0 || slot > 10)
        throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 번호 오류"});
    auto values = std::make_shared<XmlScheduleValues>();
    selectedValues(p, slot,
                   {"builtin:baselinestart", "builtin:baselinefinish", "builtin:baselineduration",
                    "builtin:baselinework", "builtin:baselinecost"},
                   *values);
    return values;
}
void addBaselineFormulaValues(const Project &p, XmlScheduleValues &out) {
    std::map<int, std::set<std::string>> requested;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && f.xmlFormula->enabled)
                for (const auto &r : f.xmlFormula->references)
                    if (xmlBaselineReference(r.key))
                        requested[xmlBaselineSlot(r.key).value_or(-1)].insert(xmlBaselineBaseKey(r.key));
    for (const auto &[slot, references] : requested) {
        calculationCheckpoint();
        auto numbered = slot < 0 ? nullptr : std::make_shared<XmlScheduleValues>();
        auto &values = numbered ? *numbered : out;
        selectedValues(p, slot, references, values);
        if (numbered) {
            out.baselineCaption += (out.baselineCaption.empty() ? "" : " / ") + values.baselineCaption;
            out.numberedBaselines.emplace(slot, std::move(numbered));
        }
    }
}
} // namespace jobs
