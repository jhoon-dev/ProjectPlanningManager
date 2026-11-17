#include "persistence/UdfMapping.h"
#include "domain/Classification.h"
#include <QRegularExpression>
#include <QString>
namespace jobs {
namespace {
[[noreturn]] void bad(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
std::int64_t integer(const QString &s, std::int64_t limit) {
    bool ok = false;
    const auto n = s.toLongLong(&ok);
    if (!ok || n < -limit || n > limit)
        bad("XML 사용자 필드 수치 한도를 확인하세요.");
    return n;
}
} // namespace
std::optional<CustomFieldType> xmlCfType(const std::string &s) {
    if (s == "0")
        return CustomFieldType::Cost;
    if (s == "1")
        return CustomFieldType::Date;
    if (s == "2")
        return CustomFieldType::Duration;
    if (s == "3")
        return CustomFieldType::Finish;
    if (s == "6")
        return CustomFieldType::Start;
    if (s == "4")
        return CustomFieldType::Flag;
    if (s == "5")
        return CustomFieldType::Number;
    if (s == "7")
        return CustomFieldType::Text;
    return {};
}
std::string xmlCfType(CustomFieldType t) {
    switch (t) {
    case CustomFieldType::Cost:
        return "0";
    case CustomFieldType::Date:
        return "1";
    case CustomFieldType::Start:
        return "6";
    case CustomFieldType::Finish:
        return "3";
    case CustomFieldType::Duration:
        return "2";
    case CustomFieldType::Flag:
        return "4";
    case CustomFieldType::Number:
        return "5";
    default:
        return "7";
    }
}
int xmlLookupType(CustomFieldType t) {
    switch (t) {
    case CustomFieldType::Cost:
        return 9;
    case CustomFieldType::Date:
    case CustomFieldType::Start:
        return 4;
    case CustomFieldType::Finish:
        return 27;
    case CustomFieldType::Duration:
        return 6;
    case CustomFieldType::Flag:
        return 17;
    case CustomFieldType::Number:
        return 15;
    default:
        return 21;
    }
}
std::optional<CustomFieldType> xmlLookupType(int n) {
    for (auto t : {CustomFieldType::Text, CustomFieldType::Number, CustomFieldType::Date, CustomFieldType::Flag,
                   CustomFieldType::Cost, CustomFieldType::Duration, CustomFieldType::Finish})
        if (xmlLookupType(t) == n)
            return t;
    return {};
}
int xmlFieldDurationFormat(const std::string &value) {
    const auto d = parseFieldDuration(value);
    return (d.elapsed ? 4 : 3) + (d.estimated ? 32 : 0);
}
std::string importXmlUdfScalar(const PlanInput &p, CustomFieldType type, const std::string &raw, int format,
                               bool lookup, bool packedDate) {
    const auto text = QString::fromStdString(raw);
    if (dateTimeCustomField(type)) {
        auto value = raw;
        if (packedDate && QRegularExpression("^[+-]?[0-9]+$").match(text).hasMatch())
            value = unpackFieldDateTime(integer(text, 200000000000LL));
        if (type != CustomFieldType::Date && value.size() == 10 && packedDate)
            value += "T00:00:00";
        return normalizeLookupScalar(type, value, p.currency);
    }
    if (type == CustomFieldType::Cost) {
        static const QRegularExpression re("^([+-]?[0-9]+)(?:\\.0+)?$");
        const auto m = re.match(text.trimmed());
        if (!m.hasMatch())
            bad("XML Cost는 최소 통화 단위로 정확히 표현되어야 합니다.");
        const auto wire = integer(m.captured(1), maxMoney * 100);
        const auto scale = currencyDecimals(p.currency) ? 1 : 100;
        if (wire % scale)
            bad("XML Cost의 통화 정밀도를 확인하세요.");
        return formatFieldCost(wire / scale, p.currency);
    }
    if (type == CustomFieldType::Duration) {
        if (lookup) {
            if (!QRegularExpression("^[+-]?[0-9]+$").match(text.trimmed()).hasMatch())
                bad("목록 기간은 정수 1/10분이어야 합니다.");
            const auto n = integer(text.trimmed(), 9999999999LL);
            return formatFieldDuration({n * (fieldNumberScale / 10), false, false});
        }
        int base = format;
        bool estimated = base >= 32;
        if (estimated)
            base -= 32;
        if (base < 3 || base > 12)
            bad("XML DurationFormat의 시간 단위가 지원 범위를 벗어납니다.");
        static const QRegularExpression re("^(-)?PT(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+(?:\\.\\d{1,6})?)S)?$");
        const auto m = re.match(text.trimmed());
        if (!m.hasMatch() || (m.captured(2).isEmpty() && m.captured(3).isEmpty() && m.captured(4).isEmpty()))
            bad("XML Duration은 시간·분·초의 ISO 기간이어야 합니다.");
        const auto hours = m.captured(2).isEmpty() ? 0 : integer(m.captured(2), 16666666);
        const auto minutes = m.captured(3).isEmpty() ? 0 : integer(m.captured(3), 999999999);
        const auto seconds = m.captured(4).isEmpty() ? 0 : parseFieldNumber(m.captured(4).toStdString());
        if (seconds % 60)
            bad("XML 기간의 소수 정밀도가 1/1,000,000분을 초과합니다.");
        auto amount = (hours * 60 + minutes) * fieldNumberScale + seconds / 60;
        if (amount > 999999999999999LL)
            bad("XML 기간 한도를 초과했습니다.");
        if (!m.captured(1).isEmpty())
            amount = -amount;
        return formatFieldDuration({amount, base % 2 == 0, estimated});
    }
    if (type == CustomFieldType::Flag) {
        const auto v = text.trimmed().toStdString();
        if (v != "0" && v != "1" && v != "true" && v != "false")
            bad("XML Flag는 0/1 또는 true/false여야 합니다.");
        return normalizeLookupScalar(type, v, p.currency);
    }
    return normalizeLookupScalar(type, raw, p.currency);
}
std::string exportXmlUdfScalar(const PlanInput &p, CustomFieldType type, const std::string &raw, bool lookup,
                               bool packedDate) {
    if (dateTimeCustomField(type)) {
        if (packedDate)
            return std::to_string(packFieldDateTime(raw));
        return raw.size() == 10 ? raw + "T00:00:00" : raw;
    }
    if (type == CustomFieldType::Cost)
        return std::to_string(parseFieldCost(raw, p.currency) * (currencyDecimals(p.currency) ? 1 : 100));
    if (type == CustomFieldType::Duration) {
        const auto d = parseFieldDuration(raw);
        if (lookup) {
            if (d.elapsed || d.estimated || d.minutes % (fieldNumberScale / 10))
                bad("기간 목록은 일반 정수 1/10분만 지원합니다.");
            return std::to_string(d.minutes / (fieldNumberScale / 10));
        }
        auto n = d.minutes < 0 ? -d.minutes : d.minutes;
        const auto hours = n / (60 * fieldNumberScale);
        n %= 60 * fieldNumberScale;
        const auto minutes = n / fieldNumberScale;
        const auto seconds = (n % fieldNumberScale) * 60;
        return (d.minutes < 0 ? "-PT" : "PT") + std::to_string(hours) + "H" + std::to_string(minutes) + "M" +
               formatFieldNumber(seconds) + "S";
    }
    if (type == CustomFieldType::Flag)
        return normalizeLookupScalar(type, raw, p.currency);
    return raw;
}
} // namespace jobs
