#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace jobs {
class Document;
bool reviewXmlActualAllocation(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace jobs
