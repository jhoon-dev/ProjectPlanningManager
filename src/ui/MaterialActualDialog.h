#pragma once
#include <QDialog>
#include <memory>
namespace jobs {
class Document;
class MaterialActualDialog final : public QDialog {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit MaterialActualDialog(Document &, QWidget *parent = nullptr);
    ~MaterialActualDialog() override;
};
} // namespace jobs
