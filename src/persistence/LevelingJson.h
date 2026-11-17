#pragma once
#include "domain/LevelingSelection.h"
#include <QJsonObject>
namespace jobs::json {
QJsonObject encodeLeveling(const LevelingSelection &);
LevelingSelection decodeLeveling(const QJsonValue &);
} // namespace jobs::json
