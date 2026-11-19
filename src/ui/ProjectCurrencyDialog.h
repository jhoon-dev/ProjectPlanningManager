#pragma once
#include <QDialog>
#include <memory>
namespace ppm {
class Document;
class ProjectCurrencyDialog final : public QDialog {
public:
    explicit ProjectCurrencyDialog(Document &, QWidget *parent = nullptr);
    ~ProjectCurrencyDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
