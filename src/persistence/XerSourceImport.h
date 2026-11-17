#pragma once
#include "persistence/PrimaveraXer.h"
namespace jobs {
XerRelinkAudit importXerRelinkSource(const QByteArray &, const QString &, XerEncoding, const QString &projectId,
                                     const std::string &nativeStatus);
}
