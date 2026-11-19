#pragma once
#include "application/Document.h"
#include "domain/ResourceView.h"
#include <QDialog>
#include <array>
class QComboBox;
class QLineEdit;
class QCheckBox;
class QTableWidget;
class QTreeWidget;
class QLabel;
class QPushButton;
class QTimer;
namespace ppm {
class ResourceViewDialog final : public QDialog {
public:
    explicit ResourceViewDialog(Document &, QWidget *parent = nullptr);
    ~ResourceViewDialog() override;
    void reject() override;

private:
    Document &doc_;
    std::shared_ptr<const Project> source_;
    std::uint64_t session_ = 0, listener_ = 0, generation_ = 0;
    bool committing_ = false, loading_ = false;
    QComboBox *saved_, *sort_;
    std::array<QComboBox *, 3> groups_;
    QLineEdit *query_, *name_;
    QCheckBox *descending_;
    QTableWidget *filters_;
    QTreeWidget *tree_;
    QLabel *message_;
    QPushButton *save_, *remove_, *xlsx_, *pdf_;
    QTimer *timer_;
    std::shared_ptr<CalculationControl> control_;
    std::optional<ResourceViewSettings> displayed_;
    bool current() const;
    ResourceViewSettings read() const;
    void fill(const ResourceViewSettings &);
    void reload();
    void savedChoices(const std::string &id = {});
    void addFilter(const AttributeFilter &filter = {"name", "contains", ""});
    void changed();
    void preview();
    void controls();
    void exportView(bool pdf);
};
} // namespace ppm
