#pragma once
#include "domain/Project.h"
#include <map>
namespace ppm {
const XerFinancialSnapshot *externalEvmSource(const Project &, const std::string &revisionId);
std::string latestExternalEvmSourceId(const Project &);
ExternalEvmSubmission externalEvmDraft(const Project &, const std::string &baselineId);
ExternalEvmAdoption prepareExternalEvmAdoption(const Project &, ExternalEvmSubmission, std::string id);
void validateExternalEvmAdoptions(const Project &);
struct ExternalEvmPoint {
    bool useEv = false, usePv = false;
    std::optional<std::int64_t> ev, pv;
    std::string adoptionId;
};
// Current adoption policy restates historical boundaries. Unknown interior/gaps are never interpolated.
class ExternalEvmIndex {
public:
    ExternalEvmIndex(const Project &, const std::string &baselineId);
    ExternalEvmPoint at(const std::string &taskId, const std::string &stamp, bool inclusive = true) const;

private:
    struct Series {
        const ExternalEvmAdoption *adoption = nullptr;
        std::map<std::pair<std::string, bool>, std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>>
            points;
    };
    std::map<std::string, Series> tasks_;
};
std::pair<std::string, std::string> externalEvmBounds(const XerFinancialSnapshot &, const XerFinancialPeriod &);
} // namespace ppm
