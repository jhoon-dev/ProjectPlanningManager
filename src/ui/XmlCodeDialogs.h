#pragma once
#include "application/Document.h"
class QWidget;
namespace jobs {
void editXmlLookupMask(Document &, FieldScope, const std::string &key, QWidget *);
void createXmlLookupCode(Document &, FieldScope, const std::string &key, QWidget *);
void editXmlWbsCodes(Document &, QWidget *);
} // namespace jobs
