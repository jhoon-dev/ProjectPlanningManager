#pragma once
#include "calculation/SharedResources.h"
#include "domain/MaterialActuals.h"
namespace ppm {
struct IntegratedTask {
    ProjectTaskRef ref;
    std::string projectName, name;
    std::uint64_t revision = 0;
    bool floatAvailable = false;
    std::optional<std::string> originalStart, originalFinish;
    TaskSchedule schedule;
    std::vector<ProjectTaskRef> driving;
};
struct IntegratedResult {
    CalculationStatus status = CalculationStatus::Incomplete;
    bool forecast = false, leveled = false;
    std::optional<std::string> finish;
    std::vector<IntegratedTask> tasks;
    SharedResourceResult shared;
    std::vector<Diagnostic> diagnostics;
    std::string policy, floatPolicy;
    struct MaterialBalance {
        std::string projectId, materialId, name;
        std::uint64_t revision = 0;
        std::size_t ledgerRows = 0;
        ActualMaterialStock stock;
        std::int64_t remainingUse = 0, incoming = 0;
    };
    std::vector<MaterialBalance> materials;
};
IntegratedResult calculateIntegratedWorkspace(const Workspace &, const std::vector<std::shared_ptr<const Project>> &,
                                              const std::shared_ptr<CalculationControl> & = {});
} // namespace ppm
