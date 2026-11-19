#pragma once
#include "domain/Project.h"
namespace ppm {
bool hasLedgerIdentity(const Project &, const std::string &kind, const std::string &id);
Project migrateLedgerIdentities(const Project &, const std::vector<LedgerIdLink> &);
void validateLedgerIdMigrations(const Project &);
void validateLedgerIdCurrent(const Project &);
std::size_t ledgerIdentityRows(const Project &);
} // namespace ppm
