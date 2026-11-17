#pragma once
#include "persistence/ProjectXml.h"
#include <QJsonArray>
class QXmlStreamWriter;
namespace jobs {
std::vector<XmlBaselineCandidate> readXmlBaselines(const QByteArray &, const Project &, const QJsonArray &mapping);
std::vector<XmlBaselineRow> projectXmlBaselines(const Project &, QJsonArray &diagnostics);
using XmlBaselineIndex = std::map<std::string, std::vector<const XmlBaselineRow *>>;
XmlBaselineIndex indexXmlBaselines(const std::vector<XmlBaselineRow> &);
void writeXmlBaselines(QXmlStreamWriter &, const XmlBaselineIndex &, const std::string &kind, const std::string &id,
                       const std::string &resourceId, int uid, const std::string &currency);
void writeXmlBaselineTimes(QXmlStreamWriter &, const XmlBaselineIndex &, const std::string &kind, const std::string &id,
                           const std::string &resourceId, int uid);
void verifyXmlBaselineImport(const ProjectXmlExchange &);
std::int64_t xmlBaselineMinutes(const std::string &, std::int64_t maximum);
std::string xmlBaselineDurationText(std::int64_t fixedMinutes);
} // namespace jobs
