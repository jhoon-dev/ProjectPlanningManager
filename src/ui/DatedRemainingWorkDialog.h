#pragma once
#include "application/Document.h"
#include "calculation/DatedRemainingWork.h"
#include <QDialog>
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QDateEdit;
namespace ppm {
class DatedRemainingWorkDialog final : public QDialog {
public:
    DatedRemainingWorkDialog(Document &, const std::string &taskId = {}, QWidget *parent = nullptr);
    ~DatedRemainingWorkDialog() override;
    void reject() override;

private:
    Document &doc_;
    std::shared_ptr<const Project> source_;
    std::uint64_t session_ = 0, listener_ = 0, generation_ = 0;
    QComboBox *assignment_;
    QTableWidget *table_;
    QLabel *message_, *basisLabel_;
    QPushButton *preview_, *apply_, *cancel_, *clear_, *clearAll_, *addDate_;
    QDateEdit *date_;
    std::optional<RemainingWorkDescription> basis_;
    std::shared_ptr<Document::PreparedEdit> prepared_;
    std::shared_ptr<CalculationControl> control_;
    bool busy_ = false, committing_ = false;
    bool current() const;
    const Assignment *selected() const;
    void controls();
    void reload();
    void load();
    void changed();
    void prepare(int mode = 0);
    void commit();
};
} // namespace ppm
