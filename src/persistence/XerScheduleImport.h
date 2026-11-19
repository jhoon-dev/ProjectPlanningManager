#pragma once
#include "persistence/PrimaveraXer.h"
namespace ppm {
XerScheduleSource importXerScheduleSource(const QByteArray &, const QString &, XerEncoding, const QString &projectId);
} // namespace ppm
