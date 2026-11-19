#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class ExternalEvmDialog final : public QDialog {
public:
    ExternalEvmDialog(Document &, const std::string &baselineId = {}, QWidget *parent = nullptr);
    ~ExternalEvmDialog() override;
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
