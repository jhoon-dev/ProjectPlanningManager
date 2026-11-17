#include "ui/XerProjectDialog.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
namespace jobs {
namespace {
class Projects final : public QAbstractTableModel {
    const std::vector<XerProjectInfo> &projects_;

public:
    Projects(const std::vector<XerProjectInfo> &projects, QObject *parent)
        : QAbstractTableModel(parent), projects_(projects) {}
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(projects_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 7;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            const QStringList headers{"원본 ID", "프로젝트 코드", "프로젝트 이름", "시작일",
                                      "작업",    "외부 선행",     "외부 후행"};
            return headers.value(section);
        }
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || std::size_t(index.row()) >= projects_.size())
            return {};
        const auto &p = projects_[std::size_t(index.row())];
        if (role == Qt::UserRole)
            return p.id;
        if (role == Qt::UserRole + 1)
            return p.externalIncoming != 0 || p.externalOutgoing != 0;
        if (role == Qt::UserRole + 2)
            return p.id + "\n" + p.code + "\n" + p.name;
        if (role == Qt::ToolTipRole)
            return "<qt>" + data(index, Qt::DisplayRole).toString().toHtmlEscaped() + "</qt>";
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return p.id;
        case 1:
            return p.code;
        case 2:
            return p.name;
        case 3:
            return p.start;
        case 4:
            return qulonglong(p.tasks);
        case 5:
            return qulonglong(p.externalIncoming);
        case 6:
            return qulonglong(p.externalOutgoing);
        default:
            return {};
        }
    }
};
} // namespace
std::optional<QString> chooseXerProject(QWidget *parent, const std::vector<XerProjectInfo> &projects) {
    QDialog dialog(parent);
    dialog.setObjectName("xerProjectDialog");
    dialog.setWindowTitle("XER에서 가져올 프로젝트 선택");
    dialog.resize(1050, 560);
    auto *body = new QVBoxLayout(&dialog);
    auto *note =
        new QLabel(QString("원본의 %1개 프로젝트 중 하나를 선택하세요. 나머지 프로젝트는 변환하지 않습니다.\n"
                           "공유 자원은 선택 배정에 필요한 항목만 가져옵니다. 원본 전체는 original.xer에 보관합니다.")
                       .arg(projects.size()),
                   &dialog);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    body->addWidget(note);
    auto *search = new QLineEdit(&dialog);
    search->setObjectName("xerProjectSearch");
    search->setPlaceholderText("프로젝트 ID / 코드 / 이름 검색");
    search->setAccessibleName("가져올 XER 프로젝트 검색");
    body->addWidget(search);
    Projects model(projects, &dialog);
    QSortFilterProxyModel proxy(&dialog);
    proxy.setSourceModel(&model);
    proxy.setFilterKeyColumn(0);
    proxy.setFilterRole(Qt::UserRole + 2);
    proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
    auto *table = new QTableView(&dialog);
    table->setObjectName("xerProjects");
    table->setModel(&proxy);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->setColumnWidth(0, 70);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(2, 160);
    table->setColumnWidth(3, 125);
    table->setColumnWidth(4, 45);
    table->setColumnWidth(5, 85);
    table->setColumnWidth(6, 85);
    table->horizontalHeader()->setStretchLastSection(true);
    body->addWidget(table, 1);
    auto *status = new QLabel("프로젝트를 선택하면 외부 관계를 확인합니다.", &dialog);
    status->setObjectName("xerProjectStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    body->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 프로젝트 변환 검토");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    body->addWidget(buttons);
    QString selected;
    const auto update = [&] {
        const auto rows = table->selectionModel()->selectedRows();
        const bool chosen = rows.size() == 1;
        const bool blocked = chosen && rows.front().data(Qt::UserRole + 1).toBool();
        selected = chosen ? rows.front().data(Qt::UserRole).toString() : QString{};
        buttons->button(QDialogButtonBox::Ok)->setEnabled(chosen && !blocked);
        status->setText(!chosen ? "프로젝트를 선택하세요."
                        : blocked
                            ? "외부 선행/후행 관계가 있어 가져올 수 없습니다. 관계를 제거하거나 무시하지 않습니다."
                            : "선택 ID " + selected + " · 다음 단계에서 지원 범위와 변환 손실을 검토합니다.");
    };
    QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, update);
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &text) {
        table->clearSelection();
        proxy.setFilterFixedString(text);
        update();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        update();
        if (!selected.isEmpty() && buttons->button(QDialogButtonBox::Ok)->isEnabled())
            dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    prepareEditorDialog(dialog);
    // Keep the first project unselected, including when the view receives keyboard focus.
    table->clearSelection();
    search->setFocus(Qt::OtherFocusReason);
    return dialog.exec() == QDialog::Accepted ? std::optional<QString>(selected) : std::nullopt;
}
} // namespace jobs
