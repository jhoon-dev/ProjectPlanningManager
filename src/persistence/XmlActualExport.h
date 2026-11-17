#pragma once
#include "domain/Project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <map>
namespace jobs {
using XmlActualPair = std::pair<std::string, std::string>;
struct XmlActualProjection {
    std::map<std::string, const ProgressEntry *> progress;
    std::map<std::string, const CostEntry *> cost;
    std::map<XmlActualPair, std::vector<const PeriodActualEntry *>> days;
    std::map<XmlActualPair, std::optional<std::int64_t>> work, assignmentCost;
    std::map<std::string, std::optional<std::int64_t>> taskWork, resourceWork;
    QJsonObject report;
    QJsonArray diagnostics;
};
XmlActualProjection projectXmlActuals(const Project &);
} // namespace jobs
