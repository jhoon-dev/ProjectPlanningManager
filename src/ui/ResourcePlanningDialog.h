#pragma once
#include <QDialog>
#include <memory>
namespace ppm {
class Document;
class ResourcePlanningDialog final : public QDialog {
public:
    ResourcePlanningDialog(Document &, QWidget *parent = nullptr);
    ~ResourcePlanningDialog() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
