#pragma once
#include "domain/ProgressPolicySelection.h"
#include <QJsonObject>
namespace jobs::json {
QJsonObject encodeProgressPolicySelection(const ProgressPolicySelection &);
ProgressPolicySelection decodeProgressPolicySelection(const QJsonValue &);
} // namespace jobs::json
