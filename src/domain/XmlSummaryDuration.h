#pragma once
#include "domain/XmlFormula.h"
namespace jobs {
bool xmlDurationReference(const std::string &);
// Project bindings are new. Legacy task summary settings remain readable and upgrade on save.
bool hasXmlSummaryDurationReferences(const PlanInput &, bool projectOnly = false);
XmlFormulaResult xmlSummaryDuration(const PlanInput &, const std::string &key, const std::vector<std::string> &taskIds,
                                    bool wholeBaseline = false);
} // namespace jobs
