#pragma once
#include <QString>
#include <optional>
#include <vector>
class QWidget;
namespace ppm {
struct XerBatchChoice {
    QString label;
    bool blocked = false;
};
std::optional<std::vector<std::size_t>> chooseXerBatch(QWidget *, const std::vector<XerBatchChoice> &,
                                                       const QString &note);
} // namespace ppm
