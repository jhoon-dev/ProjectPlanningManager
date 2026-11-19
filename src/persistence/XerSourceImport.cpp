#include "persistence/XerSourceImport.h"
#include "persistence/XerFinancialJson.h"
#include <QCryptographicHash>
#include <QDateTime>
namespace ppm {
XerRelinkAudit importXerRelinkSource(const QByteArray &bytes, const QString &name, XerEncoding encoding,
                                     const QString &projectId, const std::string &status) {
    auto imported = importPrimaveraXer(bytes, name, encoding, projectId);
    const auto &p = imported.project;
    XerRelinkAudit out;
    out.projectName = p.name;
    out.sourceStatusAt = p.statusAt;
    if (p.xerFinancials)
        out.incoming = *p.xerFinancials;
    else {
        auto &s = out.incoming;
        s.sourceName = name.toStdString();
        s.sourceSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
        s.projectId = projectId.toStdString();
        s.statusAt = p.statusAt.value_or(status);
        s.currency = p.currency;
        s.importedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
        s.projectionSha256 = xerFinancialProjectionHash(p);
        for (const auto &t : p.tasks)
            s.tasks.push_back({t.id, t.name, {}});
        for (const auto &r : p.resources)
            s.resources.push_back({r.id, r.name, r.type});
        for (const auto &a : p.assignments)
            s.assignments.push_back({a.taskId, a.resourceId, {}, {}, {}, {}});
    }
    validateXerFinancials(out.incoming);
    return out;
}
} // namespace ppm
