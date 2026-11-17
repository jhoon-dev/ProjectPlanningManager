#pragma once
#include "persistence/ProjectXml.h"
namespace jobs {
enum class XmlAllocationBasis { ElapsedMinutes, WorkingIntersectionMinutes };
struct XmlAllocationPolicy {
    XmlAllocationBasis basis = XmlAllocationBasis::ElapsedMinutes;
    bool rangeTotalsConfirmed = false;
};
struct XmlActualAllocation {
    std::vector<PeriodActualSubmission> batches;
    QJsonObject report;
};
XmlActualAllocation prepareXmlActualAllocation(const Project &, const ProjectXmlExchange &, const XmlProgressOptions &,
                                               const std::vector<XmlPeriodMapping> &, const XmlAllocationPolicy &);
// Recompute the whole preview and return only the revalidated submissions.
std::vector<PeriodActualSubmission> confirmXmlActualAllocation(const Project &, const ProjectXmlExchange &,
                                                               const XmlProgressOptions &,
                                                               const std::vector<XmlPeriodMapping> &,
                                                               const XmlAllocationPolicy &,
                                                               const XmlActualAllocation &);
// Create-only audit package: original.xml, source.ppm (before adoption), allocation.json.
QString publishXmlActualAllocation(const Project &, const ProjectXmlExchange &, const XmlProgressOptions &,
                                   const std::vector<XmlPeriodMapping> &, const XmlAllocationPolicy &,
                                   const XmlActualAllocation &, const QString &destination);
} // namespace jobs
