#include "domain/Productivity.h"
#include "domain/CalculationControl.h"
#include <algorithm>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID", {s}, {}, "RESOURCE_PLANNING_INVALID"});
}
bool label(const std::string &s, std::size_t n = 4096) {
    return !s.empty() && s.size() <= n && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
} // namespace
void validateProductivityRate(const ProductivityRate &r) {
    if (!label(r.unit, 64) || !label(r.reference) || !label(r.reporter, 1024) || !validDate(r.effectiveDate) ||
        r.quantityMilliPerHour < 1 || r.quantityMilliPerHour > 1000000000)
        invalid("생산성의 물량 단위·양수 시간당 물량·기준일·근거·작성자를 확인하세요.");
}
std::int64_t productivityWork(std::int64_t quantity, const ProductivityRate &r) {
    validateProductivityRate(r);
    if (quantity < 0 || quantity > 1000000000)
        invalid("작업 물량은 0~1,000,000,000 최소 물량 단위(소수3자리)입니다.");
    const auto work = (quantity * 6000 + r.quantityMilliPerHour - 1) / r.quantityMilliPerHour;
    if (work > maxWork)
        invalid("필요 공수가 지원 범위를 넘습니다.");
    return work;
}
bool hasResourcePlanning(const PlanInput &p) {
    return std::any_of(p.resources.begin(), p.resources.end(),
                       [](const auto &r) { return r.productivity.has_value(); }) ||
           std::any_of(p.assignments.begin(), p.assignments.end(),
                       [](const auto &a) { return a.nativeCurve || a.productivityEstimate; });
}
void validateResourcePlanning(const PlanInput &p) {
    for (const auto &r : p.resources)
        if (r.productivity)
            validateProductivityRate(*r.productivity);
    std::size_t count = 0;
    for (const auto &a : p.assignments) {
        calculationCheckpoint();
        if (a.productivityEstimate) {
            const auto &e = *a.productivityEstimate;
            if (productivityWork(e.quantityMilli, e.rate) != e.workUnitMinutes || !label(e.reason) ||
                !validUtcStamp(e.recordedAt) || !e.confirmed || !label(e.reporter, 1024) ||
                (e.application != "duration-rates-v1" && e.application != "work-duration-v1"))
                invalid("물량→공수 산정의 정수 결과·사유·UTC·명시 확인을 검토하세요.");
        }
        if (!a.nativeCurve)
            continue;
        const auto &c = *a.nativeCurve;
        if ((!c.reference.empty() || !c.reporter.empty() || !c.recordedAt.empty()) &&
            (!label(c.reference) || !label(c.reporter, 1024) || !validUtcStamp(c.recordedAt)))
            invalid("곡선의 기록 근거·작성자·UTC를 확인하세요.");
        if ((c.source != "p6-xer" && c.source != "mspdi" && c.source != "native-21") || !label(c.externalId, 1024) ||
            !label(c.name, 1024) || c.mspCode < -1 || c.mspCode > 8 || c.baseUnits < 1 || c.baseUnits > 100000 ||
            c.durationMinutes < 1 || c.durationMinutes > 5000000 || c.workUnitMinutes < 0 ||
            c.workUnitMinutes > maxWork || c.projectedPeriods.size() > 1024 ||
            (!c.weights.empty() && c.weights.size() != 21))
            invalid("외부 곡선의 출처/ID·기준 기간/공수·배정률·구간 한도를 확인하세요.");
        if (c.weights.empty() && !(c.source == "mspdi" && c.mspCode == 8))
            invalid("21개 지점의 곡선 정의가 필요합니다.");
        std::int64_t sum = 0;
        for (int weight : c.weights) {
            if (weight < 0 || weight > 100000000)
                invalid("곡선 비중은 0~100%입니다.");
            sum += weight;
        }
        if (!c.weights.empty() && (c.weights.front() != 0 || sum != 100000000))
            invalid("곡선의 0% 지점은0, 21개 비중 합계는100%여야 합니다.");
        std::int64_t last = 0;
        for (const auto &v : c.projectedPeriods) {
            if (v.fromMinute < last || v.fromMinute < 0 || v.toMinute <= v.fromMinute || v.toMinute > 5000000 ||
                v.units < 0 || v.units > 100000)
                invalid("곡선의 보존 투영 구간을 확인하세요.");
            last = v.toMinute;
        }
        count += c.projectedPeriods.size();
        if (count > 100000)
            invalid("외부 곡선의 보존 투영은 합계100,000구간까지입니다.");
    }
}
} // namespace ppm
