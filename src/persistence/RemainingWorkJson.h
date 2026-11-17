#pragma once
#include "domain/RemainingWorkTypes.h"
#include <QJsonArray>
namespace jobs::json {
QJsonArray encodeRemainingWork(const std::vector<RemainingWorkPlan> &);
std::vector<RemainingWorkPlan> decodeRemainingWork(const QJsonValue &);
} // namespace jobs::json
