#include "persistence/ResourceCurveExchange.h"
#include "domain/Project.h"
#include <QRegularExpression>
namespace jobs {
namespace {
[[noreturn]] void bad() {
    throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID",
                              {"곡선의 정수·소수 정밀도·공수 형식을 확인하세요."},
                              {},
                              "RESOURCE_PLANNING_INVALID"});
}
} // namespace
std::int64_t curveDecimal(const QString &s, std::int64_t scale, std::int64_t maximum) {
    static const QRegularExpression re("^(0|[1-9][0-9]*)(?:\\.([0-9]{1,6})0*)?$");
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch() || s.size() > 40)
        bad();
    bool ok = false;
    const auto whole = m.captured(1).toLongLong(&ok);
    if (!ok || whole > maximum / scale + 1)
        bad();
    const auto part = m.captured(2).leftJustified(6, '0').toLongLong();
    const auto result = whole * scale + (part * scale + 500000) / 1000000;
    if (result > maximum)
        bad();
    return result;
}
std::int64_t curveWorkDuration(const QString &s) {
    static const QRegularExpression re(
        "^PT(?:(\\d+(?:\\.\\d{1,6})?)H)?(?:(\\d+(?:\\.\\d{1,6})?)M)?(?:(\\d+(?:\\.\\d{1,6})?)S)?$");
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty() && m.captured(3).isEmpty()))
        bad();
    std::int64_t total = 0;
    for (int i = 1; i <= 3; ++i)
        if (!m.captured(i).isEmpty()) {
            const auto numerator = i == 1 ? 6000LL : i == 2 ? 100LL : 5LL;
            const auto denominator = i == 3 ? 3000000LL : 1000000LL;
            const auto maximum = i == 1 ? maxWork / 6000 + 1 : i == 2 ? maxWork / 100 + 1 : maxWork * 3 / 5 + 1;
            const auto value = curveDecimal(m.captured(i), 1000000, maximum * 1000000);
            if (value * numerator % denominator)
                bad();
            const auto work = value * numerator / denominator;
            if (work > maxWork - total)
                bad();
            total += work;
        }
    return total;
}
QString curveWorkText(std::int64_t work) {
    if (work < 0 || work > maxWork)
        bad();
    const auto minutes = work / 100, secondsTenths = (work % 100) * 6;
    return QString("PT%1H%2M%3.%4S")
        .arg(minutes / 60)
        .arg(minutes % 60)
        .arg(secondsTenths / 10)
        .arg(secondsTenths % 10);
}
QString curveHourText(std::int64_t work) {
    if (work < 0 || work > maxWork)
        bad();
    const auto whole = work / 6000, micro = ((work % 6000) * 1000000 + 3000) / 6000;
    return QString::number(whole) + "." + QString::number(micro).rightJustified(6, '0');
}
} // namespace jobs
