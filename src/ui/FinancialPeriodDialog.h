#pragma once
#include "application/Document.h"
#include <QDialog>
namespace jobs {
class FinancialPeriodDialog final : public QDialog {
public:
    FinancialPeriodDialog(Document &, const std::string &baselineId = {}, QWidget *parent = nullptr);
    ~FinancialPeriodDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace jobs
