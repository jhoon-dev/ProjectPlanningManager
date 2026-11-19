#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class LedgerIdentityDialog final : public QDialog {
public:
    explicit LedgerIdentityDialog(Document &, QWidget *parent = nullptr);
    ~LedgerIdentityDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
