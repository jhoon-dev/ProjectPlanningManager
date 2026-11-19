#pragma once
#include "domain/ResourcePlanTypes.h"
#include <QJsonObject>
namespace ppm {
QJsonObject encodeResourcePlan(const ResourcePlan &);
ResourcePlan decodeResourcePlan(const QJsonValue &, int schema);
} // namespace ppm
