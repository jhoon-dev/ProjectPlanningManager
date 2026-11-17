#pragma once
#include "domain/Governance.h"
#include <QJsonObject>
namespace jobs {
QJsonObject encodeGovernance(const Governance &);
Governance decodeGovernance(const QJsonObject &);
} // namespace jobs
