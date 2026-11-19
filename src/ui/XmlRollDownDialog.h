#pragma once
#include "application/Document.h"
class QWidget;
namespace ppm {
void editXmlRollDown(Document &, FieldScope, const std::string &key, QWidget *);
}
