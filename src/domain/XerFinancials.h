#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "domain/CurrencyTypes.h"
namespace ppm {
struct XerFinancialTask {
    std::string id, name;
    std::optional<std::int64_t> actualMinor;
    bool operator==(const XerFinancialTask &) const = default;
};
struct XerFinancialResource {
    std::string id, name, type;
    bool operator==(const XerFinancialResource &) const = default;
};
struct XerFinancialAssignment {
    std::string taskId, resourceId;
    std::optional<std::int64_t> regularWork, overtimeWork, regularMinor, overtimeMinor;
    bool operator==(const XerFinancialAssignment &) const = default;
};
struct XerFinancialPeriod {
    std::string taskId, resourceId, start, finish; // Empty resource = TASKFIN summary, not an extra AC.
    std::optional<std::int64_t> work, actualMinor, laborWork, equipmentWork;
    std::optional<std::int64_t> laborMinor, equipmentMinor, materialMinor, expenseMinor, bcwp, bcws;
    bool operator==(const XerFinancialPeriod &) const = default;
};
struct XerFinancialSnapshot {
    std::string sourceName, sourceSha256, projectId, statusAt, currency, importedAt, projectionSha256;
    std::vector<XerFinancialTask> tasks;
    std::vector<XerFinancialResource> resources;
    std::vector<XerFinancialAssignment> assignments;
    std::vector<XerFinancialPeriod> periods;
    bool operator==(const XerFinancialSnapshot &) const = default;
};
void validateXerFinancials(const XerFinancialSnapshot &);
struct XerFinancialDay {
    std::string date;
    std::int64_t weight = 1;
    bool operator==(const XerFinancialDay &) const = default;
};
struct XerFinancialAllocation {
    std::size_t periodIndex = 0;
    std::vector<XerFinancialDay> days;
    bool operator==(const XerFinancialAllocation &) const = default;
};
struct XerFinancialMergeAudit {
    std::string baseProjectionSha256;
    std::vector<std::size_t> replacedPeriods;
    std::vector<XerFinancialAssignment> assignmentOverrides;
    bool operator==(const XerFinancialMergeAudit &) const = default;
};
struct XerIdLink {
    std::string kind, sourceId, targetId; // task/resource; empty target explicitly retires an unobserved identity.
    bool operator==(const XerIdLink &) const = default;
};
struct XerResourceConversion {
    std::string fromType, toType;
    std::int64_t workNumerator = 1, workDenominator = 1;
    bool operator==(const XerResourceConversion &) const = default;
};
struct XerSourceConversion {
    // Major currency units: one source unit = numerator / denominator target units.
    std::string targetCurrency, effectiveDate, reference;
    std::int64_t numerator = 1, denominator = 1;
    std::vector<XerResourceConversion> resources;
    bool operator==(const XerSourceConversion &) const = default;
};
struct XerRelinkAudit {
    XerFinancialSnapshot incoming;
    std::string projectName;
    std::optional<std::string> sourceStatusAt;
    std::vector<XerIdLink> incomingLinks, previousLinks;
    bool confirmed = false, replaceMissingData = false;
    std::optional<XerSourceConversion> incomingConversion, previousConversion;
    std::vector<CurrencyRate> incomingCurrencySteps;
    bool operator==(const XerRelinkAudit &) const = default;
};
struct XerFinancialRevision {
    std::string id, reporter, reason, recordedAt, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    XerFinancialSnapshot financials;
    std::vector<XerFinancialAllocation> allocations;
    std::vector<std::string> ownedEntryIds;
    std::optional<XerFinancialMergeAudit> merge;
    std::optional<XerRelinkAudit> relink;
    bool operator==(const XerFinancialRevision &) const = default;
};
struct XerFinancialSubmission {
    XerFinancialSnapshot financials;
    std::vector<XerFinancialAllocation> allocations;
    std::string reporter, reason, recordedAt;
};
struct Project;
const XerFinancialSnapshot *activeXerFinancials(const Project &);
bool xerFinancialsMatch(const Project &, const std::string &projectionHash);
void validateXerFinancialRevisions(const Project &);
bool xerFinancialsNeedSchema27(const Project &);
} // namespace ppm
