#pragma once
#include "application/Document.h"
#include <QDialog>
class QComboBox;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
namespace ppm {
class ComparisonDialog final : public QDialog {
public:
    ComparisonDialog(Document &document, const std::string &baselineId, QWidget *parent = nullptr);
    ~ComparisonDialog() override;

private:
    Document &document_;
    QComboBox *baseline_;
    QLabel *summary_;
    QTableWidget *table_;
    QPlainTextEdit *diagnostics_;
    QPlainTextEdit *details_;
    std::uint64_t generation_ = 0;
    std::uint64_t session_ = 0, subscription_ = 0;
    QTableWidget *outcomes_;
    QLabel *adoption_;
    void recalculate();
    void display(const ComparisonReport &report);
};
} // namespace ppm
