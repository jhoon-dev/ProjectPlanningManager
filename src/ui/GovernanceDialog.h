#pragma once
#include <QDialog>
namespace ppm {
class DesktopWorkspace;
class GovernanceDialog final : public QDialog {
public:
    explicit GovernanceDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace ppm
