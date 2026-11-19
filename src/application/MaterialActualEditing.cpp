#include "application/Document.h"
#include "domain/MaterialActuals.h"
namespace ppm {
void Document::recordMaterialActuals(MaterialActualSubmission submission) {
    requireEditable();
    auto p = *project_;
    appendMaterialActuals(p, std::move(submission), [this] { return store_.newId(); });
    edit(std::move(p), "실제 자재 원장 제출");
}
} // namespace ppm
