#pragma once
#include "domain/Project.h"
namespace jobs {
void validateXmlMasks(const std::vector<XmlCodeMask> &);
void validateXmlLookupMask(const XmlLookup &);
void validateXmlCodeSelection(const XmlLookup &, const std::string &);
void validateXmlWbsCodes(const PlanInput &);
std::string xmlWbsLevel(const XmlWbsCodes &, const std::string &);
bool hasXmlCodeMasks(const PlanInput &);
std::string xmlLookupCode(const XmlLookup &, const std::string &optionId);
void rebuildXmlLookupOptions(CustomField &);
std::string nextXmlLookupCode(const XmlLookup &, const std::string &parentId);
void setXmlLookupMasks(PlanInput &, FieldScope, const std::string &key, const std::vector<XmlCodeMask> &, bool);
std::string appendXmlLookupCode(PlanInput &, FieldScope, const std::string &key, const std::string &parentId,
                                const std::optional<std::string> &segment, const std::string &description,
                                const std::string &newGuid);
void synchronizeXmlWbsCodes(PlanInput &, const PlanInput *before = nullptr, bool fillMissing = false);
std::optional<std::string> xmlWbsCode(const PlanInput &, const std::string &kind, const std::string &id);
} // namespace jobs
