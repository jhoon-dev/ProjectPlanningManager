#pragma once
#include "domain/Project.h"
#include <QJsonArray>
namespace jobs {
void addNativeBaselineTimephased(const Project &, const Baseline &, std::vector<XmlBaselineRow> &, std::size_t first,
                                 std::size_t &periodCount, QJsonArray &diagnostics);
}
