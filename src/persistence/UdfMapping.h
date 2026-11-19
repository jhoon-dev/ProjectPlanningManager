#pragma once
#include "domain/Project.h"
#include <QJsonArray>
namespace ppm {
class RollDownValues;
struct ExternalField {
    FieldScope scope;
    std::string nativeId, externalId, name;
    CustomFieldType type;
    std::optional<XmlFieldIdentity> xmlIdentity = {};
    std::optional<XmlLookup> lookup = {};
};
std::vector<ExternalField> exportUdfFields(const Project &, bool xml, QJsonArray &, QJsonArray &);
std::optional<std::pair<FieldScope, CustomFieldType>> xmlUdfType(const std::string &);
std::optional<FieldScope> xmlDynamicUdfScope(const std::string &);
bool xmlOutlineUdf(const std::string &);
std::string xmlSlotName(const std::string &);
std::optional<CustomFieldType> xmlCfType(const std::string &);
std::string xmlCfType(CustomFieldType);
int xmlLookupType(CustomFieldType);
std::optional<CustomFieldType> xmlLookupType(int);
std::string importXmlUdfScalar(const PlanInput &, CustomFieldType, const std::string &, int durationFormat = 3,
                               bool lookup = false, bool packedDate = false);
std::string exportXmlUdfScalar(const PlanInput &, CustomFieldType, const std::string &, bool lookup = false,
                               bool packedDate = false);
int xmlFieldDurationFormat(const std::string &);
ExternalField importUdfField(Project &, bool xml, FieldScope, const std::string &id, const std::string &name,
                             CustomFieldType, QJsonArray &);
void importUdfValue(Project &, const ExternalField &, std::vector<TaskAttribute> &, const std::string &);
std::optional<std::string> exportUdfValue(const Project &, const ExternalField &, const Task *, const Resource *,
                                          QJsonArray &, const Assignment * = nullptr, const RollDownValues * = nullptr);
} // namespace ppm
