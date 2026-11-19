#pragma once
#include "domain/Project.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace ppm {
enum class XerEncoding { Utf8, Windows1252 };
struct XerProjectInfo {
    QString id, code, name, start;
    std::size_t tasks = 0, externalIncoming = 0, externalOutgoing = 0;
};
std::vector<XerProjectInfo> listPrimaveraXerProjects(const QByteArray &, XerEncoding = XerEncoding::Utf8);
struct PrimaveraXerExchange {
    Project project;
    QByteArray xer;
    QJsonObject report;
    bool importing = true;
};
QByteArray readPrimaveraXer(const QString &path);
PrimaveraXerExchange importPrimaveraXer(const QByteArray &, const QString &sourceName, XerEncoding = XerEncoding::Utf8,
                                        const QString &projectId = {});
PrimaveraXerExchange exportPrimaveraXer(const Project &);
// Combines validated native exports, remapping every identity and foreign key.
QByteArray combinePrimaveraXer(std::vector<PrimaveraXerExchange> &);
QString publishPrimaveraXer(const PrimaveraXerExchange &, const QString &destination);
QString primaveraXerPreview(const PrimaveraXerExchange &);
} // namespace ppm
