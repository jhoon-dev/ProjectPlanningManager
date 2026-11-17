#pragma once
#include "persistence/PrimaveraXer.h"
namespace jobs {
XerScheduleSource importXerScheduleSource(const QByteArray &, const QString &, XerEncoding, const QString &projectId);
} // namespace jobs
