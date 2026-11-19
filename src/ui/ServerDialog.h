#pragma once
#include <QDialog>
namespace ppm {
class DesktopWorkspace;
class ServerDialog final : public QDialog {
public:
    explicit ServerDialog(DesktopWorkspace &, QWidget *parent = nullptr);
};
} // namespace ppm
