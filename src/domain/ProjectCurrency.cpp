#include "domain/ProjectCurrency.h"
#include "domain/XmlBaselines.h"
#include "domain/LedgerIdentity.h"
#include "domain/XerSourceRelink.h"
#include "domain/CalculationControl.h"
#include "domain/Classification.h"
#include <algorithm>
#include <set>
#include <unordered_map>
namespace jobs {
namespace {
[[noreturn]] void bad(const std::string &s) {
    throw AppError(Diagnostic{"CURRENCY_CONVERSION_INVALID", {s}, {}, "CURRENCY_CONVERSION_INVALID"});
}
bool label(const std::string &s, std::size_t max = 4096) {
    return !s.empty() && s.size() <= max && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
using Visit = std::function<void(const std::string &, const std::string &, std::optional<std::int64_t> &)>;
void plan(PlanInput &p, const std::string &path, const Visit &visit, const std::string &resultCurrency) {
    const auto &target = resultCurrency.empty() ? p.currency : resultCurrency;
    auto value = [&](const std::string &at, std::string &raw) {
        std::optional<std::int64_t> minor = parseFieldCost(raw, p.currency);
        visit(at, p.currency, minor);
        if (!minor)
            bad("사용자 금액을 미설정으로 바꿀 수 없습니다.");
        raw = formatFieldCost(*minor, target);
    };
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        for (auto &f : customFields(p, scope))
            if (f.xmlLookup && f.xmlLookup->valueType == CustomFieldType::Cost) {
                for (auto &v : f.xmlLookup->values)
                    value(path + "/customCost/" + std::to_string(int(scope)) + "/" + f.id + "/" + v.optionId, v.value);
                std::set<std::string> names;
                std::unordered_map<std::string, const XmlLookupValue *> entries;
                for (const auto &v : f.xmlLookup->values)
                    entries.emplace(v.optionId, &v);
                for (auto &o : f.options) {
                    calculationCheckpoint();
                    auto current = entries.at(o.id);
                    std::vector<std::string> parts;
                    std::set<std::string> seen;
                    while (current) {
                        if (!seen.insert(current->optionId).second || seen.size() > 16)
                            bad("금액 목록의 계층을 확인하세요.");
                        parts.push_back(current->value);
                        if (current->parentId.empty())
                            break;
                        const auto parent = current->parentId;
                        current = entries.at(parent);
                    }
                    o.name.clear();
                    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                        o.name += (o.name.empty() ? "" : " › ") + *it;
                    if (!names.insert(o.name).second) {
                        o.name += " [" + o.id + "]";
                        if (!names.insert(o.name).second)
                            bad("금액 목록 표시 이름이 중복됩니다.");
                    }
                }
            }
        auto attributes = [&](const std::string &id, std::vector<TaskAttribute> &values, bool summary = false) {
            for (auto &a : values) {
                const auto *f = customField(p, a.key, scope);
                if (f && f->type == CustomFieldType::Cost)
                    value(path + (summary ? "/customSummaryCost/" : "/customCost/") + std::to_string(int(scope)) + "/" +
                              id + "/" + f->id,
                          a.value);
            }
        };
        if (scope == FieldScope::Project)
            attributes("project", p.projectAttributes);
        if (scope == FieldScope::Task) {
            for (auto &t : p.tasks)
                attributes(t.id, t.attributes);
            attributes("root", p.taskSummaryAttributes, true);
            for (auto &g : p.wbsNodes)
                attributes("wbs/" + g.id, g.attributes, true);
        }
        if (scope == FieldScope::Resource)
            for (auto &r : p.resources)
                attributes(r.id, r.attributes);
        if (scope == FieldScope::Assignment)
            for (auto &a : p.assignments)
                attributes(assignmentAttributeId(a), a.attributes);
    }
    if (p.xmlBaselines)
        for (auto &r : p.xmlBaselines->rows) {
            const auto at = path + "/xmlBaselines/" + std::to_string(r.number) + "/" +
                            xmlBaselineRowKey(r.kind, r.id, r.resourceId);
            visit(at + "/Cost", p.currency, r.cost);
            for (auto &[key, n] : r.money) {
                std::optional<std::int64_t> amount = n;
                visit(at + "/" + key, p.currency, amount);
                if (!amount)
                    bad("외부 기준선 금액을 비울 수 없습니다.");
                n = *amount;
            }
            for (auto &t : r.timephased)
                if (t.value && !t.value->empty() && *t.value != "NA" && xmlBaselineTimeCost(t.type)) {
                    std::optional<std::int64_t> amount = xmlBaselineMoney(*t.value, p.currency);
                    visit(at + "/TimephasedData", p.currency, amount);
                    if (!amount)
                        bad("외부 기준선 시간분포 원가를 비울 수 없습니다.");
                    *t.value = xmlBaselineMoneyText(*amount, target);
                }
        }
    for (auto &r : p.resources)
        for (auto &v : r.rates)
            visit(path + "/resources/" + r.id + "/rates/" + v.effectiveDate, p.currency, v.hourlyMinor);
    for (auto &v : p.taskCosts)
        visit(path + "/taskCosts/" + v.taskId, p.currency, v.fixedMinor);
    for (auto &v : p.resourcePlan.materials)
        visit(path + "/materials/" + v.id, p.currency, v.unitPriceMinor);
    for (auto &v : p.resourcePlan.expenses)
        visit(path + "/expenses/" + v.id, p.currency, v.amountMinor);
}
void assignment(XerFinancialAssignment &a, const std::string &path, const std::string &currency, const Visit &v) {
    v(path + "/regularMinor", currency, a.regularMinor);
    v(path + "/overtimeMinor", currency, a.overtimeMinor);
}
void financial(XerFinancialSnapshot &s, const std::string &path, const Visit &v) {
    for (auto &t : s.tasks)
        v(path + "/tasks/" + t.id, s.currency, t.actualMinor);
    for (auto &a : s.assignments)
        assignment(a, path + "/assignments/" + a.taskId + "/" + a.resourceId, s.currency, v);
    for (std::size_t i = 0; i < s.periods.size(); ++i) {
        auto &p = s.periods[i];
        const auto at = path + "/periods/" + std::to_string(i);
        for (auto [name, member] : {std::pair{"actualMinor", &XerFinancialPeriod::actualMinor},
                                    {"laborMinor", &XerFinancialPeriod::laborMinor},
                                    {"equipmentMinor", &XerFinancialPeriod::equipmentMinor},
                                    {"materialMinor", &XerFinancialPeriod::materialMinor},
                                    {"expenseMinor", &XerFinancialPeriod::expenseMinor},
                                    {"bcwp", &XerFinancialPeriod::bcwp},
                                    {"bcws", &XerFinancialPeriod::bcws}})
            v(at + "/" + name, s.currency, p.*member);
    }
}
void all(Project &p, const Visit &v, const std::string &targetCurrency = {}) {
    plan(p, "current", v, targetCurrency);
    for (auto &b : p.baselines)
        plan(b.input, "baselines/" + b.id, v, targetCurrency);
    for (auto &s : p.scenarios)
        plan(s.input, "scenarios/" + s.id, v, targetCurrency);
    for (auto &c : p.costEntries)
        v("costEntries/" + c.id, c.currency, c.actualMinor);
    for (auto &c : p.periodActuals)
        v("periodActuals/" + c.id, c.currency, c.actualMinor);
    if (p.xerFinancials)
        financial(*p.xerFinancials, "xerFinancials", v);
    for (auto &r : p.xerFinancialRevisions) {
        financial(r.financials, "xerFinancialRevisions/" + r.id, v);
        if (r.merge)
            for (auto &a : r.merge->assignmentOverrides)
                assignment(a, "xerOverrides/" + r.id + "/" + a.taskId + "/" + a.resourceId, r.financials.currency, v);
    }
    for (auto &a : p.externalEvmAdoptions)
        for (auto &r : a.rows) {
            const auto at = "externalEvm/" + a.id + "/" + std::to_string(r.periodIndex);
            v(at + "/openingEv", a.currency, r.openingEv);
            v(at + "/openingPv", a.currency, r.openingPv);
        }
    for (auto &e : p.financialPeriodEvents) {
        const auto at = "financialPeriods/" + e.id;
        for (auto &r : e.period.values) {
            v(at + "/" + r.taskId + "/budget", e.currency, r.budget);
            v(at + "/" + r.taskId + "/earned", e.currency, r.earned);
        }
        for (auto &t : e.tasks) {
            auto balance = [&](FinancialPeriodBalance &b, const char *side) {
                const auto path = at + "/" + t.taskId + "/" + side;
                v(path + "/bac", e.currency, b.bac);
                v(path + "/pv", e.currency, b.pv);
                v(path + "/ev", e.currency, b.ev);
                v(path + "/ac", e.currency, b.ac);
            };
            balance(t.opening, "opening");
            balance(t.closing, "closing");
            v(at + "/" + t.taskId + "/dated", e.currency, t.dated);
        }
    }
}
void currentMatches(const Project &p, const CurrencyConversion &e) {
    auto actual = p, expected = *e.after;
    clearTransitionHistories(actual);
    expected.documentId = actual.documentId;
    expected.derivedFrom = actual.derivedFrom;
    if (expected.planLeveling || expected.remainingLeveling) {
        if (actual.planLeveling || actual.remainingLeveling ||
            actual.levelingDecisions.size() != expected.levelingDecisions.size() + 1 ||
            !std::equal(expected.levelingDecisions.begin(), expected.levelingDecisions.end(),
                        actual.levelingDecisions.begin()) ||
            actual.levelingDecisions.back().action != "invalidate" ||
            actual.levelingDecisions.back().reason != "프로젝트 전체 통화 전환" ||
            actual.levelingDecisions.back().recordedRevision != e.recordedRevision)
            bad("통화 전환의 평준화 해제 기록이 일치하지 않습니다.");
        expected.planLeveling.reset();
        expected.remainingLeveling.reset();
        expected.levelingDecisions = actual.levelingDecisions;
    }
    if (actual != expected)
        bad("통화 전환 직후 현재 문서와 검증한 환산 결과가 다릅니다.");
}
} // namespace
void validateCurrencyRate(const CurrencyRate &r) {
    currencyDecimals(r.fromCurrency);
    currencyDecimals(r.targetCurrency);
    if (r.numerator < 1 || r.denominator < 1 || r.numerator > 1000000000 || r.denominator > 1000000000 ||
        !validDate(r.effectiveDate) || !label(r.reference) ||
        (r.rounding != "half-away-from-zero-v1" && r.rounding != "exact-minor-v1") ||
        (r.fromCurrency == r.targetCurrency && (r.numerator != 1 || r.denominator != 1)))
        bad("환율 분자·분모(1~1,000,000,000), 기준일·근거·반올림 정책을 확인하세요.");
}
std::optional<std::int64_t> convertCurrencyMoney(std::optional<std::int64_t> value, const CurrencyRate &r) {
    validateCurrencyRate(r);
    if (!value)
        return {};
    if (*value < -maxMoney || *value > maxMoney)
        bad("원본 금액 범위를 확인하세요.");
    const auto n = std::uint64_t(r.numerator) * (currencyDecimals(r.targetCurrency) == 2 ? 100ULL : 1ULL),
               d = std::uint64_t(r.denominator) * (currencyDecimals(r.fromCurrency) == 2 ? 100ULL : 1ULL);
    const auto a = std::uint64_t(*value < 0 ? -*value : *value), limit = std::uint64_t(maxMoney);
    std::uint64_t q = 0, rem = 0;
    for (int bit = 63; bit >= 0; --bit) {
        if (q > limit / 2)
            bad("환산 금액이 지원 범위를 초과합니다.");
        q *= 2;
        rem *= 2;
        if ((a >> bit) & 1) {
            if (n / d > limit - q)
                bad("환산 금액이 지원 범위를 초과합니다.");
            q += n / d;
            rem += n % d;
        }
        if (rem / d > limit - q)
            bad("환산 금액이 지원 범위를 초과합니다.");
        q += rem / d;
        rem %= d;
    }
    if (r.rounding == "exact-minor-v1" && rem)
        bad("최소 통화 단위로 정확히 환산되지 않는 금액입니다. 반올림 정책을 검토하세요.");
    if (r.rounding == "half-away-from-zero-v1" && rem >= (d + 1) / 2) {
        if (q == limit)
            bad("반올림 금액이 지원 범위를 초과합니다.");
        ++q;
    }
    return *value < 0 ? -std::int64_t(q) : std::int64_t(q);
}
XerFinancialSnapshot convertCurrencyFinancials(const XerFinancialSnapshot &s, const CurrencyRate &r) {
    if (s.currency != r.fromCurrency)
        bad("외부 재무 투영의 원래 통화가 환율 출발 통화와 다릅니다.");
    auto out = s;
    financial(out, "", [&](const auto &, const auto &, auto &v) { v = convertCurrencyMoney(v, r); });
    out.currency = r.targetCurrency;
    validateXerFinancials(out);
    return out;
}
void clearTransitionHistories(Project &p) {
    p.currencyConversions.clear();
    p.ledgerIdMigrations.clear();
}
Project convertProjectCurrency(const Project &source, const CurrencyRate &r) {
    validateCurrencyRate(r);
    if (source.currency != r.fromCurrency || r.fromCurrency == r.targetCurrency)
        bad("현재 통화와 다른 대상 통화를 지정하세요.");
    auto p = source;
    clearTransitionHistories(p);
    all(
        p,
        [&](const std::string &path, const std::string &currency, auto &v) {
            calculationCheckpoint();
            if (currency != r.fromCurrency)
                bad("다른 통화의 금액에는 별도 환율이 필요합니다: " + path);
            v = convertCurrencyMoney(v, r);
        },
        r.targetCurrency);
    auto setPlan = [&](PlanInput &v) { v.currency = r.targetCurrency; };
    setPlan(p);
    for (auto &b : p.baselines)
        setPlan(b.input);
    for (auto &s : p.scenarios)
        setPlan(s.input);
    for (auto &v : p.costEntries)
        v.currency = r.targetCurrency;
    for (auto &v : p.periodActuals)
        v.currency = r.targetCurrency;
    for (auto &v : p.externalEvmAdoptions)
        v.currency = r.targetCurrency;
    for (auto &v : p.financialPeriodEvents)
        v.currency = r.targetCurrency;
    if (p.xerFinancials)
        p.xerFinancials->currency = r.targetCurrency;
    for (auto &revision : p.xerFinancialRevisions) {
        revision.financials.currency = r.targetCurrency;
        if (revision.relink) {
            auto &audit = *revision.relink;
            if (audit.incomingCurrencySteps.size() >= 16)
                bad("외부 원본의 추가 통화 전환 한도는16건입니다.");
            audit.incomingCurrencySteps.push_back(r);
            if (audit.previousConversion) {
                auto &c = *audit.previousConversion;
                if (c.targetCurrency != r.fromCurrency || c.numerator != 1 || c.denominator != 1)
                    bad("이전 원본 환율이 단일 현재 통화와 다릅니다. 원본 연결을 먼저 정리하세요.");
                c.targetCurrency = r.targetCurrency;
            }
        }
    }
    ++p.revision;
    try {
        validate(p);
    } catch (const AppError &e) {
        bad("환산 후 원장·기간·외부 집계 대사가 일치하지 않습니다. 개별 반올림과 원본을 검토하세요. [" + e.code() +
            "]");
    }
    return p;
}
std::vector<CurrencyMoneyRow> currencyMoneyRows(const Project &source) {
    auto p = source;
    std::vector<CurrencyMoneyRow> rows;
    all(p, [&](const auto &path, const auto &currency, auto &v) {
        calculationCheckpoint();
        rows.push_back({path, currency, v});
    });
    return rows;
}
bool CurrencyConversion::operator==(const CurrencyConversion &b) const {
    return static_cast<const CurrencyConversionSubmission &>(*this) ==
               static_cast<const CurrencyConversionSubmission &>(b) &&
           id == b.id && sourceDocumentId == b.sourceDocumentId && recordedRevision == b.recordedRevision &&
           bool(before) == bool(b.before) && bool(after) == bool(b.after) &&
           (!before || before == b.before || *before == *b.before) &&
           (!after || after == b.after || *after == *b.after);
}
void validateCurrencyConversions(const Project &p) {
    if (p.currencyConversions.size() > 16)
        bad("프로젝트 통화 전환 이력은 최대16건입니다.");
    std::set<std::string> ids;
    std::uint64_t revision = 0;
    std::size_t rows = 0;
    const CurrencyConversion *previous = nullptr;
    for (const auto &e : p.currencyConversions) {
        calculationCheckpoint();
        if (!e.before || !e.after || !e.before->currencyConversions.empty() || !e.before->ledgerIdMigrations.empty() ||
            !e.after->currencyConversions.empty() || !e.after->ledgerIdMigrations.empty() || !label(e.id, 1024) ||
            !ids.insert(e.id).second || !label(e.reporter, 1024) || !label(e.reason) || !validUtcStamp(e.recordedAt) ||
            !e.confirmed || e.sourceDocumentId != e.before->documentId ||
            e.recordedRevision != e.before->revision + 1 || e.recordedRevision <= revision ||
            e.recordedRevision > p.revision)
            bad("통화 전환의 확인·출처·시각·revision·전후 원본을 확인하세요.");
        rows += ledgerIdentityRows(*e.before) + ledgerIdentityRows(*e.after);
        if (rows > 300000)
            bad("통화 전환 전후 주요 스냅샷 한도는 합계300,000행입니다.");
        validate(*e.before);
        if (convertProjectCurrency(*e.before, e.rate) != *e.after)
            bad("환율과 저장한 통화 전환 결과가 일치하지 않습니다.");
        if (previous && e.before->revision == previous->recordedRevision)
            currentMatches(*e.before, *previous);
        previous = &e;
        revision = e.recordedRevision;
    }
}
void validateCurrencyCurrent(const Project &p) {
    if (!p.currencyConversions.empty() && p.currencyConversions.back().recordedRevision == p.revision)
        currentMatches(p, p.currencyConversions.back());
}
} // namespace jobs
