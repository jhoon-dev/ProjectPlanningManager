#pragma once
#include "application/Document.h"
#include "persistence/PrimaveraXer.h"
#include <QDialog>
namespace jobs {
class XerSourceRelinkDialog final : public QDialog {
public:
    explicit XerSourceRelinkDialog(Document &, QWidget *parent = nullptr);
    ~XerSourceRelinkDialog() override;
    void loadSource(const QString &, XerEncoding = XerEncoding::Utf8);
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace jobs
