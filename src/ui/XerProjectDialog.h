#pragma once
#include "persistence/PrimaveraXer.h"
#include <optional>
class QWidget;
namespace jobs {
std::optional<QString> chooseXerProject(QWidget *, const std::vector<XerProjectInfo> &);
}
