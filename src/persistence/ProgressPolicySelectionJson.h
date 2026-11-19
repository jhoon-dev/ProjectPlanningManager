#pragma once
#include "domain/ProgressPolicySelection.h"
#include <QJsonObject>
namespace ppm::json {
QJsonObject encodeProgressPolicySelection(const ProgressPolicySelection &);
ProgressPolicySelection decodeProgressPolicySelection(const QJsonValue &);
} // namespace ppm::json
