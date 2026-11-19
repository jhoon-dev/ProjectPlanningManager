#pragma once
#include "domain/Project.h"
#include <QJsonObject>
namespace ppm::json {
QJsonObject encodePlanInput(const PlanInput &);
PlanInput decodePlanInput(const QJsonObject &, int schema);
QJsonObject encodeProject(const Project &project, bool includeXerFinancials = true);
Project decodeProject(const QJsonObject &object, int schema);
} // namespace ppm::json
