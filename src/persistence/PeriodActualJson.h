#pragma once
#include "domain/PeriodActualTypes.h"
#include <QJsonArray>
namespace jobs::json {
QJsonArray encodePeriodActuals(const std::vector<PeriodActualEntry> &);
std::vector<PeriodActualEntry> decodePeriodActuals(const QJsonValue &);
} // namespace jobs::json
