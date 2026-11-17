#pragma once
#include "calculation/SharedAvailabilityQuery.h"
#include "calculation/Reports.h"
#include <QByteArray>
namespace jobs {
QByteArray sharedAvailabilityJson(const Workspace &, const SharedAvailabilityResult &, std::uint64_t generation);
ProjectReport sharedAvailabilityReport(const Workspace &, const SharedAvailabilityResult &, std::uint64_t generation,
                                       const std::string &generatedAt);
} // namespace jobs
