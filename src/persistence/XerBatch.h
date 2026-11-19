#pragma once
#include "domain/Workspace.h"
#include "persistence/PrimaveraXer.h"
#include <QStringList>
#include <functional>
namespace ppm {
struct XerBatchSource {
    QByteArray bytes;
    QString name;
    XerEncoding encoding = XerEncoding::Utf8;
    QStringList projectIds;
};
struct XerBatchExchange {
    Workspace workspace;
    std::vector<PrimaveraXerExchange> projects;
    std::vector<QByteArray> originals;
    QByteArray combined;
    QJsonObject report;
    bool importing = true;
};
XerBatchExchange importXerBatch(const std::vector<XerBatchSource> &);
XerBatchExchange exportXerBatch(const std::vector<Project> &, const Workspace & = {});
QString publishXerBatch(const XerBatchExchange &, const QString &destination, std::function<void()> beforePublish = {});
QString xerBatchPreview(const XerBatchExchange &);
} // namespace ppm
