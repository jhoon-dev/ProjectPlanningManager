#pragma once
#include "calculation/Scheduler.h"
#include <QDialog>
class QComboBox;
class QCheckBox;
class QLabel;
class QTableView;
namespace ppm {
class LevelingWitnessModel;
class LevelingWitnessDialog final : public QDialog {
public:
    LevelingWitnessDialog(const Project &, const ScheduleResult &, const std::string &focus, const QString &scenario,
                          QWidget *parent = nullptr);

private:
    LevelingWitnessModel *model_;
    QComboBox *tasks_;
    QCheckBox *changedOnly_;
    QLabel *summary_;
    QTableView *table_;
    void refresh();
};
} // namespace ppm
