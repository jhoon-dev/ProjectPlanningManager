#pragma once
#include "application/Document.h"
class QWidget;
namespace jobs {
void editXmlRollDown(Document &, FieldScope, const std::string &key, QWidget *);
}
