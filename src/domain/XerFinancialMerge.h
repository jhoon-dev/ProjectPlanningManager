#pragma once
#include "domain/XerFinancialAllocation.h"
namespace jobs {
struct XerFinancialMergeOptions {
    std::vector<std::size_t> replacePeriods;
    std::vector<XerFinancialAssignment> assignmentOverrides;
};
struct XerFinancialMergeSubmission {
    XerFinancialMergeOptions options;
    std::string reporter, reason, recordedAt;
};
struct XerFinancialMergeIssue {
    std::string kind, taskId, resourceId, start, finish, message;
    std::optional<std::size_t> periodIndex;
    bool resolved = false;
};
struct XerFinancialMergeResult {
    XerFinancialSnapshot financials;
    std::vector<XerFinancialAllocation> allocations;
    std::vector<std::string> entryIds;
    std::vector<XerFinancialMergeIssue> issues;
    bool ready() const;
};
// Pure analysis: native cost/progress/period journals are never mutated.
XerFinancialMergeResult mergeXerFinancials(const Project &, const XerFinancialMergeOptions & = {});
} // namespace jobs
