#pragma once
#include "application/Document.h"
#include <QString>
#include <functional>
class QWidget;
namespace ppm {
// Opens only a fully published, independently saved native copy after the review.
QString importMppPlan(QWidget *, Document &, const std::function<bool()> &beforeReplace);
} // namespace ppm
