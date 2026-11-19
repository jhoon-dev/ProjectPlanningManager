#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
std::string pad(int n, int width = 2) {
    auto s = std::to_string(n);
    return std::string(std::max(0, width - int(s.size())), '0') + s;
}
} // namespace
bool dateTimeCustomField(CustomFieldType type) {
    return type == CustomFieldType::Date || type == CustomFieldType::Start || type == CustomFieldType::Finish;
}
std::string normalizeFieldDateTime(const std::string &value) {
    if (value.size() < 16 || !validDate(value.substr(0, 10)) || (value[10] != 'T' && value[10] != ' ') ||
        value[13] != ':')
        invalid("일시는 YYYY-MM-DDTHH:MM:SS[.소수6자리]로 입력하세요. 시간대 오프셋은 지원하지 않습니다.");
    for (auto i : {11, 12, 14, 15})
        if (value[i] < '0' || value[i] > '9')
            invalid("일시의 시·분을 확인하세요.");
    if (std::stoi(value.substr(11, 2)) > 23 || std::stoi(value.substr(14, 2)) > 59)
        invalid("일시의 시·분 범위를 확인하세요.");
    std::string result = value.substr(0, 16);
    result[10] = 'T';
    if (value.size() == 16)
        return result + ":00";
    if (value.size() < 19 || value[16] != ':' || value[17] < '0' || value[17] > '5' || value[18] < '0' ||
        value[18] > '9')
        invalid("일시의 초 범위를 확인하세요.");
    result += value.substr(16, 3);
    if (value.size() == 19)
        return result;
    if (value[19] != '.' || value.size() < 21 || value.size() > 26 ||
        std::any_of(value.begin() + 20, value.end(), [](char c) { return c < '0' || c > '9'; }))
        invalid("일시의 소수 초 정밀도·시간대 표기를 확인하세요.");
    auto fraction = value.substr(20);
    while (!fraction.empty() && fraction.back() == '0')
        fraction.pop_back();
    return result + (fraction.empty() ? "" : "." + fraction);
}
std::int64_t packFieldDateTime(const std::string &raw) {
    const auto v = normalizeFieldDateTime(raw.size() == 10 ? raw + "T00:00:00" : raw);
    if (v.size() != 19 || std::stoi(v.substr(17, 2)) % 6)
        invalid("GUID 날짜 목록은 6초 단위의 시각만 정확히 보존할 수 있습니다.");
    const auto date = std::chrono::year{std::stoi(v.substr(0, 4))} / unsigned(std::stoi(v.substr(5, 2))) /
                      unsigned(std::stoi(v.substr(8, 2)));
    const auto epoch = std::chrono::sys_days{std::chrono::year{1983} / 12 / 31};
    const auto days = (std::chrono::sys_days{date} - epoch).count();
    if (days < 0)
        invalid("GUID 날짜 목록은 1983-12-31 이후만 지원합니다.");
    const auto seconds =
        std::stoi(v.substr(11, 2)) * 3600 + std::stoi(v.substr(14, 2)) * 60 + std::stoi(v.substr(17, 2));
    return days * 65536 + seconds / 6;
}
std::string unpackFieldDateTime(std::int64_t raw) {
    const auto epoch = std::chrono::sys_days{std::chrono::year{1983} / 12 / 31};
    const auto maximum = (std::chrono::sys_days{std::chrono::year{9999} / 12 / 31} - epoch).count();
    if (raw < 0 || raw / 65536 > maximum || raw % 65536 >= 14400)
        invalid("GUID 날짜 목록의 압축 날짜·시간 범위를 확인하세요.");
    const auto date = std::chrono::year_month_day{epoch + std::chrono::days{raw / 65536}};
    const auto seconds = int(raw % 65536) * 6;
    return pad(int(date.year()), 4) + "-" + pad(int(unsigned(date.month()))) + "-" + pad(int(unsigned(date.day()))) +
           "T" + pad(seconds / 3600) + ":" + pad(seconds / 60 % 60) + ":" + pad(seconds % 60);
}
bool hasDateTimeFields(const PlanInput &p) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        std::unordered_set<std::string> keys;
        for (const auto &f : customFields(p, scope)) {
            calculationCheckpoint();
            if (f.type == CustomFieldType::Start || f.type == CustomFieldType::Finish)
                return true;
            if (f.type == CustomFieldType::Date)
                keys.insert("field:" + f.id);
            if (f.xmlLookup) {
                const auto &l = *f.xmlLookup;
                if (l.valueType == CustomFieldType::Start || l.valueType == CustomFieldType::Finish)
                    return true;
                if (l.valueType == CustomFieldType::Date &&
                    std::any_of(l.values.begin(), l.values.end(), [](const auto &v) { return v.value.size() > 10; }))
                    return true;
            }
        }
        auto values = [&](const auto &a) {
            return std::any_of(a.begin(), a.end(),
                               [&](const auto &v) { return keys.contains(v.key) && v.value.size() > 10; });
        };
        if (scope == FieldScope::Project && values(p.projectAttributes))
            return true;
        if (scope == FieldScope::Task)
            for (const auto &t : p.tasks)
                if (values(t.attributes))
                    return true;
        if (scope == FieldScope::Resource)
            for (const auto &r : p.resources)
                if (values(r.attributes))
                    return true;
        if (scope == FieldScope::Assignment)
            for (const auto &a : p.assignments)
                if (values(a.attributes))
                    return true;
    }
    return false;
}
XmlLookup sharedXmlLookup(XmlLookup l) {
    l.fieldGuid.clear();
    l.fieldName.clear();
    l.defaultOptionId.clear();
    l.maxValues = 1;
    l.restrictValues = false;
    std::erase_if(l.values, [](const auto &v) { return !v.listed; });
    if (dateTimeCustomField(l.valueType)) {
        l.valueType = CustomFieldType::Date;
        for (auto &v : l.values)
            v.value = normalizeFieldDateTime(v.value.size() == 10 ? v.value + "T00:00:00" : v.value);
    }
    return l;
}
} // namespace ppm
