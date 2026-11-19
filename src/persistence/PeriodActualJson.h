#pragma once
#include "domain/PeriodActualTypes.h"
#include <QJsonArray>
namespace ppm::json {
QJsonArray encodePeriodActuals(const std::vector<PeriodActualEntry> &);
std::vector<PeriodActualEntry> decodePeriodActuals(const QJsonValue &);
} // namespace ppm::json
