#pragma once
#include <QDialog>
namespace jobs {
class DesktopWorkspace;
class ServerDialog final : public QDialog {
public:
    explicit ServerDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace jobs
