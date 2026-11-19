#pragma once
#include "application/Document.h"
#include "application/EditCommand.h"
#include "calculation/ProgressPolicies.h"
#include <QDialog>
#include <functional>
class QLabel;
class QTableView;
class QPlainTextEdit;
class QPushButton;
namespace ppm {
class ProgressPolicyModel;
class ProgressPolicyDialog final : public QDialog {
public:
    ProgressPolicyDialog(Document &, std::string selected = {}, QWidget *parent = nullptr);
    ~ProgressPolicyDialog() override;
    void reject() override;
    std::function<void(const std::string &)> taskActivated;
    using Submit = std::function<std::shared_ptr<CalculationControl>(EditCommand,
                                                                     std::function<void(EditState, const QString &)>)>;
    void useEditor(Submit submit) {
        submit_ = std::move(submit);
        updateActions();
    }

private:
    Document &document_;
    std::string selected_;
    std::uint64_t listener_ = 0, generation_ = 0, session_ = 0;
    std::shared_ptr<CalculationControl> control_;
    std::shared_ptr<const ProgressPolicyComparison> result_;
    QLabel *summary_;
    QTableView *tasks_, *relations_;
    ProgressPolicyModel *tasksModel_, *relationsModel_;
    QPlainTextEdit *details_, *diagnostics_;
    QPushButton *cancel_, *export_, *jump_, *adoptA_, *adoptB_, *adoptC_, *clear_;
    Submit submit_;
    bool posting_ = false;
    void updateActions();
    void applyPolicy(const char *policy);
    bool current() const;
    void invalidate(const QString &);
    void recalculate();
    void display();
    void selected();
    void save();
};
} // namespace ppm
