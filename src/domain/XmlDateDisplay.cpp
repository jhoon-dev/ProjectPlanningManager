#include "domain/XmlDateDisplay.h"
#include "domain/XmlFormatting.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <array>
namespace jobs {
std::string xmlFormatDateTime(const std::string &raw, int namedFormat) {
    calculationCheckpoint();
    if (namedFormat < 0 || namedFormat > 4)
        throw AppError(ErrorCode::FORMULA_INVALID, {"FormatDateTime 표시 형식은 반올림 후 0~4입니다."});
    if (raw.size() > 32)
        throw AppError(ErrorCode::FORMULA_INVALID, {"FormatDateTime에는 명시 ISO 날짜가 필요합니다."});
    const auto normalized = normalizeFieldDateTime(raw.size() == 10 ? raw + "T00:00:00" : raw);
    XmlFormatInput input;
    input.kind = XmlFormatKind::Date;
    // Display precision does not change the stored Date or round into another day.
    input.text = normalized.substr(0, 19);
    static const std::array<const char *, 5> masks = {"General Date", "Long Date", "Short Date", "Long Time",
                                                      "Short Time"};
    const bool timeOnly = namedFormat == 0 && input.text.substr(0, 10) == "1899-12-30";
    return *xmlFormat(input, timeOnly ? "Long Time" : masks[std::size_t(namedFormat)], 1033);
}
} // namespace jobs
