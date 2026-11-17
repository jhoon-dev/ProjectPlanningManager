#pragma once
#include "domain/Project.h"
#include <QJsonObject>
namespace jobs::json {
void encodePresentation(QJsonObject &, const Project &);
void decodePresentation(Project &, const QJsonObject &);
} // namespace jobs::json
