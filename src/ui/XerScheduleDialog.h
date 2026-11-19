#pragma once
#include "application/Document.h"
#include "persistence/PrimaveraXer.h"
#include <QDialog>
namespace ppm {
class XerScheduleDialog final : public QDialog {
public:
    explicit XerScheduleDialog(Document &, QWidget *parent = nullptr);
    ~XerScheduleDialog() override;
    void loadSource(const QString &, XerEncoding = XerEncoding::Utf8);
    void reject() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ppm
