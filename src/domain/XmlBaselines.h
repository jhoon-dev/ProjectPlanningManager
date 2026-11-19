#pragma once
#include "domain/Project.h"
#include <map>
namespace ppm {
std::optional<int> xmlBaselineTimeNumber(int type, const std::string &kind);
const XmlBaselineOrigin &xmlBaselineOrigin(const XmlBaselineSet &, int number);
std::vector<XmlBaselineLink> xmlBaselineLinks(const XmlBaselineSet &);
XmlBaselineSet mergeXmlBaselineSets(const std::optional<XmlBaselineSet> &, const XmlBaselineMergeSubmission &);
void validateXmlBaselineMergeTarget(const Project &, const XmlBaselineMergeSubmission &);
void validateXmlBaselineMerges(const Project &);
std::string xmlBaselineRowKey(const std::string &kind, const std::string &id, const std::string &resource = {});
int xmlBaselineTimeType(int slot, const std::string &kind, bool cost);
bool xmlBaselineTimeCost(int type);
std::int64_t xmlBaselineMinutes(const std::string &, std::int64_t maximum);
std::int64_t xmlBaselineMoney(const std::string &wire, const std::string &currency);
std::string xmlBaselineMoneyText(std::int64_t minor, const std::string &currency);
void validateXmlBaselines(const PlanInput &);
} // namespace ppm
