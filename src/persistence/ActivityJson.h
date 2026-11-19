#pragma once
#include "domain/ActivityTypes.h"
#include <QJsonArray>
#include <QJsonValue>
namespace ppm {
QJsonArray encodeActivities(const std::vector<ActivityDefinition> &);
std::vector<ActivityDefinition> decodeActivities(const QJsonValue &);
QJsonArray encodeStepReports(const std::vector<StepReport> &);
std::vector<StepReport> decodeStepReports(const QJsonValue &);
} // namespace ppm
