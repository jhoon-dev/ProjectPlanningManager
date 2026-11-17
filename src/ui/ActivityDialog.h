#pragma once
#include "application/Document.h"
#include <QDialog>
namespace jobs {
class ActivityDialog final : public QDialog {
public:
    ActivityDialog(Document &, const std::string &taskId = {}, QWidget *parent = nullptr);
};
} // namespace jobs
