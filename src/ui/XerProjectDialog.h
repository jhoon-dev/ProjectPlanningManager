#pragma once
#include "persistence/PrimaveraXer.h"
#include <optional>
class QWidget;
namespace ppm {
std::optional<QString> chooseXerProject(QWidget *, const std::vector<XerProjectInfo> &);
}
