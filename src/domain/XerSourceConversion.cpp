#include "domain/XerSourceRelink.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <map>
namespace ppm {
namespace {
[[noreturn]] void invalid(const char *message) {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {message});
}
bool kind(const std::string &s) {
    return s == "labor" || s == "equipment";
}
void ratio(std::int64_t n, std::int64_t d) {
    if (n < 1 || d < 1 || n > 1000000000 || d > 1000000000)
        invalid("환산비의 분자·분모는 1~1,000,000,000의 정수여야 합니다.");
}
// Binary long division avoids floating point and intermediate product overflow on MSVC too.
std::optional<std::int64_t> scale(std::optional<std::int64_t> amount, std::uint64_t n, std::uint64_t d,
                                  std::uint64_t limit, bool exact) {
    if (!amount)
        return {};
    std::uint64_t q = 0, r = 0;
    const auto whole = n / d, fraction = n % d;
    for (int bit = 63; bit >= 0; --bit) {
        if (q > limit / 2)
            invalid("환산 결과가 지원 금액·공수 범위를 초과합니다.");
        q *= 2;
        r *= 2;
        if ((std::uint64_t(*amount) >> bit) & 1) {
            if (whole > limit - q)
                invalid("환산 결과가 지원 금액·공수 범위를 초과합니다.");
            q += whole;
            r += fraction;
        }
        const auto carry = r / d;
        if (carry > limit - q)
            invalid("환산 결과가 지원 금액·공수 범위를 초과합니다.");
        q += carry;
        r %= d;
    }
    if (exact && r)
        invalid("공수 환산 결과가 최소 단위(1/100 근무분)의 정수가 아닙니다. 환산비를 확인하세요.");
    if (!exact && r >= (d + 1) / 2) {
        if (q == limit)
            invalid("환산 결과가 지원 금액 범위를 초과합니다.");
        ++q;
    }
    return std::int64_t(q);
}
std::optional<std::int64_t> add(std::optional<std::int64_t> a, std::optional<std::int64_t> b, std::int64_t limit) {
    if (!a)
        return b;
    if (!b)
        return a;
    if (*a > limit - *b)
        invalid("자원 종류 전환 후 집계가 지원 범위를 초과합니다.");
    return *a + *b;
}
} // namespace
XerFinancialSnapshot convertXerSource(const XerFinancialSnapshot &source, const XerSourceConversion &c) {
    validateXerFinancials(source);
    ratio(c.numerator, c.denominator);
    if (!validDate(c.effectiveDate) || c.reference.empty() || c.reference.size() > 4096 ||
        std::none_of(c.reference.begin(), c.reference.end(), [](unsigned char ch) { return ch > 32; }) ||
        std::any_of(c.reference.begin(), c.reference.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }))
        invalid("전환 기준일과 환율·단위 환산의 근거를 입력하세요.");
    const auto fromScale = currencyDecimals(source.currency) == 2 ? 100ULL : 1ULL;
    const auto toScale = currencyDecimals(c.targetCurrency) == 2 ? 100ULL : 1ULL;
    if (source.currency == c.targetCurrency && (c.numerator != 1 || c.denominator != 1))
        invalid("같은 통화에는 1/1 환율을 사용하세요.");
    std::map<std::string, XerResourceConversion> rules;
    for (const auto &r : c.resources) {
        ratio(r.workNumerator, r.workDenominator);
        if (!kind(r.fromType) || !kind(r.toType) || !rules.emplace(r.fromType, r).second ||
            (std::none_of(source.resources.begin(), source.resources.end(),
                          [&](const auto &v) { return v.type == r.fromType; }) &&
             std::none_of(source.periods.begin(), source.periods.end(), [&](const auto &e) {
                 return r.fromType == "labor" ? bool(e.laborWork || e.laborMinor)
                                              : bool(e.equipmentWork || e.equipmentMinor);
             })))
            invalid("원본에 존재하는 인력·장비 종류별로 한 개의 전환 규칙을 지정하세요.");
    }
    auto out = source;
    out.currency = c.targetCurrency;
    auto money = [&](auto a) {
        return scale(a, std::uint64_t(c.numerator) * toScale, std::uint64_t(c.denominator) * fromScale, maxMoney,
                     false);
    };
    std::map<std::string, std::string> types;
    for (auto &r : out.resources) {
        types[r.id] = r.type;
        if (rules.contains(r.type))
            r.type = rules.at(r.type).toType;
    }
    auto work = [&](auto a, const std::string &type) {
        if (!rules.contains(type))
            return a;
        const auto &r = rules.at(type);
        return scale(a, r.workNumerator, r.workDenominator, maxWork, true);
    };
    for (auto &t : out.tasks) {
        calculationCheckpoint();
        t.actualMinor = money(t.actualMinor);
    }
    for (auto &a : out.assignments) {
        calculationCheckpoint();
        a.regularMinor = money(a.regularMinor);
        a.overtimeMinor = money(a.overtimeMinor);
        a.regularWork = work(a.regularWork, types.at(a.resourceId));
        a.overtimeWork = work(a.overtimeWork, types.at(a.resourceId));
    }
    for (auto &e : out.periods) {
        calculationCheckpoint();
        for (auto m : {&XerFinancialPeriod::actualMinor, &XerFinancialPeriod::laborMinor,
                       &XerFinancialPeriod::equipmentMinor, &XerFinancialPeriod::materialMinor,
                       &XerFinancialPeriod::expenseMinor, &XerFinancialPeriod::bcwp, &XerFinancialPeriod::bcws})
            e.*m = money(e.*m);
        if (!e.resourceId.empty()) {
            e.work = work(e.work, types.at(e.resourceId));
            continue;
        }
        // Class totals are transformed simultaneously, so a swap never transforms a value twice.
        const auto old = e;
        e.laborWork.reset();
        e.equipmentWork.reset();
        e.laborMinor.reset();
        e.equipmentMinor.reset();
        for (const std::string type : {"labor", "equipment"}) {
            const auto target = rules.contains(type) ? rules.at(type).toType : type;
            const auto w = work(type == "labor" ? old.laborWork : old.equipmentWork, type);
            const auto m = type == "labor" ? old.laborMinor : old.equipmentMinor;
            auto &tw = target == "labor" ? e.laborWork : e.equipmentWork;
            auto &tm = target == "labor" ? e.laborMinor : e.equipmentMinor;
            tw = add(tw, w, maxWork);
            tm = add(tm, m, maxMoney);
        }
    }
    // Independent half-up rounding may violate a source's total/detail inequalities.
    // Reject instead of changing an observed amount to manufacture a reconciliation.
    try {
        validateXerFinancials(out);
    } catch (const AppError &) {
        invalid("환산 후 누계·기간 상세가 대사되지 않습니다. 반올림과 원본 집계를 확인하세요.");
    }
    return out;
}
} // namespace ppm
