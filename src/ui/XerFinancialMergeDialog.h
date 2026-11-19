#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class XerFinancialMergeDialog final : public QDialog {
public:
    explicit XerFinancialMergeDialog(Document &, QWidget *parent = nullptr, std::optional<XerRelinkAudit> relink = {});
    ~XerFinancialMergeDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
