#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace ppm {
class Document;
bool reviewXmlActualAllocation(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace ppm
