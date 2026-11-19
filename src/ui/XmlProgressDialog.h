#pragma once
#include "persistence/ProjectXml.h"
#include <optional>
class QWidget;
namespace ppm {
class Document;
std::optional<ProjectXmlExchange> reviewXmlProgress(QWidget *, Document &, const ProjectXmlExchange &);
bool reviewXmlProgressMerge(QWidget *, Document &, const ProjectXmlExchange &);
} // namespace ppm
