#pragma once
#include <QDialog>
namespace jobs {
class DesktopWorkspace;
class GovernanceDialog final : public QDialog {
public:
    explicit GovernanceDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace jobs
