#pragma once
#include "application/Document.h"
#include <QDialog>
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QTimer;
namespace ppm {
class AssignmentCurveView;
class AssignmentPeriodsDialog final : public QDialog {
public:
    AssignmentPeriodsDialog(Document &, const std::string &taskId = {}, QWidget *parent = nullptr);
    ~AssignmentPeriodsDialog() override;
    void reject() override;

private:
    Document &doc_;
    std::shared_ptr<const Project> source_;
    std::uint64_t session_ = 0, listener_ = 0, previewGeneration_ = 0;
    QComboBox *assignment_, *curve_, *period_;
    QTableWidget *table_;
    QLabel *basis_, *message_, *preview_;
    AssignmentCurveView *view_;
    QPushButton *apply_, *cancel_, *preset_, *generate_, *add_, *remove_, *reset_, *work_;
    QTimer *timer_;
    std::shared_ptr<CalculationControl> control_, previewControl_;
    std::optional<TaskSchedule> dates_;
    std::int64_t previewDuration_ = 0;
    bool busy_ = false, committing_ = false;
    const Assignment *selected() const;
    const Task *task() const;
    bool current() const;
    void controls();
    void load();
    void fill(const std::vector<AssignmentPeriod> &);
    std::vector<AssignmentPeriod> read() const;
    void changed();
    void preview();
    void generate();
    void plannedWork();
    void submit();
};
} // namespace ppm
