#include "domain/XmlSchedule.h"
#include "domain/XmlSummaryDuration.h"
#include "domain/XmlFormula.h"
#include "domain/XmlRollUp.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
// Two unsigned 128-bit magnitudes avoid order-dependent overflow and floating-point sums.
struct Magnitude {
    std::array<std::uint32_t, 4> digits{};
    void add(const Magnitude &b) {
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            auto n = std::uint64_t(digits[i]) + b.digits[i] + carry;
            digits[i] = std::uint32_t(n);
            carry = n >> 32;
        }
        if (carry)
            invalid("요약 합계 범위 초과");
    }
    static Magnitude from(std::uint64_t n) {
        Magnitude a;
        a.digits[0] = std::uint32_t(n);
        a.digits[1] = std::uint32_t(n >> 32);
        return a;
    }
    bool less(const Magnitude &b) const {
        for (int i = 3; i >= 0; --i)
            if (digits[std::size_t(i)] != b.digits[std::size_t(i)])
                return digits[std::size_t(i)] < b.digits[std::size_t(i)];
        return false;
    }
    Magnitude minus(const Magnitude &b) const {
        Magnitude out;
        std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            auto sub = std::uint64_t(b.digits[i]) + borrow;
            out.digits[i] = std::uint32_t(std::uint64_t(digits[i]) - sub);
            borrow = std::uint64_t(digits[i]) < sub;
        }
        return out;
    }
};
struct Wide {
    Magnitude positive, negative;
    void add(std::int64_t n) {
        if (n < 0)
            negative.add(Magnitude::from(std::uint64_t(-(n + 1)) + 1));
        else
            positive.add(Magnitude::from(std::uint64_t(n)));
    }
    void add(const Wide &b) {
        positive.add(b.positive);
        negative.add(b.negative);
    }
    std::int64_t divided(std::size_t count) const {
        if (!count)
            invalid("평균의 입력이 없습니다.");
        const bool neg = positive.less(negative);
        auto n = neg ? negative.minus(positive) : positive.minus(negative);
        std::uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            const auto at = std::size_t(i);
            const auto v = (rem << 32) | n.digits[at];
            n.digits[at] = std::uint32_t(v / count);
            rem = v % count;
        }
        if (n.digits[2] || n.digits[3])
            invalid("요약 수치 범위 초과");
        auto q = (std::uint64_t(n.digits[1]) << 32) | n.digits[0];
        if (rem * 2 >= count) {
            if (q == std::numeric_limits<std::uint64_t>::max())
                invalid("요약 수치 범위 초과");
            ++q;
        }
        if (q > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
            invalid("요약 수치 범위 초과");
        return neg ? -std::int64_t(q) : std::int64_t(q);
    }
};
struct Stats {
    std::size_t leaves = 0, groups = 0, missing = 0, known = 0;
    Wide sum;
    std::optional<std::int64_t> low, high;
    std::optional<std::string> first, last;
    bool duration = false, elapsed = false, mixed = false, estimated = false;
    void add(const Stats &b) {
        leaves += b.leaves;
        groups += b.groups;
        missing += b.missing;
        known += b.known;
        sum.add(b.sum);
        if (b.low)
            low = low ? std::min(*low, *b.low) : b.low;
        if (b.high)
            high = high ? std::max(*high, *b.high) : b.high;
        if (b.first)
            first = first ? std::min(*first, *b.first) : b.first;
        if (b.last)
            last = last ? std::max(*last, *b.last) : b.last;
        if (b.duration) {
            if (duration && elapsed != b.elapsed)
                mixed = true;
            elapsed = b.elapsed;
            duration = true;
        }
        mixed = mixed || b.mixed;
        estimated = estimated || b.estimated;
    }
};
Stats scalar(const PlanInput &p, const CustomField &f, const std::optional<std::string> &v) {
    Stats out;
    out.leaves = 1;
    if (!v) {
        out.missing = 1;
        return out;
    }
    out.known = 1;
    if (dateTimeCustomField(f.type)) {
        auto stamp = normalizeFieldDateTime(v->size() == 10 ? *v + "T00:00:00" : *v);
        out.first = out.last = stamp;
        return out;
    }
    std::int64_t number = 0;
    if (f.type == CustomFieldType::Flag)
        number = *v == "1" ? 1 : 0;
    else if (f.type == CustomFieldType::Cost)
        number = parseFieldCost(*v, p.currency);
    else if (f.type == CustomFieldType::Duration) {
        auto d = parseFieldDuration(*v);
        number = d.minutes;
        out.duration = true;
        out.elapsed = d.elapsed;
        out.estimated = d.estimated;
    } else
        number = parseFieldNumber(*v);
    out.sum.add(number);
    out.low = out.high = number;
    return out;
}
std::string formatted(const PlanInput &p, const CustomField &f, std::int64_t n, const Stats &s) {
    std::string raw = f.type == CustomFieldType::Cost       ? formatFieldCost(n, p.currency)
                      : f.type == CustomFieldType::Duration ? formatFieldDuration({n, s.elapsed, s.estimated})
                                                            : formatFieldNumber(n);
    return normalizeLookupScalar(f.type, raw, p.currency);
}
RollUpCell result(const PlanInput &p, const CustomField &f, const Stats &all, const Stats &direct,
                  std::size_t directRows) {
    RollUpCell cell;
    cell.leaves = all.leaves;
    cell.missing = all.missing;
    cell.origin = "자동 roll-up";
    const auto method = f.xmlRollUp->rollupType;
    if (method == 2 || method == 6 || method == 7) {
        auto count = method == 2 ? all.leaves + all.groups : method == 6 ? directRows : all.leaves;
        cell.value = std::to_string(count);
        return cell;
    }
    if (!all.leaves)
        return cell;
    const auto &s = method == 5 ? direct : all;
    if (f.type == CustomFieldType::Flag) {
        if (method == 0 && s.high && *s.high == 1) {
            cell.value = "1";
            return cell;
        }
        if (method == 1 && s.low && *s.low == 0) {
            cell.value = "0";
            return cell;
        }
        if (s.missing) {
            cell.issue = "하위 값 미설정";
            return cell;
        }
        if (s.known)
            cell.value = method == 0 ? "0" : "1";
        return cell;
    }
    if (s.missing) {
        cell.issue = "하위 값 미설정 또는 계산 오류";
        return cell;
    }
    if (s.mixed) {
        cell.issue = "근무 기간과 경과 기간을 함께 집계할 수 없습니다.";
        return cell;
    }
    if (dateTimeCustomField(f.type)) {
        if (method == 0 && s.last)
            cell.value = normalizeLookupScalar(f.type, *s.last);
        if (method == 1 && s.first)
            cell.value = normalizeLookupScalar(f.type, *s.first);
        return cell;
    }
    try {
        auto n = method == 0 ? *s.high : method == 1 ? *s.low : s.sum.divided(method == 4 || method == 5 ? s.known : 1);
        cell.value = formatted(p, f, n, s);
    } catch (const AppError &) {
        cell.issue = "요약 수치·정밀도 범위 초과";
    }
    return cell;
}
void rule(const CustomField &f, FieldScope scope) {
    if (!f.xmlRollUp)
        return;
    const auto &r = *f.xmlRollUp;
    if (r.calculationType < 0 || r.calculationType > 2 || r.rollupType < 0 || r.rollupType > 7)
        invalid("XML 요약 계산/roll-up 형식 범위를 확인하세요.");
    if (r.calculationType == 1 && (scope == FieldScope::Task || scope == FieldScope::Resource) &&
        !rollUpSupported(f.type, r.rollupType))
        invalid("이 필드 형식에서 사용할 수 없는 요약 roll-up입니다.");
}
} // namespace
bool rollUpSupported(CustomFieldType type, int method) {
    if (method < 0 || method > 7)
        return false;
    if (type == CustomFieldType::Number)
        return true;
    if (type == CustomFieldType::Cost || type == CustomFieldType::Duration)
        return method == 0 || method == 1 || method == 3 || method == 4 || method == 5;
    if (type == CustomFieldType::Flag || dateTimeCustomField(type))
        return method == 0 || method == 1;
    return false;
}
std::string rollUpMethodName(int m) {
    static const std::array<std::string, 8> names = {"최대 / OR",      "최소 / AND",       "전체 행 개수",
                                                     "합계",           "평균 (말단 전체)", "평균 (직접 하위)",
                                                     "직접 하위 개수", "말단 개수"};
    if (m < 0 || m > 7)
        invalid("요약 방식 범위 오류");
    return names[std::size_t(m)];
}
std::string rollUpPath(const std::vector<std::string> &parts) {
    std::string out;
    for (const auto &p : parts)
        out += std::to_string(p.size()) + ":" + p;
    return out;
}
bool hasXmlRollUp(const PlanInput &p) {
    if (!p.taskSummaryAttributes.empty() ||
        std::any_of(p.wbsNodes.begin(), p.wbsNodes.end(), [](const auto &n) { return !n.attributes.empty(); }))
        return true;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlRollUp)
                return true;
    return false;
}
void validateXmlRollUp(const PlanInput &p) {
    if (!hasXmlRollUp(p))
        return;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            rule(f, s);
    auto attrs = [&](const auto &values) {
        if (values.size() > 64)
            invalid("요약 필드 값은 객체별64개까지입니다.");
        std::set<std::string> keys;
        for (const auto &v : values) {
            calculationCheckpoint();
            if (!v.key.starts_with("field:") || !keys.insert(v.key).second ||
                normalizeAttributeValue(p, v.key, v.value, FieldScope::Task, true) != v.value)
                invalid("요약 값의 필드·형식·중복을 확인하세요.");
        }
    };
    attrs(p.taskSummaryAttributes);
    for (const auto &g : p.wbsNodes)
        attrs(g.attributes);
    std::size_t fields = 0;
    for (const auto &f : p.customFields)
        if (f.xmlRollUp && f.xmlRollUp->calculationType == 1)
            ++fields;
    if ((p.wbsNodes.size() + 1) * fields > 100000)
        invalid("자동 요약은 최대100,000셀입니다.");
}
RollUpValues::RollUpValues(const PlanInput &p, FieldScope scope) : p_(p), scope_(scope) {
    if (!hasXmlRollUp(p))
        return;
    std::vector<RollUpGroup> groups{{"", {}, {}}};
    std::map<std::string, std::size_t> indices{{"", 0}};
    if (scope == FieldScope::Task) {
        for (const auto &g : p.wbsNodes) {
            indices.emplace(g.id, groups.size());
            groups.push_back({g.id, g.parentId, {}});
        }
        for (std::size_t i = 0; i < p.tasks.size(); ++i) {
            if (!indices.contains(p.tasks[i].wbsId))
                invalid("요약 WBS 부모가 없습니다.");
            groups[indices.at(p.tasks[i].wbsId)].rows.push_back(i);
        }
    } else if (scope == FieldScope::Resource) {
        for (std::size_t i = 0; i < p.resources.size(); ++i)
            groups[0].rows.push_back(i);
    } else
        invalid("작업·자원 요약 범위를 선택하세요.");
    build(std::move(groups), true);
}
RollUpValues::RollUpValues(const PlanInput &p, FieldScope scope, std::vector<RollUpGroup> groups)
    : p_(p), scope_(scope) {
    build(std::move(groups), false);
}
void RollUpValues::build(std::vector<RollUpGroup> groups, bool snapshots) {
    EvaluationScope evaluation;
    if (scope_ != FieldScope::Task && scope_ != FieldScope::Resource)
        invalid("작업·자원 요약 범위를 선택하세요.");
    std::set<std::string> stored;
    if (snapshots && scope_ == FieldScope::Task) {
        for (const auto &v : p_.taskSummaryAttributes)
            stored.insert(v.key);
        for (const auto &g : p_.wbsNodes)
            for (const auto &v : g.attributes)
                stored.insert(v.key);
    }
    for (const auto &f : customFields(p_, scope_)) {
        rule(f, scope_);
        if ((f.xmlRollUp && (f.xmlRollUp->calculationType == 1 ||
                             (f.xmlRollUp->calculationType == 2 && f.xmlFormula && f.xmlFormula->enabled))) ||
            stored.contains("field:" + f.id))
            fields_.push_back(&f);
    }
    if (fields_.empty())
        return;
    if (std::none_of(groups.begin(), groups.end(), [](const auto &g) { return g.id.empty(); }))
        groups.insert(groups.begin(), {"", {}, {}});
    if (groups.size() > 30001 || groups.size() * fields_.size() > 100000)
        invalid("요약 조회는 최대100,000셀입니다.");
    std::map<std::string, const RollUpGroup *> nodes;
    std::map<std::string, std::vector<std::string>> children;
    std::set<std::size_t> assigned;
    for (const auto &g : groups) {
        if (!nodes.emplace(g.id, &g).second)
            invalid("중복 요약 그룹 ID");
        for (auto row : g.rows) {
            if (row >= (scope_ == FieldScope::Task ? p_.tasks.size() : p_.resources.size()) ||
                !assigned.insert(row).second)
                invalid("요약 행 범위·중복 오류");
        }
    }
    for (const auto &g : groups)
        if (!g.id.empty()) {
            if (!nodes.contains(g.parentId))
                invalid("요약 그룹 부모 누락");
            children[g.parentId].push_back(g.id);
        }
    std::map<std::string, int> color;
    std::vector<std::string> order;
    std::function<void(const std::string &, int)> visit = [&](const std::string &id, int depth) {
        calculationCheckpoint();
        if (depth > 64 || color[id] == 1)
            invalid("요약 그룹 순환·깊이 초과");
        if (color[id] == 2)
            return;
        color[id] = 1;
        for (const auto &child : children[id])
            visit(child, depth + 1);
        color[id] = 2;
        order.push_back(id);
    };
    visit("", 0);
    if (order.size() != nodes.size())
        invalid("연결되지 않은 요약 그룹");
    std::map<std::string, const std::vector<TaskAttribute> *> raw;
    if (snapshots && scope_ == FieldScope::Task) {
        raw[""] = &p_.taskSummaryAttributes;
        for (const auto &g : p_.wbsNodes)
            raw[g.id] = &g.attributes;
    }
    for (const auto *f : fields_) {
        const auto key = "field:" + f->id;
        std::map<std::string, Stats> totals;
        for (const auto &id : order) {
            calculationCheckpoint();
            RollUpCell cell;
            if (!f->xmlRollUp || f->xmlRollUp->calculationType != 1) {
                if (raw.contains(id))
                    cell.value = attributeValue(*raw.at(id), key);
                cell.origin = f->xmlRollUp && f->xmlRollUp->calculationType == 2 ? "명시 요약 값 · 외부 계산식 미실행"
                                                                                 : "명시 요약 값";
                cells_[id][key] = std::move(cell);
                continue;
            }
            Stats all, direct;
            std::size_t directRows = nodes.at(id)->rows.size() + children[id].size();
            for (auto row : nodes.at(id)->rows) {
                calculationCheckpoint();
                const auto value = scope_ == FieldScope::Task ? resolvedAttributeValue(p_, p_.tasks[row], key)
                                                              : resolvedAttributeValue(p_, p_.resources[row], key);
                auto one = scalar(p_, *f, value);
                all.add(one);
                direct.add(one);
            }
            for (const auto &child : children[id]) {
                const auto &stat = totals.at(child);
                all.add(stat);
                ++all.groups;
                if (stat.leaves) {
                    const auto &c = cells_.at(child).at(key);
                    auto one = scalar(p_, *f, c.value);
                    if (!c.issue.empty() && !one.missing)
                        one.missing = 1;
                    direct.add(one);
                }
            }
            totals.emplace(id, all);
            cell = result(p_, *f, all, direct, directRows);
            cells_[id][key] = std::move(cell);
        }
    }
    struct DateRange {
        XmlFormulaResult value;
        std::size_t rows = 0;
    };
    std::map<std::string, std::vector<std::string>> durationRows;
    std::function<const std::vector<std::string> &(const std::string &)> collect =
        [&](const std::string &id) -> const std::vector<std::string> & {
        if (durationRows.contains(id))
            return durationRows.at(id);
        auto &rows = durationRows[id];
        for (auto row : nodes.at(id)->rows) {
            calculationCheckpoint();
            rows.push_back(p_.tasks[row].id);
        }
        for (const auto &child : children[id]) {
            const auto &nested = collect(child);
            rows.insert(rows.end(), nested.begin(), nested.end());
        }
        return rows;
    };
    std::map<std::pair<std::string, std::string>, DateRange> ranges;
    std::function<DateRange(const std::string &, const std::string &)> range =
        [&](const std::string &id, const std::string &key) -> DateRange {
        const auto token = std::pair{id, key};
        if (ranges.contains(token))
            return ranges.at(token);
        DateRange out;
        const auto baseKey = xmlBaselineBaseKey(key);
        if (xmlDurationReference(key)) {
            if (scope_ != FieldScope::Task)
                out.value = {{}, "자원 자체의 요약 기간은 지원하지 않습니다.", true};
            else {
                const auto &rows = collect(id);
                out.rows = rows.size();
                out.value = xmlSummaryDuration(p_, key, rows);
            }
            ranges.emplace(token, out);
            return out;
        }
        const bool cost = baseKey == "builtin:baselinecost";
        const bool work =
            key == "builtin:actualwork" || key == "builtin:remainingwork" || baseKey == "builtin:baselinework";
        std::int64_t workTotal = 0;
        bool missing = false, fatal = false;
        std::string reason;
        const bool actualStart = key == "builtin:actualstart",
                   minimum = actualStart || key == "builtin:start" || baseKey == "builtin:baselinestart" ||
                             key == "builtin:earlystart" || key == "builtin:latestart";
        auto add = [&](const XmlFormulaResult &v, std::size_t rows) {
            if (!rows)
                return;
            out.rows += rows;
            if (!v.value) {
                missing = true;
                if (reason.empty())
                    reason = v.issue;
                if (!v.missing)
                    fatal = true;
                return;
            }
            if (cost) {
                const auto n = parseFieldCost(*v.value, p_.currency);
                if (n < 0 || n > maxMoney || workTotal > maxMoney - n) {
                    fatal = true;
                    reason = "요약 기준선 원가의 합계 범위를 초과했습니다.";
                    return;
                }
                workTotal += n;
                out.value.value = formatFieldCost(workTotal, p_.currency);
            } else if (work) {
                const auto n = parseFieldNumber(*v.value);
                if (n < 0 || n > maxWork * 10000 || workTotal > maxWork * 10000 - n) {
                    fatal = true;
                    reason = "요약 공수의 합계 범위를 초과했습니다.";
                    return;
                }
                workTotal += n;
                out.value.value = formatFieldNumber(workTotal);
            } else if (!out.value.value || (minimum ? *v.value < *out.value.value : *v.value > *out.value.value))
                out.value.value = v.value;
        };
        for (auto row : nodes.at(id)->rows) {
            if (xmlEarlyLateReference(key) && scope_ == FieldScope::Task)
                if (auto values = XmlScheduleScope::current(p_);
                    values && values->earlyLateCompleted.contains(p_.tasks[row].id))
                    continue;
            if (scope_ == FieldScope::Task)
                add(xmlScheduleReference(p_, FieldScope::Task, key, &p_.tasks[row]), 1);
            else if ((work || cost) && scope_ == FieldScope::Resource)
                add(xmlScheduleReference(p_, FieldScope::Resource, key, nullptr, &p_.resources[row]), 1);
        }
        for (const auto &child : children[id]) {
            auto v = range(child, key);
            add(v.value, v.rows);
        }
        if (fatal || (missing && !actualStart && key != "builtin:actualwork") || !out.value.value) {
            out.value.value.reset();
            out.value.issue = reason;
            out.value.missing = !fatal;
        }
        ranges.emplace(token, out);
        return out;
    };
    for (const auto &id : order) {
        std::set<std::string> completed, active;
        std::function<XmlFormulaResult(const std::string &)> resolve = [&](const std::string &key) -> XmlFormulaResult {
            if (xmlComputedReference(key) || xmlActualReference(key) || xmlBaselineReference(key))
                return range(id, key).value;
            const auto *f = customField(p_, key, scope_);
            const bool formula =
                f && f->xmlRollUp && f->xmlRollUp->calculationType == 2 && f->xmlFormula && f->xmlFormula->enabled;
            if (formula && !completed.contains(key)) {
                if (!active.insert(key).second)
                    return {{}, "요약 식 참조 순환", false};
                auto v = xmlFormulaValue(p_, scope_, key, nullptr, nullptr, nullptr, resolve);
                active.erase(key);
                completed.insert(key);
                cells_[id][key] = {v.value, v.issue, "XML 요약 계산식", v.missing ? 1u : 0u, 0};
            }
            const auto &v = cell(id, key);
            return {v.value, v.issue, !v.value && (v.issue.empty() || v.missing > 0)};
        };
        for (const auto *f : fields_)
            if (f->xmlRollUp && f->xmlRollUp->calculationType == 2 && f->xmlFormula && f->xmlFormula->enabled)
                (void)resolve("field:" + f->id);
    }
}
const RollUpCell &RollUpValues::cell(const std::string &groupId, const std::string &key) const {
    static const RollUpCell empty{{}, {}, "미설정", 0, 0};
    auto g = cells_.find(groupId);
    if (g == cells_.end())
        return empty;
    auto v = g->second.find(key);
    return v == g->second.end() ? empty : v->second;
}
std::string RollUpValues::describe(const std::string &id) const {
    std::string out;
    for (const auto *f : fields_) {
        const auto &c = cell(id, "field:" + f->id);
        if (!out.empty())
            out += '\n';
        out += f->name + ": " +
               (c.value           ? attributeText(p_, "field:" + f->id, *c.value, scope_)
                : c.issue.empty() ? "미설정"
                                  : c.issue);
        const auto method = f->xmlRollUp && f->xmlRollUp->calculationType == 1 ? f->xmlRollUp->rollupType : -1;
        const auto unit =
            method == 2 || method == 6 || method == 7 ? "개" : attributeUnit(p_, "field:" + f->id, scope_);
        if (c.value && !unit.empty())
            out += " " + unit;
        out += " [" + c.origin + "]";
    }
    return out;
}
} // namespace jobs
