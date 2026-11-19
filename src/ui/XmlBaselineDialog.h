#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace ppm {
class Document;
std::optional<ProjectXmlExchange> reviewXmlBaselines(QWidget *, Document &, const ProjectXmlExchange &);
bool reviewXmlBaselineMerge(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace ppm
