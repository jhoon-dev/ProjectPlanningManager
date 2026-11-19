#pragma once
#include "calculation/IntegratedWorkspace.h"
#include "calculation/Reports.h"
#include <QByteArray>
namespace ppm {
ProjectReport integratedWorkspaceReport(const Workspace &, const IntegratedResult &, std::uint64_t generation,
                                        const std::string &generatedAt);
QByteArray integratedWorkspaceJson(const Workspace &, const IntegratedResult &, std::uint64_t generation);
} // namespace ppm
