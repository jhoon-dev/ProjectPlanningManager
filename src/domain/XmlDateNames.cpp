#include "domain/XmlDateNames.h"
#include "domain/XmlFormatting.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
namespace ppm {
std::string xmlMonthName(int month, bool abbreviate) {
    calculationCheckpoint();
    if (month < 1 || month > 12)
        throw AppError(ErrorCode::FORMULA_INVALID, {"MonthName의 월은 반올림 후 1~12입니다."});
    XmlFormatInput input;
    input.kind = XmlFormatKind::Date;
    input.text = "2000-" + std::string(month < 10 ? "0" : "") + std::to_string(month) + "-01T00:00:00";
    return *xmlFormat(input, abbreviate ? "mmm" : "mmmm", 1033);
}
std::string xmlWeekdayName(int weekday, bool abbreviate, int firstDay) {
    calculationCheckpoint();
    if (weekday < 1 || weekday > 7 || firstDay < 0 || firstDay > 7)
        throw AppError(ErrorCode::FORMULA_INVALID, {"WeekdayName의 요일은 1~7, 주 시작일은 0~7입니다."});
    if (!firstDay)
        firstDay = 1;
    // 2000-01-02 is Sunday. Reuse the existing Format names and abbreviations.
    const auto day = 2 + (weekday + firstDay - 2) % 7;
    XmlFormatInput input;
    input.kind = XmlFormatKind::Date;
    input.text = "2000-01-0" + std::to_string(day) + "T00:00:00";
    return *xmlFormat(input, abbreviate ? "ddd" : "dddd", 1033);
}
} // namespace ppm
