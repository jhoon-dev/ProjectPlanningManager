#include "application/Document.h"
#include "domain/XmlBaselines.h"
namespace ppm {
void Document::recordXmlBaselineMerge(XmlBaselineMergeSubmission submission) {
    requireEditable();
    validateXmlBaselineMergeTarget(*project_, submission);
    auto p = *project_;
    XmlBaselineMerge event;
    static_cast<XmlBaselineMergeSubmission &>(event) = std::move(submission);
    event.id = store_.newId();
    event.sourceDocumentId = p.documentId;
    event.recordedRevision = p.revision + 1;
    event.before = p.xmlBaselines;
    event.after = mergeXmlBaselineSets(event.before, event);
    if (event.before == std::optional{event.after})
        return;
    p.xmlBaselines = event.after;
    p.xmlBaselineMerges.push_back(std::move(event));
    edit(std::move(p), "외부 XML 기준선 병합");
}
} // namespace ppm
