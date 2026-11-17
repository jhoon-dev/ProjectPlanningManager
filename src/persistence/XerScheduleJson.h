#pragma once
#include "domain/Project.h"
#include <QJsonArray>
namespace jobs {
QJsonArray encodeXerScheduleAdoptions(const std::vector<XerScheduleAdoption> &);
std::vector<XerScheduleAdoption> decodeXerScheduleAdoptions(const QJsonValue &);
} // namespace jobs
