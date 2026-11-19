#pragma once
#include "calculation/ProgressPolicies.h"
#include <QByteArray>
#include <QString>
namespace ppm {
QByteArray progressPolicyJson(const ProgressPolicyComparison &, const QString &createdAt = {});
void saveProgressPolicyJson(const ProgressPolicyComparison &, const QString &path);
} // namespace ppm
