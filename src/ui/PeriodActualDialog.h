#pragma once
#include "application/Document.h"
#include <QDialog>
class QComboBox;
class QTableWidget;
class QTableView;
class QStandardItemModel;
class QLineEdit;
class QLabel;
class QPushButton;
namespace jobs {
class PeriodActualDialog final : public QDialog {
public:
    PeriodActualDialog(Document &, const std::string &taskId = {}, QWidget *parent = nullptr);
    ~PeriodActualDialog() override;
    void reject() override;

private:
    Document &document_;
    std::shared_ptr<const Project> source_;
    std::uint64_t sourceSession_ = 0, listener_ = 0;
    QComboBox *task_;
    QTableWidget *entries_;
    QTableView *history_;
    QStandardItemModel *resourceChoices_;
    QLineEdit *total_, *reporter_, *reason_;
    QLabel *message_, *balance_;
    QPushButton *submit_, *cancel_, *add_, *remove_, *correct_;
    std::shared_ptr<CalculationControl> control_;
    bool busy_ = false, committing_ = false;
    bool current() const;
    void loadTask();
    void addRow(const PeriodActualEntry *entry = nullptr);
    void submit();
    void controls();
};
} // namespace jobs
