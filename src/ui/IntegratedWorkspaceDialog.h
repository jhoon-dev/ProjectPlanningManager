#pragma once
#include <QDialog>
namespace ppm {
class DesktopWorkspace;
class IntegratedWorkspaceDialog final : public QDialog {
public:
    explicit IntegratedWorkspaceDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace ppm
