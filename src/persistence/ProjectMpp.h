#pragma once
#include "persistence/ProjectXml.h"
namespace ppm {
struct MppReaderOptions {
    QString runtimeDirectory;     // Empty selects the bundled reader beside the application.
    QString planCurrency = "KRW"; // No source financial values are converted.
    int timeoutMilliseconds = 60000;
};
struct ProjectMppExchange {
    Project project;
    QByteArray mpp, readerXml;
    QJsonObject report;
};
QByteArray readProjectMpp(const QString &path);
ProjectMppExchange importProjectMpp(const QByteArray &, const QString &sourceName, const MppReaderOptions & = {});
QString publishProjectMpp(const ProjectMppExchange &, const QString &destination);
QString projectMppPreview(const ProjectMppExchange &);
} // namespace ppm
