#pragma once
#include "application/Document.h"
#include <QDialog>
namespace jobs {
class ResourcePlanDialog final : public QDialog {
public:
    explicit ResourcePlanDialog(Document &, QWidget *parent = nullptr);
};
} // namespace jobs
