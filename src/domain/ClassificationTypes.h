#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jobs {
enum class CustomFieldType { Text, Number, Date, Choice, Flag, Cost, Duration, Start, Finish };
enum class FieldScope { Task, Project, Resource, Assignment };
struct XmlFieldIdentity {
    std::string fieldId, guid, fieldName;
    bool operator==(const XmlFieldIdentity &) const = default;
};
struct FieldOption {
    std::string id, name;
    bool operator==(const FieldOption &) const = default;
};
struct XmlLookupValue {
    std::string optionId, valueId, guid, value, description, parentId;
    bool listed = true;
    bool operator==(const XmlLookupValue &) const = default;
};
struct XmlCodeMask {
    int level = 1, type = 0, length = 0;
    std::string separator = ".";
    bool operator==(const XmlCodeMask &) const = default;
};
struct XmlWbsCode {
    std::string kind, id, code; // task or wbs; stable native owner ID, separate from the code.
    bool operator==(const XmlWbsCode &) const = default;
};
struct XmlWbsCodes {
    std::string prefix;
    bool generate = false, verifyUnique = false;
    std::vector<XmlCodeMask> masks;
    std::vector<XmlWbsCode> values;
    bool operator==(const XmlWbsCodes &) const = default;
};
struct XmlLookup {
    CustomFieldType valueType = CustomFieldType::Text;
    std::string tableGuid, tableName, defaultOptionId, fieldGuid, fieldName;
    int maxValues = 1; // 0 means any number, bounded by the 1,000-option native limit.
    bool restrictValues = true, leafOnly = false;
    std::vector<XmlLookupValue> values;
    std::vector<XmlCodeMask> masks;
    bool allLevelsRequired = false;
    bool operator==(const XmlLookup &) const = default;
};
struct XmlRollDown {
    std::string assignmentFieldId;
    bool automatic = false;
    bool operator==(const XmlRollDown &) const = default;
};
struct XmlRollUp {
    int calculationType = 0, rollupType = 0;
    bool operator==(const XmlRollUp &) const = default;
};
struct XmlFormulaReference {
    std::string token, key;
    FieldScope scope = FieldScope::Task;
    bool operator==(const XmlFormulaReference &) const = default;
};
struct XmlFormula {
    std::string source;
    std::vector<XmlFormulaReference> references;
    bool enabled = false;
    bool operator==(const XmlFormula &) const = default;
};
struct CustomField {
    std::string id, name;
    CustomFieldType type = CustomFieldType::Text;
    std::string unit;
    std::vector<FieldOption> options;
    std::string expression = {};
    std::optional<XmlFieldIdentity> xmlIdentity = {};
    std::optional<XmlLookup> xmlLookup = {};
    std::optional<XmlRollDown> xmlRollDown = {};
    std::optional<XmlRollUp> xmlRollUp = {};
    std::optional<XmlFormula> xmlFormula = {};
    bool operator==(const CustomField &) const = default;
};
inline bool calculatedField(const CustomField &f) {
    return !f.expression.empty() || (f.xmlFormula && f.xmlFormula->enabled);
}
struct ActivityCode {
    std::string id, name, parentId;
    bool operator==(const ActivityCode &) const = default;
};
struct ActivityCodeType {
    std::string id, name;
    std::vector<ActivityCode> values;
    bool operator==(const ActivityCodeType &) const = default;
};
// field:<definition ID> or code:<type ID>. Absence differs from text "" or number "0".
struct TaskAttribute {
    std::string key, value;
    bool operator==(const TaskAttribute &) const = default;
};
struct AttributeFilter {
    std::string key, operation = "equals", value;
    bool operator==(const AttributeFilter &) const = default;
};
struct ViewColumn {
    std::string key;
    int width = 120;
    bool hidden = false;
    bool operator==(const ViewColumn &) const = default;
};
struct TaskViewSettings {
    std::string wbsId, query;
    int filter = 0, sort = 0;
    std::vector<std::string> groupBy, groupPath;
    std::vector<AttributeFilter> attributeFilters;
    std::string sortKey;
    bool descending = false;
    std::vector<ViewColumn> columns;
    bool operator==(const TaskViewSettings &) const = default;
};
struct SavedTaskView {
    std::string id, name;
    TaskViewSettings settings;
    bool operator==(const SavedTaskView &) const = default;
};
struct ResourceViewSettings {
    std::string query, sortKey = "name";
    bool descending = false;
    std::vector<std::string> groupBy;
    std::vector<AttributeFilter> filters;
    bool operator==(const ResourceViewSettings &) const = default;
};
struct SavedResourceView {
    std::string id, name;
    ResourceViewSettings settings;
    bool operator==(const SavedResourceView &) const = default;
};
inline constexpr std::array<std::string_view, 14> builtinTaskColumns{
    "name",      "duration", "predecessors", "start",      "finish",   "totalFloat", "critical",
    "resources", "id",       "calendar",     "constraint", "deadline", "effort",     "breaks"};
} // namespace jobs
