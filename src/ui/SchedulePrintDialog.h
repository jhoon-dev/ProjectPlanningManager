#pragma once
#include "application/Document.h"
#include "reporting/SchedulePrint.h"
#include <QDialog>
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QScrollArea;
namespace ppm {
class TaskTableView;
SchedulePrintSource captureSchedulePrint(Document &, TaskTableView &, const QString &scope, const QString &baseline);
class SchedulePrintDialog final : public QDialog {
public:
    SchedulePrintDialog(Document &, SchedulePrintSource, QWidget *parent = nullptr);
    ~SchedulePrintDialog() override;
    void reject() override;

private:
    Document &document_;
    SchedulePrintSource source_;
    QComboBox *preset_, *scope_, *paper_, *orientation_;
    QSpinBox *margin_, *font_, *tablePercent_, *nameWidth_, *days_, *page_, *zoom_;
    QDateEdit *from_, *to_;
    QCheckBox *automatic_, *fit_, *baseline_, *relations_, *wbs_;
    QPushButton *saveSettings_;
    PrintPreferences preferences() const;
    QLabel *message_, *context_;
    QPushButton *prepare_, *pdf_, *print_, *cancel_;
    QScrollArea *preview_;
    std::shared_ptr<const SchedulePrintLayout> layout_;
    std::shared_ptr<CalculationControl> control_;
    std::uint64_t generation_ = 0;
    bool working_ = false;
    bool current() const;
    void invalidate();
    void prepare();
    void showPage();
    void output(bool pdf);
    void ready(bool working);
    void applyPreset();
    void updateContext();
};
} // namespace ppm
