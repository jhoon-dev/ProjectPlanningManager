#include "persistence/XerScheduleImport.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
namespace jobs {
XerScheduleSource importXerScheduleSource(const QByteArray &bytes, const QString &name, XerEncoding encoding,
                                          const QString &projectId) {
    const auto imported = importPrimaveraXer(bytes, name, encoding, projectId);
    XerScheduleSource s;
    s.input = static_cast<const PlanInput &>(imported.project);
    s.sourceName = QFileInfo(name).fileName().toStdString();
    s.sourceSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    s.projectId = projectId.toStdString();
    s.importedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
    s.statusAt = imported.project.statusAt;
    s.reportJson = QJsonDocument(imported.report).toJson(QJsonDocument::Compact).toStdString();
    return s;
}
} // namespace jobs
