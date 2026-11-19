#include "calculation/AssignmentPeriods.h"
#include "domain/Diagnostics.h"
#include <algorithm>
namespace ppm {
std::vector<AssignmentPeriod> assignmentCurve(std::int64_t duration, int units, AssignmentCurve curve) {
    if (duration <= 0 || duration > 5000000 || units <= 0 || units > 100000 ||
        (curve != AssignmentCurve::Flat && curve != AssignmentCurve::Front && curve != AssignmentCurve::Back &&
         curve != AssignmentCurve::Bell))
        throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"유효한 기간·기본 배정률·곡선을 선택하세요."});
    if (curve == AssignmentCurve::Flat)
        return {};
    const int count = int(std::min(std::int64_t{10}, duration));
    std::int64_t denominator = 0;
    auto weight = [&](int i) {
        return curve == AssignmentCurve::Front  ? count - i
               : curve == AssignmentCurve::Back ? i + 1
                                                : std::min(i + 1, count - i);
    };
    for (int i = 0; i < count; ++i)
        denominator += (duration * (i + 1) / count - duration * i / count) * weight(i);
    std::vector<AssignmentPeriod> raw, result;
    auto remainder = duration * units;
    for (int i = 0; i < count; ++i) {
        const auto from = duration * i / count, to = duration * (i + 1) / count;
        const auto rate = duration * units * weight(i) / denominator;
        raw.push_back({from, to, int(rate)});
        remainder -= (to - from) * rate;
    }
    auto append = [&](std::int64_t from, std::int64_t to, int rate) {
        if (rate > 100000)
            throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                           {"곡선의 최대 배정률이 100,000%를 넘습니다. 기본 배정률을 낮추세요."});
        if (from == to)
            return;
        if (!result.empty() && result.back().units == rate && result.back().toMinute == from)
            result.back().toMinute = to;
        else
            result.push_back({from, to, rate});
    };
    for (const auto &v : raw) {
        const auto extra = std::min(remainder, v.toMinute - v.fromMinute);
        if (extra)
            append(v.fromMinute, v.fromMinute + extra, v.units + 1);
        append(v.fromMinute + extra, v.toMinute, v.units);
        remainder -= extra;
    }
    return result;
}
} // namespace ppm
