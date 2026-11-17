#include "domain/XmlCodes.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <map>
#include <set>
#include <functional>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
std::vector<std::size_t> characters(const std::string &s) {
    std::vector<std::size_t> positions;
    for (std::size_t i = 0; i < s.size();) {
        positions.push_back(i);
        unsigned char c = s[i++];
        unsigned cp = c;
        int following = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            cp = c & 31;
            following = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            cp = c & 15;
            following = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            cp = c & 7;
            following = 3;
        } else if (c >= 128)
            invalid("코드의 UTF-8 문자를 확인하세요.");
        for (int j = 0; j < following; ++j) {
            if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80)
                invalid("코드의 UTF-8 문자를 확인하세요.");
            cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        }
        if (cp < 32 || (cp >= 127 && cp <= 159) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (following == 1 && cp < 128) || (following == 2 && cp < 2048) || (following == 3 && cp < 65536))
            invalid("코드의 제어 문자·인코딩을 확인하세요.");
    }
    positions.push_back(s.size());
    return positions;
}
void segment(const XmlCodeMask &m, const std::string &s) {
    auto count = characters(s).size() - 1;
    if (!count || count > 255 || (m.length && count != std::size_t(m.length)) ||
        (!m.separator.empty() && s.find(m.separator) != std::string::npos))
        invalid("코드의 길이·구분자가 마스크와 다릅니다.");
    if (m.type != 3 && std::any_of(s.begin(), s.end(), [&](unsigned char c) {
            return m.type == 0 ? c < '0' || c > '9' : m.type == 1 ? c < 'A' || c > 'Z' : c < 'a' || c > 'z';
        }))
        invalid("코드는 숫자·영문 대문자·영문 소문자 마스크를 따라야 합니다.");
}
std::vector<std::string> split(const std::vector<XmlCodeMask> &masks, const std::string &raw) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < masks.size() && begin < raw.size(); ++i) {
        const auto &m = masks[i];
        auto end = raw.size();
        if (i + 1 < masks.size()) {
            if (!m.separator.empty())
                end = raw.find(m.separator, begin);
            else if (m.length) {
                auto positions = characters(raw.substr(begin));
                if (positions.size() > std::size_t(m.length))
                    end = begin + positions[std::size_t(m.length)];
            }
        }
        if (end == std::string::npos)
            end = raw.size();
        auto value = raw.substr(begin, end - begin);
        segment(m, value);
        parts.push_back(value);
        if (end == raw.size()) {
            begin = end;
            break;
        }
        begin = end + m.separator.size();
        if (begin == raw.size())
            invalid("코드의 마지막 수준이 비어 있습니다.");
    }
    if (parts.empty() || begin != raw.size() || characters(raw).size() - 1 > 255)
        invalid("코드의 수준·전체 길이를 확인하세요.");
    return parts;
}
std::vector<const XmlLookupValue *> path(const XmlLookup &l, const std::string &id) {
    std::vector<const XmlLookupValue *> parts;
    std::set<std::string> seen;
    auto current = id;
    while (!current.empty()) {
        if (!seen.insert(current).second || parts.size() >= 16)
            invalid("코드 계층의 순환·깊이를 확인하세요.");
        auto found =
            std::find_if(l.values.begin(), l.values.end(), [&](const auto &v) { return v.optionId == current; });
        if (found == l.values.end())
            invalid("코드의 부모 항목이 없습니다.");
        parts.push_back(&*found);
        current = found->parentId;
    }
    std::reverse(parts.begin(), parts.end());
    return parts;
}
std::string next(const XmlCodeMask &mask, const std::set<std::string> &occupied) {
    if (mask.type == 3)
        invalid("문자열 마스크는 자동 순번이 없습니다. 코드를 직접 입력하세요.");
    const char first = mask.type == 0 ? '0' : mask.type == 1 ? 'A' : 'a';
    const char last = mask.type == 0 ? '9' : mask.type == 1 ? 'Z' : 'z';
    std::string candidate = mask.type == 0 ? "1" : std::string(1, first);
    if (mask.length)
        candidate.insert(0, std::size_t(mask.length) - candidate.size(), first);
    if (!occupied.empty()) {
        const auto largest = *std::max_element(occupied.begin(), occupied.end(), [](const auto &a, const auto &b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
        candidate = largest;
        auto i = candidate.size();
        while (i && candidate[i - 1] == last)
            candidate[--i] = first;
        if (i)
            ++candidate[i - 1];
        else
            candidate.insert(candidate.begin(), mask.type == 0 ? '1' : first);
    }
    segment(mask, candidate);
    return candidate;
}
bool owner(const PlanInput &p, const XmlWbsCode &v) {
    if (v.kind == "task")
        return std::any_of(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == v.id; });
    if (v.kind == "wbs")
        return std::any_of(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &t) { return t.id == v.id; });
    return false;
}
} // namespace
void validateXmlMasks(const std::vector<XmlCodeMask> &masks) {
    if (masks.size() > 16)
        invalid("코드 마스크는 최대 16수준입니다.");
    for (std::size_t i = 0; i < masks.size(); ++i) {
        const auto &m = masks[i];
        if (m.level != int(i + 1) || m.type < 0 || m.type > 3 || m.length < 0 || m.length > 255 ||
            characters(m.separator).size() > 2 || (i + 1 < masks.size() && m.separator.empty() && !m.length))
            invalid("마스크의 연속 수준·형식·길이(0~255)·구분자(한 문자)를 확인하세요.");
    }
}
std::string xmlLookupCode(const XmlLookup &l, const std::string &id) {
    auto parts = path(l, id);
    std::string code;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            code += l.masks.empty() ? " › " : l.masks.at(i - 1).separator;
        code += parts[i]->value;
    }
    return code;
}
void validateXmlLookupMask(const XmlLookup &l) {
    validateXmlMasks(l.masks);
    if (l.masks.empty()) {
        if (l.allLevelsRequired)
            invalid("모든 수준 필수에는 마스크가 필요합니다.");
        return;
    }
    if (l.tableGuid.empty() || l.valueType != CustomFieldType::Text)
        invalid("코드 마스크는 GUID 텍스트 목록에 적용합니다.");
    std::set<std::pair<std::string, std::string>> siblings;
    for (const auto &v : l.values) {
        calculationCheckpoint();
        if (!v.listed) {
            auto parts = split(l.masks, v.value);
            if (l.allLevelsRequired && parts.size() != l.masks.size())
                invalid("코드의 모든 수준이 필요합니다.");
            continue;
        }
        auto parts = path(l, v.optionId);
        if (parts.size() > l.masks.size())
            invalid("목록 계층이 마스크 수준을 초과합니다.");
        segment(l.masks[parts.size() - 1], v.value);
        if (!siblings.emplace(v.parentId, v.value).second)
            invalid("같은 부모의 코드가 중복됩니다.");
        (void)split(l.masks, xmlLookupCode(l, v.optionId));
    }
}
void validateXmlCodeSelection(const XmlLookup &l, const std::string &id) {
    if (!l.allLevelsRequired)
        return;
    auto parts = path(l, id);
    auto depth = parts.size();
    if (parts.size() == 1 && !parts.front()->listed)
        depth = split(l.masks, parts.front()->value).size();
    if (depth != l.masks.size())
        invalid("이 코드는 마스크의 모든 수준을 선택해야 합니다.");
}
void rebuildXmlLookupOptions(CustomField &field) {
    field.options.clear();
    std::set<std::string> labels;
    for (const auto &v : field.xmlLookup->values) {
        auto label = xmlLookupCode(*field.xmlLookup, v.optionId);
        if (!labels.insert(label).second) {
            label += " [" + v.optionId + "]";
            if (!labels.insert(label).second)
                invalid("코드 표시 이름이 중복됩니다.");
        }
        field.options.push_back({v.optionId, label});
    }
}
std::string nextXmlLookupCode(const XmlLookup &l, const std::string &parent) {
    validateXmlLookupMask(l);
    const auto depth = path(l, parent).size();
    if (depth >= l.masks.size())
        invalid("생성할 다음 마스크 수준이 없습니다.");
    std::set<std::string> used;
    for (const auto &v : l.values)
        if (v.listed && v.parentId == parent)
            used.insert(v.value);
    return next(l.masks[depth], used);
}
void setXmlLookupMasks(PlanInput &p, FieldScope scope, const std::string &key, const std::vector<XmlCodeMask> &masks,
                       bool required) {
    const auto *found = customField(p, key, scope);
    if (!found || !found->xmlLookup || found->xmlLookup->tableGuid.empty())
        invalid("GUID 코드 목록을 선택하세요.");
    auto l = *found->xmlLookup;
    l.masks = masks;
    l.allLevelsRequired = required;
    validateXmlLookupMask(l);
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(p, s))
            if (f.xmlLookup && f.xmlLookup->tableGuid == l.tableGuid) {
                f.xmlLookup->masks = masks;
                f.xmlLookup->allLevelsRequired = required;
                rebuildXmlLookupOptions(f);
            }
}
std::string appendXmlLookupCode(PlanInput &p, FieldScope scope, const std::string &key, const std::string &parent,
                                const std::optional<std::string> &value, const std::string &description,
                                const std::string &guid) {
    const auto *f = customField(p, key, scope);
    if (!f || !f->xmlLookup)
        invalid("마스크가 있는 XML 목록을 선택하세요.");
    auto l = *f->xmlLookup;
    validateXmlLookupMask(l);
    auto parts = path(l, parent);
    if (l.masks.empty() || parts.size() >= l.masks.size() || l.values.size() >= 1000 ||
        (!parts.empty() && !parts.back()->listed))
        invalid("코드 생성의 부모·수준·항목 한도를 확인하세요.");
    auto code = value.value_or("");
    if (!value)
        code = nextXmlLookupCode(l, parent);
    segment(l.masks[parts.size()], code);
    int maximum = 0;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &field : customFields(p, s))
            if (field.xmlLookup)
                for (const auto &v : field.xmlLookup->values)
                    if (!v.valueId.empty())
                        maximum = std::max(maximum, std::stoi(v.valueId));
    if (maximum == 2147483647)
        invalid("XML 값 ID 공간이 소진되었습니다.");
    XmlLookupValue added{"xml-choice-" + guid, std::to_string(maximum + 1), guid, code, description, parent, true};
    l.values.push_back(added);
    validateXmlLookupMask(l);
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &field : customFields(p, s))
            if (field.xmlLookup && field.xmlLookup->tableGuid == l.tableGuid) {
                field.xmlLookup->values.push_back(added);
                rebuildXmlLookupOptions(field);
            }
    return added.optionId;
}
bool hasXmlCodeMasks(const PlanInput &p) {
    if (p.xmlWbs)
        return true;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlLookup && (!f.xmlLookup->masks.empty() || f.xmlLookup->allLevelsRequired))
                return true;
    return false;
}
std::optional<std::string> xmlWbsCode(const PlanInput &p, const std::string &kind, const std::string &id) {
    if (p.xmlWbs)
        for (const auto &v : p.xmlWbs->values)
            if (v.kind == kind && v.id == id)
                return v.code;
    return {};
}
std::string xmlWbsLevel(const XmlWbsCodes &c, const std::string &code) {
    if (c.masks.empty() || !code.starts_with(c.prefix))
        return {};
    return split(c.masks, code.substr(c.prefix.size())).back();
}
void validateXmlWbsCodes(const PlanInput &p) {
    if (!p.xmlWbs)
        return;
    const auto &c = *p.xmlWbs;
    validateXmlMasks(c.masks);
    if (characters(c.prefix).size() - 1 > 50 || c.values.size() > 20000)
        invalid("WBS 접두사·마스크·코드 개수 제한을 확인하세요.");
    std::set<std::pair<std::string, std::string>> owners;
    std::set<std::string> codes;
    for (const auto &v : c.values) {
        calculationCheckpoint();
        if (!owner(p, v) || !owners.emplace(v.kind, v.id).second || v.code.empty() ||
            characters(v.code).size() - 1 > 255 || (c.verifyUnique && !codes.insert(v.code).second))
            invalid("WBS 코드의 소유 객체·중복·길이를 확인하세요.");
        if (!c.masks.empty()) {
            if (!v.code.starts_with(c.prefix))
                invalid("WBS 코드 접두사가 다릅니다.");
            (void)split(c.masks, v.code.substr(c.prefix.size()));
        }
    }
}
void synchronizeXmlWbsCodes(PlanInput &p, const PlanInput *before, bool fillMissing) {
    if (!p.xmlWbs)
        return;
    auto &c = *p.xmlWbs;
    if (before)
        std::erase_if(c.values, [&](const auto &v) { return owner(*before, v) && !owner(p, v); });
    validateXmlWbsCodes(p);
    if (!c.generate && !fillMissing)
        return;
    auto masks = c.masks;
    if (masks.empty())
        for (int level = 1; level <= 32; ++level)
            masks.push_back({level, 0, 0, "."});
    std::map<std::string, std::vector<const WbsNode *>> groups;
    std::map<std::string, std::vector<const Task *>> tasks;
    for (const auto &g : p.wbsNodes)
        groups[g.parentId].push_back(&g);
    for (const auto &t : p.tasks)
        tasks[t.wbsId].push_back(&t);
    std::set<std::string> used;
    for (const auto &v : c.values)
        used.insert(v.code);
    auto assign = [&](const std::string &kind, const std::string &id, const std::string &parent, int depth) {
        if (xmlWbsCode(p, kind, id) || depth >= int(masks.size()) || masks[std::size_t(depth)].type == 3 ||
            (!fillMissing && (!before || owner(*before, {kind, id, {}}))))
            return;
        std::string prefix = c.prefix;
        if (!parent.empty()) {
            auto parentCode = xmlWbsCode(p, "wbs", parent);
            if (!parentCode || !depth)
                return;
            prefix = *parentCode + masks[std::size_t(depth - 1)].separator;
        }
        std::set<std::string> siblings;
        for (const auto &code : used)
            if (code.starts_with(prefix)) {
                auto tail = code.substr(prefix.size());
                try {
                    segment(masks[std::size_t(depth)], tail);
                    siblings.insert(tail);
                } catch (const AppError &) {
                }
            }
        auto code = prefix + next(masks[std::size_t(depth)], siblings);
        if (used.contains(code))
            invalid("생성한 WBS 코드가 기존 코드와 충돌합니다.");
        used.insert(code);
        c.values.push_back({kind, id, code});
    };
    std::set<std::string> seen;
    std::function<void(const std::string &, int)> visit = [&](const std::string &parent, int depth) {
        if (depth > 32)
            invalid("WBS 계층이 너무 깊습니다.");
        for (const auto *g : groups[parent]) {
            calculationCheckpoint();
            if (!seen.insert(g->id).second)
                invalid("WBS 계층이 순환합니다.");
            assign("wbs", g->id, parent, depth);
            visit(g->id, depth + 1);
        }
        for (const auto *t : tasks[parent]) {
            calculationCheckpoint();
            assign("task", t->id, parent, depth);
        }
    };
    visit("", 0);
    validateXmlWbsCodes(p);
}
} // namespace jobs
