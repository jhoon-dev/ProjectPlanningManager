#pragma once
#include <QDialog>
namespace jobs {
class DesktopWorkspace;
class IntegratedWorkspaceDialog final : public QDialog {
public:
    explicit IntegratedWorkspaceDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace jobs
