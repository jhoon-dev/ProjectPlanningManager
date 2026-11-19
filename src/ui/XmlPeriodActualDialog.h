#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace ppm {
class Document;
bool reviewXmlPeriodActuals(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace ppm
