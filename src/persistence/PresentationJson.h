#pragma once
#include "domain/Project.h"
#include <QJsonObject>
namespace ppm::json {
void encodePresentation(QJsonObject &, const Project &);
void decodePresentation(Project &, const QJsonObject &);
} // namespace ppm::json
