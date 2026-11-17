#pragma once
#include "application/Document.h"
#include "calculation/Usage.h"
#include <QAbstractItemModel>
#include <QDialog>
class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QTreeView;
class QCloseEvent;
namespace jobs {
class UsageModel final : public QAbstractItemModel {
public:
    explicit UsageModel(QObject *parent = nullptr);
    void setUsage(std::shared_ptr<const UsageResult>, UsageHierarchy);
    void setMetric(int);
    QModelIndex index(int, int, const QModelIndex & = {}) const override;
    QModelIndex parent(const QModelIndex &) const override;
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;

private:
    std::shared_ptr<const UsageResult> result_;
    std::vector<UsageRow> rows_;
    std::vector<std::vector<std::size_t>> children_;
    std::vector<int> position_;
    int metric_ = 0;
};
class UsageDialog final : public QDialog {
public:
    UsageDialog(Document &, const std::string &wbs, const std::string &baseline, QWidget *parent = nullptr);
    ~UsageDialog() override;
    void reject() override;

protected:
    void closeEvent(QCloseEvent *) override;

private:
    Document &document_;
    QComboBox *scope_, *baseline_, *period_, *hierarchy_, *metric_;
    QDateEdit *from_, *to_;
    QLabel *message_, *diagnostics_;
    QPushButton *refresh_, *cancel_, *xlsx_, *pdf_;
    QTreeView *tree_;
    UsageModel *model_;
    std::shared_ptr<const UsageResult> result_;
    std::shared_ptr<CalculationControl> control_;
    std::uint64_t generation_ = 0, sourceSession_ = 0, listener_ = 0;
    bool exporting_ = false, sourceUnsaved_ = false;
    void prepare();
    void showResult();
    bool current() const;
    void exportFile(bool pdf);
};
} // namespace jobs
