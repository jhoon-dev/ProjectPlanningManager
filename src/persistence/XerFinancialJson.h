#pragma once
#include "domain/Project.h"
#include <QJsonObject>
#include <QJsonArray>
namespace jobs {
QJsonObject encodeXerFinancials(const XerFinancialSnapshot &);
XerFinancialSnapshot decodeXerFinancials(const QJsonValue &);
std::string xerFinancialProjectionHash(const Project &);
QJsonArray encodeXerFinancialRevisions(const std::vector<XerFinancialRevision> &);
std::vector<XerFinancialRevision> decodeXerFinancialRevisions(const QJsonValue &, bool allowMerge = false,
                                                              bool allowRelink = false, bool allowConversion = false,
                                                              bool allowCurrencySteps = false);
bool xerFinancialsCurrent(const Project &);
} // namespace jobs
