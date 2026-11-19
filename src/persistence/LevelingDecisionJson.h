#pragma once
#include "domain/LevelingDecision.h"
#include <QJsonArray>
namespace ppm::json {
QJsonArray encodeLevelingDecisions(const std::vector<LevelingDecision> &);
std::vector<LevelingDecision> decodeLevelingDecisions(const QJsonValue &);
} // namespace ppm::json
