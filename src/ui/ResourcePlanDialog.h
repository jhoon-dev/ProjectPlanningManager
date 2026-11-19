#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class ResourcePlanDialog final : public QDialog {
public:
    explicit ResourcePlanDialog(Document &, QWidget *parent = nullptr);
};
} // namespace ppm
