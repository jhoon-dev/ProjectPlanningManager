#pragma once
#include "application/Document.h"
#include <QDialog>
#include <functional>
class QLabel;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QPushButton;
class QCheckBox;
namespace jobs {
class WeeklyUpdateDialog final : public QDialog {
public:
    explicit WeeklyUpdateDialog(Document &, QWidget *parent = nullptr);
    ~WeeklyUpdateDialog() override;
    std::function<void()> editStatus;
    std::function<bool(const std::string &, QWidget *, bool)> editProgress;
    bool recalculationRequested() const {
        return recalculationRequested_;
    }

private:
    Document &document_;
    std::uint64_t session_, listener_ = 0;
    bool editing_ = false;
    bool recalculationRequested_ = false;
    QLabel *heading_, *summary_, *details_, *notice_;
    QComboBox *filter_;
    QLineEdit *search_;
    QTableWidget *tasks_;
    QPushButton *status_, *edit_, *next_, *recalculate_;
    QCheckBox *continue_;
    std::string selectedId() const;
    void reload(const std::string &selected = {});
    void selected();
    void editSelected();
};
} // namespace jobs
