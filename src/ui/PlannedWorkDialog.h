#pragma once
#include "application/Document.h"
#include "calculation/PlannedWork.h"
#include <QDialog>
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
namespace ppm {
class PlannedWorkDialog final : public QDialog {
public:
    PlannedWorkDialog(Document &, std::shared_ptr<const Project>, std::string taskId, std::string resourceId,
                      QWidget *parent = nullptr);
    ~PlannedWorkDialog() override;
    void reject() override;
    const std::optional<PlannedWorkResult> &converted() const {
        return result_;
    }

private:
    Document &doc_;
    std::shared_ptr<const Project> source_;
    std::string taskId_, resourceId_;
    std::uint64_t session_, listener_ = 0, generation_ = 0;
    QComboBox *period_;
    QTableWidget *table_;
    QLabel *message_;
    QPushButton *convert_, *use_, *cancel_, *reload_;
    std::shared_ptr<CalculationControl> control_;
    std::optional<PlannedWorkResult> basis_, result_;
    bool busy_ = false;
    bool current() const;
    void controls();
    void run(bool convert);
};
} // namespace ppm
