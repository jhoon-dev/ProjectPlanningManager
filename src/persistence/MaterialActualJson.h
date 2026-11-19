#pragma once
#include "domain/MaterialActualTypes.h"
#include <QJsonArray>
namespace ppm {
QJsonArray encodeMaterialActuals(const std::vector<MaterialActualEntry> &);
std::vector<MaterialActualEntry> decodeMaterialActuals(const QJsonValue &);
} // namespace ppm
