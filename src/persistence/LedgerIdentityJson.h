#pragma once
#include "domain/Project.h"
#include <QJsonArray>
namespace jobs {
QJsonArray encodeLedgerIdMigrations(const std::vector<LedgerIdMigration> &);
std::vector<LedgerIdMigration> decodeLedgerIdMigrations(const QJsonValue &);
} // namespace jobs
