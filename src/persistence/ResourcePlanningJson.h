#pragma once
#include "domain/Project.h"
#include <QJsonObject>
namespace ppm {
QJsonObject encodeProductivityRate(const ProductivityRate &);
ProductivityRate decodeProductivityRate(const QJsonValue &);
QJsonObject encodeProductivityEstimate(const ProductivityEstimate &);
ProductivityEstimate decodeProductivityEstimate(const QJsonValue &);
QJsonObject encodeNativeCurve(const NativeCurve &);
NativeCurve decodeNativeCurve(const QJsonValue &);
} // namespace ppm
