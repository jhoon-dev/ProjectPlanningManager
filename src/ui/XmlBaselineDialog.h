#pragma once
#include "persistence/ProjectXml.h"
class QWidget;
namespace jobs {
class Document;
std::optional<ProjectXmlExchange> reviewXmlBaselines(QWidget *, Document &, const ProjectXmlExchange &);
bool reviewXmlBaselineMerge(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace jobs
