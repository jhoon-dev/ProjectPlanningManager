#pragma once
#include "domain/ResourcePlanTypes.h"
#include <QJsonObject>
namespace jobs {
QJsonObject encodeResourcePlan(const ResourcePlan &);
ResourcePlan decodeResourcePlan(const QJsonValue &, int schema);
} // namespace jobs
