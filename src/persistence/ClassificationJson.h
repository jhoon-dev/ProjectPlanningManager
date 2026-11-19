#pragma once
#include "domain/Project.h"
#include <QJsonArray>
#include <QJsonObject>

namespace ppm::json {
QJsonArray encodeXmlCodeMasks(const std::vector<XmlCodeMask> &);
std::vector<XmlCodeMask> decodeXmlCodeMasks(const QJsonValue &);
QJsonArray encodeCustomFields(const std::vector<CustomField> &);
std::vector<CustomField> decodeCustomFields(const QJsonValue &, int schema = 23);
QJsonObject encodeClassification(const PlanInput &);
void decodeClassification(PlanInput &, const QJsonObject &, int schema = 23);
QJsonArray encodeAttributes(const std::vector<TaskAttribute> &);
std::vector<TaskAttribute> decodeAttributes(const QJsonValue &);
QJsonArray encodeSavedTaskViews(const std::vector<SavedTaskView> &);
std::vector<SavedTaskView> decodeSavedTaskViews(const QJsonValue &);
QJsonObject encodeTaskViewSettings(const TaskViewSettings &);
QJsonArray encodeSavedResourceViews(const std::vector<SavedResourceView> &);
std::vector<SavedResourceView> decodeSavedResourceViews(const QJsonValue &);
} // namespace ppm::json
