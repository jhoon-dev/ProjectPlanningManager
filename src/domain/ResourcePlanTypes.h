#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace ppm {
struct Material {
    std::string id, name, unit;
    std::optional<std::int64_t> unitPriceMinor, stockMilli;
    std::optional<std::string> stockAt = {};
    bool operator==(const Material &) const = default;
};
struct MaterialUse {
    std::string id, taskId, materialId;
    std::int64_t quantityMilli = 1000;
    std::string accrual = "start";
    bool operator==(const MaterialUse &) const = default;
};
struct Expense {
    std::string id, taskId, name, account;
    std::optional<std::int64_t> amountMinor;
    std::string accrual = "uniform";
    bool operator==(const Expense &) const = default;
};
struct MaterialReceipt {
    std::string id, materialId, at;
    std::int64_t quantityMilli = 1000;
    bool operator==(const MaterialReceipt &) const = default;
};
struct ResourceRole {
    std::string id, name, type = "labor";
    bool operator==(const ResourceRole &) const = default;
};
struct RoleDemand {
    std::string id, taskId, roleId, resourceId;
    int units = 100;
    bool operator==(const RoleDemand &) const = default;
};
struct ResourcePlan {
    std::vector<Material> materials;
    std::vector<MaterialUse> uses;
    std::vector<Expense> expenses;
    std::vector<ResourceRole> roles;
    std::vector<RoleDemand> demands;
    std::vector<MaterialReceipt> receipts = {};
    bool autoDelayForReceipts = false;
    bool autoDelayRemainingForReceipts = false;
    bool operator==(const ResourcePlan &) const = default;
};
} // namespace ppm
