#include "domain/XmlLogical.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <bit>
namespace jobs {
namespace {
constexpr std::int64_t scale = 1000000;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 논리 연산: " + message});
}
} // namespace
bool xmlLogicalFormula(const XmlFormula &f) {
    bool quote = false, date = false, ref = false;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    for (std::size_t i = 0; i < f.source.size(); ++i) {
        const auto c = f.source[i];
        if (ref) {
            if (c == '\\' && i + 1 < f.source.size())
                ++i;
            else if (c == ']')
                ref = false;
        } else if (quote) {
            if (c == '"') {
                if (i + 1 < f.source.size() && f.source[i + 1] == '"')
                    ++i;
                else
                    quote = false;
            }
        } else if (date) {
            if (c == '#')
                date = false;
        } else if (c == '"' || c == '#' || c == '[') {
            quote = c == '"';
            date = c == '#';
            ref = c == '[';
        } else if (letter(c)) {
            const auto start = i;
            while (i + 1 < f.source.size() &&
                   (letter(f.source[i + 1]) || (f.source[i + 1] >= '0' && f.source[i + 1] <= '9')))
                ++i;
            auto word = f.source.substr(start, i - start + 1);
            for (auto &v : word)
                if (v >= 'A' && v <= 'Z')
                    v += 'a' - 'A';
            if (word == "eqv" || word == "imp")
                return true;
        }
    }
    return false;
}
bool hasXmlLogicalOperators(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlLogicalFormula(*f.xmlFormula))
                return true;
    return false;
}
std::int32_t xmlLogicalInteger(std::int64_t scaled) {
    calculationCheckpoint();
    if (scaled % scale || scaled / scale < -2147483648LL || scaled / scale > 2147483647LL)
        invalid("숫자는32비트 정수만 지원합니다.");
    return static_cast<std::int32_t>(scaled / scale);
}
std::int64_t xmlLogicalNumber(const std::string &op, std::int64_t left, std::int64_t right) {
    const auto a = static_cast<std::uint32_t>(xmlLogicalInteger(left)),
               b = static_cast<std::uint32_t>(xmlLogicalInteger(right));
    if (op != "eqv" && op != "imp")
        invalid("지원하지 않는 연산자");
    const auto bits = op == "eqv" ? ~(a ^ b) : (~a | b);
    return std::int64_t(std::bit_cast<std::int32_t>(bits)) * scale;
}
} // namespace jobs
