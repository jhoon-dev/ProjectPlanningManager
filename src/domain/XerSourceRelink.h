#pragma once
#include "domain/XerFinancialMerge.h"
namespace ppm {
struct XerRelinkCandidate {
    XerFinancialSnapshot financials;
    std::vector<XerFinancialAllocation> allocations;
};
struct XerRelinkSubmission {
    XerRelinkAudit audit;
    XerFinancialMergeSubmission merge;
};
std::vector<XerIdLink> suggestXerLinks(const XerFinancialSnapshot &, const Project &);
XerFinancialSnapshot convertXerSource(const XerFinancialSnapshot &, const XerSourceConversion &);
XerRelinkCandidate buildXerRelinkCandidate(const XerFinancialSnapshot &previous,
                                           const std::vector<XerFinancialAllocation> &previousAllocations,
                                           const XerRelinkAudit &, const XerFinancialSnapshot &target,
                                           const std::map<PeriodActualKey, const PeriodActualEntry *> &native = {});
Project projectForXerRelink(const Project &, const XerRelinkAudit &);
XerRelinkCandidate validateXerRelinkRevision(const Project &, const XerFinancialSnapshot &,
                                             const std::vector<XerFinancialAllocation> &, const XerFinancialRevision &);
} // namespace ppm
