#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace jobs {
class Document;
bool reviewXmlPeriodActuals(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace jobs
