#pragma once
#include "domain/Project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <map>
#include <set>
namespace jobs {
// Internal XER records shared by the plan and actual/cost converters.
struct XerRow {
    QStringList cells;
    int line = 0;
};
struct XerTable {
    QString name;
    QStringList fields;
    std::vector<XerRow> rows;
    mutable std::set<QString> used;
};
using XerTables = std::map<QString, XerTable>;
void validateXerActualReferences(XerTables &);
QJsonObject importXerActualCosts(Project &, XerTables &, QJsonArray &);
QJsonObject exportXerActualCosts(const Project &, XerTables &, QJsonArray &);
void captureXerFinancials(Project &, XerTables &, const QString &sourceName, const QString &sha256,
                          const QString &projectId);
} // namespace jobs
