#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class XerFinancialEditDialog final : public QDialog {
public:
    explicit XerFinancialEditDialog(Document &, QWidget *parent = nullptr);
    ~XerFinancialEditDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
