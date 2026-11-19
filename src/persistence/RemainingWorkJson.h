#pragma once
#include "domain/RemainingWorkTypes.h"
#include <QJsonArray>
namespace ppm::json {
QJsonArray encodeRemainingWork(const std::vector<RemainingWorkPlan> &);
std::vector<RemainingWorkPlan> decodeRemainingWork(const QJsonValue &);
} // namespace ppm::json
