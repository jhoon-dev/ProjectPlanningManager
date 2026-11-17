#include "calculation/ScheduleRisk.h"
#include "calculation/WorkCalendar.h"
#include "domain/Workspace.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::WORKSPACE_INVALID, {message});
}
double uniform(std::uint64_t &state) {
    auto z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return double((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
}
std::int64_t triangular(const ScheduleRisk &r, double u) {
    if (r.optimistic == r.pessimistic)
        return r.likely;
    const double low = double(r.optimistic), high = double(r.pessimistic), mode = double(r.likely);
    const auto fraction = (mode - low) / (high - low);
    return std::llround(u < fraction ? low + std::sqrt(u * (high - low) * (mode - low))
                                     : high - std::sqrt((1 - u) * (high - low) * (high - mode)));
}
std::string quantile(std::vector<std::string> values, int percent) {
    std::sort(values.begin(), values.end());
    const auto index = (values.size() * std::size_t(percent) + 99) / 100 - 1;
    return values.at(index);
}
} // namespace
RiskResult analyzeScheduleRisk(const Project &source, const std::vector<ScheduleRisk> &allRisks,
                               const RiskOptions &options, std::shared_ptr<CalculationControl> control) {
    RiskResult result;
    result.documentId = source.documentId;
    result.revision = source.revision;
    result.seed = options.seed;
    if (options.iterations < 2 || options.iterations > 10000 || source.tasks.size() > 10000 || allRisks.size() > 1000 ||
        source.tasks.size() * std::size_t(options.iterations) > 2000000 ||
        (options.deadline && !validStamp(*options.deadline)))
        invalid("확률 일정 반복 2~10000·작업×반복 200만·마감 시각을 확인하세요.");
    validate(source);
    if (!source.activities.empty() || source.planLeveling)
        invalid("확률 일정은 지원 활동·채택 평준화가 없는 계획 CPM을 지원합니다.");
    std::map<std::string, std::size_t> tasks;
    for (std::size_t i = 0; i < source.tasks.size(); ++i) {
        const auto &t = source.tasks[i];
        if (!t.durationMinutes || t.taskType != TaskType::FixedDuration || usesWork(t) || !t.breaks.empty())
            invalid("확률 일정은 기간이 확정된 고정 기간·미분할 계획을 지원합니다.");
        tasks.emplace(t.id, i);
        result.criticalCounts[t.id] = 0;
    }
    std::vector<ScheduleRisk> risks;
    std::set<std::string> ids, sampledTasks;
    for (const auto &r : allRisks) {
        if (r.projectId != source.documentId || r.state == "closed")
            continue;
        if (r.state != "open" || r.id.empty() || !ids.insert(r.id).second || !tasks.contains(r.taskId) ||
            !sampledTasks.insert(r.taskId).second || r.optimistic < 0 || r.optimistic > r.likely ||
            r.likely > r.pessimistic || r.pessimistic > 5000000 || r.probability < 0 || r.probability > 10000)
            invalid("위험 ID·작업·삼각 분포·확률을 확인하세요. 열린 위험은 작업당 하나입니다.");
        for (const auto &a : source.assignments)
            if (a.taskId == r.taskId && !a.periods.empty())
                invalid("변동 기간 작업의 기간별 배정 구간은 지원하지 않습니다.");
        risks.push_back(r);
    }
    std::sort(risks.begin(), risks.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    try {
        EvaluationScope scope(control);
        calculationCheckpoint();
        const auto baseline = calculatePlan(source);
        if (baseline.status != CalculationStatus::Valid || !baseline.projectFinish)
            invalid("결정론 계획 CPM이 정상이어야 위험 분석을 실행할 수 있습니다.");
        result.baselineFinish = *baseline.projectFinish;
        auto state = options.seed;
        std::vector<std::string> finishes;
        auto trial = source;
        for (int iteration = 0; iteration < options.iterations; ++iteration) {
            calculationCheckpoint();
            std::map<std::string, std::pair<double, double>> groups;
            for (const auto &r : risks) {
                double occurrence, duration;
                if (r.group.empty()) {
                    occurrence = uniform(state);
                    duration = uniform(state);
                } else {
                    if (!groups.contains(r.group)) {
                        const auto event = uniform(state);
                        groups.emplace(r.group, std::pair{event, uniform(state)});
                    }
                    std::tie(occurrence, duration) = groups.at(r.group);
                }
                const auto row = tasks.at(r.taskId);
                trial.tasks[row].durationMinutes =
                    occurrence * 10000 < r.probability ? triangular(r, duration) : *source.tasks[row].durationMinutes;
            }
            const auto sample = calculatePlan(trial);
            if (sample.status != CalculationStatus::Valid || !sample.projectFinish)
                invalid("표본 일정에 제약 충돌/계산 실패가 있습니다. 분위수를 게시하지 않았습니다.");
            finishes.push_back(*sample.projectFinish);
            for (const auto &t : sample.tasks)
                if (t.critical)
                    ++result.criticalCounts[t.taskId];
            if (options.deadline && *sample.projectFinish <= *options.deadline)
                ++result.deadlineHits;
            ++result.iterations;
        }
        WorkCalendar calendar(source);
        const auto middle = finishes.begin() + finishes.size() / 2;
        result.medianDriftMinutes = std::abs(calendar.offset(quantile({finishes.begin(), middle}, 50)) -
                                             calendar.offset(quantile({middle, finishes.end()}, 50)));
        for (int q : {10, 50, 80, 90})
            result.finishQuantiles[q] = quantile(finishes, q);
        calculationCheckpoint();
        result.status = CalculationStatus::Valid;
    } catch (const CalculationCancelled &) {
        result.status = CalculationStatus::Cancelled;
        result.finishQuantiles.clear();
        result.criticalCounts.clear();
        result.deadlineHits = 0;
    }
    return result;
}
std::vector<PortfolioSummary> summarizePortfolio(const Workspace &w,
                                                 const std::vector<std::shared_ptr<const Project>> &sources) {
    validateWorkspace(w);
    std::map<std::pair<std::string, std::string>, PortfolioSummary> rows;
    std::map<std::string, std::shared_ptr<const Project>> projects;
    for (const auto &p : sources)
        if (!p || !projects.emplace(p->documentId, p).second)
            invalid("포트폴리오 입력 프로젝트 중복/없음");
    for (const auto &p : w.governance.portfolio) {
        auto &r = rows[{p.portfolio, p.currency}];
        r.portfolio = p.portfolio;
        r.currency = p.currency;
        ++r.projects;
        r.budgetMinor += p.budgetMinor;
        for (const auto &risk : w.governance.risks)
            if (risk.projectId == p.projectId && risk.state == "open")
                ++r.openRisks;
        if (!projects.contains(p.projectId))
            continue;
        ++r.loaded;
        const auto schedule = calculatePlan(*projects.at(p.projectId));
        if (schedule.status == CalculationStatus::Valid && schedule.projectFinish) {
            ++r.validSchedules;
            if (!r.finish || *r.finish < *schedule.projectFinish)
                r.finish = schedule.projectFinish;
        }
    }
    std::vector<PortfolioSummary> result;
    for (const auto &[key, row] : rows)
        result.push_back(row);
    return result;
}
} // namespace jobs
