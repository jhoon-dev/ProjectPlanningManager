#include "domain/XmlPartition.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
namespace jobs {
std::string xmlPartition(std::int64_t number, std::int64_t start, std::int64_t stop, std::int64_t interval) {
    calculationCheckpoint();
    constexpr std::int64_t bound = 9000000000000LL;
    for (const auto n : {number, start, stop, interval})
        if (n < -bound || n > bound)
            throw AppError(ErrorCode::FORMULA_INVALID, {"Partition의 정수 인수는 native 수치 범위 내여야 합니다."});
    if (start < 0 || stop <= start || interval < 1)
        throw AppError(ErrorCode::FORMULA_INVALID, {"Partition은 start>=0, stop>start, interval>=1입니다."});
    const auto width = std::max(std::to_string(stop + 1).size(), std::to_string(start - 1).size());
    const auto pad = [&](const std::string &s) { return std::string(width - s.size(), ' ') + s; };
    if (number < start)
        return pad("") + ':' + pad(std::to_string(start - 1));
    if (number > stop)
        return pad(std::to_string(stop + 1)) + ':' + pad("");
    const auto lower = start + (number - start) / interval * interval;
    const auto upper = std::min(stop, lower + interval - 1);
    return pad(std::to_string(lower)) + ':' + pad(std::to_string(upper));
}
} // namespace jobs
