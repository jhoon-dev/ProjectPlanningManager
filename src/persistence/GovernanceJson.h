#pragma once
#include "domain/Governance.h"
#include <QJsonObject>
namespace ppm {
QJsonObject encodeGovernance(const Governance &);
Governance decodeGovernance(const QJsonObject &);
} // namespace ppm
