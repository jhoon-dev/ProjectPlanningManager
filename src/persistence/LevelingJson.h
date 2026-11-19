#pragma once
#include "domain/LevelingSelection.h"
#include <QJsonObject>
namespace ppm::json {
QJsonObject encodeLeveling(const LevelingSelection &);
LevelingSelection decodeLeveling(const QJsonValue &);
} // namespace ppm::json
