#pragma once
#include "domain/LevelingDecision.h"
#include <QJsonArray>
namespace jobs::json {
QJsonArray encodeLevelingDecisions(const std::vector<LevelingDecision> &);
std::vector<LevelingDecision> decodeLevelingDecisions(const QJsonValue &);
} // namespace jobs::json
