#pragma once
#include "domain/XmlBaselineTypes.h"
#include <QJsonObject>
#include <QJsonArray>
namespace jobs::json {
QJsonObject encodeXmlBaselines(const XmlBaselineSet &);
XmlBaselineSet decodeXmlBaselines(const QJsonObject &, bool allowOrigins = true, bool allowOutsideTimes = true);
QJsonArray encodeXmlBaselineMerges(const std::vector<XmlBaselineMerge> &);
std::vector<XmlBaselineMerge> decodeXmlBaselineMerges(const QJsonValue &);
} // namespace jobs::json
