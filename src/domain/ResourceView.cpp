#include "domain/ResourceView.h"
#include "domain/FormulaFields.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <unordered_set>
namespace ppm {
std::vector<RollUpGroup> resourceRollUpGroups(const std::vector<ResourceViewRow> &rows) {
    std::vector<RollUpGroup> groups{{"", {}, {}}};
    std::map<std::string, std::size_t> indices{{"", 0}};
    for (const auto &r : rows) {
        std::vector<std::string> path;
        std::string parent;
        for (const auto &g : r.groups) {
            path.push_back(g.token);
            const auto id = rollUpPath(path);
            if (!indices.contains(id)) {
                indices[id] = groups.size();
                groups.push_back({id, parent, {}});
            }
            parent = id;
        }
        groups[indices.at(parent)].rows.push_back(r.resourceIndex);
    }
    return groups;
}

namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
bool numeric(const PlanInput &p, const std::string &key) {
    const auto *f = customField(p, key, FieldScope::Resource);
    return key == "capacity" || (f && numericCustomField(f->type));
}
bool ordered(const PlanInput &p, const std::string &key) {
    const auto *f = customField(p, key, FieldScope::Resource);
    return numeric(p, key) || (f && dateTimeCustomField(f->type));
}
bool known(const PlanInput &p, const std::string &key) {
    const auto keys = resourceViewKeys(p);
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}
bool text(const std::string &s, std::size_t n) {
    return s.size() <= n && std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
int compare(const PlanInput &p, const std::string &key, const std::string &a, const std::string &b) {
    if (numeric(p, key)) {
        const auto *f = customField(p, key, FieldScope::Resource);
        const auto x = f ? customFieldNumber(p, *f, a) : parseFieldNumber(a),
                   y = f ? customFieldNumber(p, *f, b) : parseFieldNumber(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    return a < b ? -1 : a > b ? 1 : 0;
}
} // namespace
std::vector<std::string> resourceViewKeys(const PlanInput &p) {
    std::vector<std::string> keys{"name", "id", "type", "calendar", "capacity", "assigned"};
    const auto fields = attributeKeys(p, FieldScope::Resource);
    keys.insert(keys.end(), fields.begin(), fields.end());
    return keys;
}
std::string resourceViewName(const PlanInput &p, const std::string &key) {
    if (key == "name")
        return "자원";
    if (key == "id")
        return "자원 ID";
    if (key == "type")
        return "종류";
    if (key == "calendar")
        return "달력";
    if (key == "capacity")
        return "기본 가용률 (%)";
    if (key == "assigned")
        return "배정 여부";
    const auto name = attributeName(p, key, FieldScope::Resource), unit = attributeUnit(p, key, FieldScope::Resource);
    return name + (unit.empty() ? "" : " (" + unit + ")");
}
std::optional<std::string> resourceViewValue(const PlanInput &p, const Resource &r, const std::string &key) {
    if (key == "name")
        return r.name;
    if (key == "id")
        return r.id;
    if (key == "type")
        return r.type;
    if (key == "calendar")
        return r.calendarId;
    if (key == "capacity")
        return r.maxUnits ? std::optional{std::to_string(*r.maxUnits)} : std::nullopt;
    if (key == "assigned")
        return std::any_of(p.assignments.begin(), p.assignments.end(),
                           [&](const auto &a) { return a.resourceId == r.id; })
                   ? "yes"
                   : "no";
    return resolvedAttributeValue(p, r, key);
}
std::string resourceViewText(const PlanInput &p, const std::string &key, const std::string &value) {
    if (key == "type")
        return value == "labor" ? "인력" : "장비";
    if (key == "assigned")
        return value == "yes" ? "배정됨" : "미배정";
    if (key == "calendar") {
        if (value.empty())
            return "프로젝트 기본 달력";
        for (const auto &c : p.calendars)
            if (c.id == value)
                return c.name;
    }
    if (key.starts_with("field:"))
        return attributeText(p, key, value, FieldScope::Resource);
    return value;
}
void validateResourceView(const PlanInput &p, const ResourceViewSettings &v) {
    if (!text(v.query, 4096) || v.groupBy.size() > 3 || v.filters.size() > 8 || !known(p, v.sortKey))
        invalid("자원 보기의 검색·정렬·그룹·필터 제한을 확인하세요.");
    std::unordered_set<std::string> groups;
    for (const auto &key : v.groupBy)
        if (!known(p, key) || !groups.insert(key).second)
            invalid("자원 그룹 키가 없거나 중복됩니다.");
    for (const auto &f : v.filters) {
        if (!known(p, f.key) || !text(f.value, 16384))
            invalid("자원 필터 키·값을 확인하세요.");
        const auto &op = f.operation;
        if (op == "missing" || op == "present") {
            if (!f.value.empty())
                invalid("설정 여부 필터에는 값을 입력하지 마세요.");
            continue;
        }
        if (op != "equals" && op != "contains" && op != "lt" && op != "le" && op != "gt" && op != "ge")
            invalid("자원 필터 연산을 확인하세요.");
        if (op != "equals" && op != "contains" && !ordered(p, f.key))
            invalid("크기 비교는 숫자·날짜에만 사용할 수 있습니다.");
        if (op == "contains")
            continue;
        if (f.key.starts_with("field:")) {
            if (normalizeAttributeValue(p, f.key, f.value, FieldScope::Resource, true) != f.value)
                invalid("정규 형식의 필터 값을 입력하세요.");
        } else if (f.key == "capacity") {
            if (formatFieldNumber(parseFieldNumber(f.value)) != f.value)
                invalid("가용률 숫자 형식을 확인하세요.");
        } else if (f.key == "type" && f.value != "labor" && f.value != "equipment")
            invalid("종류는 labor/equipment입니다.");
        else if (f.key == "assigned" && f.value != "yes" && f.value != "no")
            invalid("배정 여부는 yes/no입니다.");
    }
}
void validateSavedResourceViews(const Project &p) {
    if (p.savedResourceViews.size() > 64)
        invalid("저장된 자원 보기는 최대 64개입니다.");
    std::unordered_set<std::string> ids, names;
    for (const auto &v : p.savedResourceViews) {
        calculationCheckpoint();
        if (v.id.empty() || v.name.empty() || !text(v.id, 1024) || !text(v.name, 1024) ||
            v.name.find_first_not_of(' ') == std::string::npos || !ids.insert(v.id).second ||
            !names.insert(v.name).second)
            invalid("자원 보기 ID·이름·중복을 확인하세요.");
        validateResourceView(p, v.settings);
    }
}
std::vector<ResourceViewRow> projectResources(const Project &p, const ResourceViewSettings &v,
                                              std::shared_ptr<CalculationControl> control) {
    EvaluationScope scope(std::move(control));
    validateResourceView(p, v);
    struct Candidate {
        ResourceViewRow row;
        std::optional<std::string> sort;
        std::vector<std::optional<std::string>> groups;
    };
    std::vector<Candidate> candidates;
    // Assignment membership is indexed once; large unassigned lists do not scan all assignments per row.
    std::unordered_set<std::string> assigned;
    for (const auto &a : p.assignments)
        assigned.insert(a.resourceId);
    const bool derived =
        std::any_of(p.resourceFields.begin(), p.resourceFields.end(), [](const auto &f) { return calculatedField(f); });
    for (std::size_t i = 0; i < p.resources.size(); ++i) {
        calculationCheckpoint();
        const auto &r = p.resources[i];
        const auto computed =
            derived ? formulaValues(p, FieldScope::Resource, nullptr, &r) : std::shared_ptr<const FormulaValues>{};
        auto value = [&](const std::string &key) -> std::optional<std::string> {
            if (key == "assigned")
                return assigned.contains(r.id) ? "yes" : "no";
            if (computed) {
                const auto it = computed->find(key);
                if (it != computed->end())
                    return formulaRawValue(it->second);
            }
            return resourceViewValue(p, r, key);
        };
        if (!v.query.empty() && r.name.find(v.query) == std::string::npos && r.id.find(v.query) == std::string::npos)
            continue;
        bool match = true;
        for (const auto &f : v.filters) {
            const auto x = value(f.key);
            const auto &op = f.operation;
            const bool ok = op == "missing"    ? !x
                            : op == "present"  ? bool(x)
                            : !x               ? false
                            : op == "contains" ? resourceViewText(p, f.key, *x).find(f.value) != std::string::npos
                            : op == "equals"   ? *x == f.value
                            : op == "lt"       ? compare(p, f.key, *x, f.value) < 0
                            : op == "le"       ? compare(p, f.key, *x, f.value) <= 0
                            : op == "gt"       ? compare(p, f.key, *x, f.value) > 0
                                               : compare(p, f.key, *x, f.value) >= 0;
            if (!ok) {
                match = false;
                break;
            }
        }
        if (!match)
            continue;
        Candidate c;
        c.row.resourceIndex = i;
        c.sort = value(v.sortKey);
        for (const auto &key : v.groupBy) {
            auto x = value(key);
            c.groups.push_back(x);
            const auto *f = customField(p, key, FieldScope::Resource);
            c.row.groups.push_back({x ? "v" + *x : "m", x                          ? resourceViewText(p, key, *x)
                                                        : f && calculatedField(*f) ? "(계산값 없음)"
                                                                                   : "(미설정)"});
        }
        candidates.push_back(std::move(c));
    }
    auto order = [&](const auto &x, const auto &y, const std::string &key, bool descending) {
        if (bool(x) != bool(y))
            return x ? -1 : 1;
        return x ? (descending ? -compare(p, key, *x, *y) : compare(p, key, *x, *y)) : 0;
    };
    std::sort(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b) {
        calculationCheckpoint();
        for (std::size_t i = 0; i < v.groupBy.size(); ++i)
            if (const auto c = order(a.groups[i], b.groups[i], v.groupBy[i], false))
                return c < 0;
        if (const auto c = order(a.sort, b.sort, v.sortKey, v.descending))
            return c < 0;
        return p.resources[a.row.resourceIndex].id < p.resources[b.row.resourceIndex].id;
    });
    std::vector<ResourceViewRow> result;
    result.reserve(candidates.size());
    for (auto &c : candidates)
        result.push_back(std::move(c.row));
    return result;
}
} // namespace ppm
