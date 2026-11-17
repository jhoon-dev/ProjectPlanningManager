#pragma once
#include "application/Document.h"
#include "calculation/Resources.h"
#include <QDialog>
#include <QPointer>
class QComboBox;
class QSpinBox;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
namespace jobs {
class LevelingWitnessDialog;
class ResourceComparisonDialog final : public QDialog {
public:
    ResourceComparisonDialog(Document &, QWidget *parent = nullptr, const std::string &baselineId = {});
    ~ResourceComparisonDialog() override;
    void reject() override;

private:
    Document &document_;
    QComboBox *sourceChoice_, *otherChoice_, *priority_;
    QSpinBox *horizon_;
    QLineEdit *name_;
    QPushButton *save_;
    QPushButton *adopt_, *release_, *cancel_;
    QPushButton *witnessA_, *witnessB_;
    QPointer<LevelingWitnessDialog> witnessAWindow_, witnessBWindow_;
    std::optional<Project> witnessOtherSource_;
    std::optional<ScheduleResult> witnessOther_;
    std::uint64_t witnessListener_ = 0;
    QLineEdit *reason_;
    QLabel *summary_;
    QComboBox *baseline_;
    QTableWidget *outcomes_;
    QLabel *outcomeState_;
    QTableWidget *tasks_, *before_, *after_, *other_, *current_;
    QPlainTextEdit *diagnostics_;
    std::optional<Project> source_;
    std::optional<LevelingResult> result_;
    std::uint64_t generation_ = 0;
    std::uint64_t sourceSession_ = 0;
    std::optional<Project> anchor_;
    std::string candidatePolicy_;
    std::shared_ptr<CalculationControl> control_;
    bool posting_ = false;
    void recalculate();
    void invalidate();
    void saveScenario();
    void applySelection(bool clear);
    void closeWitnesses();
    void openWitness(bool other);
};
} // namespace jobs
