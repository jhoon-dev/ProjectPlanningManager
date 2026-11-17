#include "application/Document.h"
#include "domain/ExternalEvm.h"
namespace jobs {
void Document::recordExternalEvm(ExternalEvmSubmission submission) {
    requireEditable();
    auto adoption = prepareExternalEvmAdoption(*project_, std::move(submission), store_.newId());
    auto p = *project_;
    const bool withdrawn = adoption.withdrawn;
    p.externalEvmAdoptions.push_back(std::move(adoption));
    edit(std::move(p), withdrawn ? "외부 XER EV/PV 채택 철회" : "외부 XER EV/PV native 채택");
}
} // namespace jobs
