#include "ui/XmlBaselineDialog.h"
#include "ui/ExchangeOperation.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/XmlBaselines.h"
#include <QAbstractTableModel>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <map>
namespace ppm {
namespace {
class BaselinePreview final : public QAbstractTableModel {
    const ProjectXmlExchange &input_;
    std::map<std::string, QString> names_;

public:
    BaselinePreview(const ProjectXmlExchange &input, QObject *parent) : QAbstractTableModel(parent), input_(input) {
        const auto &p = input.project;
        names_[xmlBaselineRowKey("project", "")] = QString::fromStdString(p.name);
        for (const auto &t : p.tasks)
            names_[xmlBaselineRowKey("task", t.id)] = QString::fromStdString(t.name);
        for (const auto &g : p.wbsNodes)
            names_[xmlBaselineRowKey("wbs", g.id)] = QString::fromStdString(g.name);
        for (const auto &r : p.resources)
            names_[xmlBaselineRowKey("resource", r.id)] = QString::fromStdString(r.name);
        for (const auto &a : p.assignments)
            names_[xmlBaselineRowKey("assignment", a.taskId, a.resourceId)] =
                names_[xmlBaselineRowKey("task", a.taskId)] + " / " +
                names_[xmlBaselineRowKey("resource", a.resourceId)];
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(input_.baselineCandidates.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 11;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Vertical)
            return section + 1;
        const QStringList labels = {"번호",     "범위",     "원본 UID", "새 객체 ID", "시작", "완료",
                                    "기간(분)", "공수(분)", "원가",     "시간분포",   "검토"};
        return labels.value(section);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        const auto &c = input_.baselineCandidates.at(std::size_t(index.row()));
        const auto &r = c.row;
        const auto text = [](const auto &v) { return v ? QString::fromStdString(*v) : QString("미설정"); };
        switch (index.column()) {
        case 0:
            return r.number;
        case 1:
            return r.kind == "task"         ? QString("작업")
                   : r.kind == "wbs"        ? QString("WBS 요약")
                   : r.kind == "resource"   ? QString("자원")
                   : r.kind == "assignment" ? QString("배정")
                                            : QString("프로젝트 요약");
        case 2:
            return QString::fromStdString(r.xmlUid);
        case 3: {
            const auto name = names_.find(xmlBaselineRowKey(r.kind, r.id, r.resourceId));
            const auto id = QString::fromStdString(r.id + (r.resourceId.empty() ? "" : " / " + r.resourceId));
            return (name == names_.end() ? QString{} : name->second) + (id.isEmpty() ? QString{} : " [" + id + "]");
        }
        case 4:
            return text(r.start);
        case 5:
            return text(r.finish);
        case 6:
            return r.duration ? QString::fromStdString(formatFieldNumber(*r.duration)) : QString("미설정");
        case 7:
            return r.work ? QString::fromStdString(formatFieldNumber(*r.work)) : QString("미설정");
        case 8:
            return r.cost ? QString::fromStdString(formatFieldCost(*r.cost, input_.project.currency) + " " +
                                                   input_.project.currency)
                          : QString("미설정");
        case 9:
            return int(r.timephased.size());
        case 10:
            return c.issue.empty() ? QString("명시 값") : QString::fromStdString(c.issue);
        }
        return {};
    }
};
} // namespace
std::optional<ProjectXmlExchange> reviewXmlBaselines(QWidget *parent, Document &document,
                                                     const ProjectXmlExchange &input) {
    const auto session = document.sessionGeneration(), revision = document.project() ? document.project()->revision : 0;
    QDialog dialog(parent);
    dialog.setObjectName("xmlBaselineDialog");
    dialog.setWindowTitle("XML 기준선 · 번호별 채택 검토");
    dialog.resize(820, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        "선택한 번호의 작업·요약·자원·배정 기준선 값을 새 사본에 함께 보존합니다. 명시0과 미설정을 구분하며 현재 "
        "계획을 덮어쓰지 않습니다. 채택 번호의 수식은 외부 값을 사용하고 native 승인 이력은 별도로 유지합니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    std::map<int, std::pair<int, QString>> slotSummary;
    for (const auto &c : input.baselineCandidates) {
        auto &s = slotSummary[c.row.number];
        ++s.first;
        if (!c.issue.empty() && s.second.isEmpty())
            s.second = QString::fromStdString(c.issue);
    }
    auto *table = new QTableWidget(int(slotSummary.size()), 4);
    table->setObjectName("xmlBaselineSlots");
    table->setHorizontalHeaderLabels({"선택", "기준선 번호", "객체 수", "검토"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    int index = 0;
    for (const auto &[slot, state] : slotSummary) {
        auto *select = new QTableWidgetItem;
        select->setFlags(state.second.isEmpty() && slot >= 0 ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable
                                                             : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        select->setData(Qt::UserRole, slot);
        table->setItem(index, 0, select);
        table->setItem(index, 1, new QTableWidgetItem("Baseline" + QString::number(slot)));
        table->setItem(index, 2, new QTableWidgetItem(QString::number(state.first)));
        table->setItem(index, 3,
                       new QTableWidgetItem(state.second.isEmpty() ? "명시 값 보존 · 번호 전체 채택" : state.second));
        ++index;
    }
    auto *form = new QFormLayout;
    auto *detail = new QTableView;
    detail->setObjectName("xmlBaselineValues");
    detail->setModel(new BaselinePreview(input, detail));
    detail->setSelectionBehavior(QAbstractItemView::SelectRows);
    detail->setMinimumHeight(180);
    detail->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    layout->addWidget(detail);
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlBaselineReporter");
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlBaselineReason");
    form->addRow("검토자", reporter);
    form->addRow("채택 사유", reason);
    layout->addLayout(form);
    auto *error = new QLabel;
    error->setObjectName("xmlBaselineError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 번호 채택");
    layout->addWidget(buttons);
    std::optional<ProjectXmlExchange> result;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (reporter->text().trimmed().isEmpty() || reason->text().trimmed().isEmpty()) {
            error->setText("검토자와 채택 사유를 입력하세요.");
            return;
        }
        XmlBaselineOptions options;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->checkState() == Qt::Checked)
                options.numbers.push_back(table->item(row, 0)->data(Qt::UserRole).toInt());
        if (options.numbers.empty()) {
            error->setText("채택할 번호를 선택하세요.");
            return;
        }
        options.reporter = reporter->text().trimmed().toStdString();
        options.reason = reason->text().trimmed().toStdString();
        options.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
        options.confirmed = true;
        try {
            auto output = prepareExchange<ProjectXmlExchange>(&dialog, document, "XML 기준선 원본 대조·채택 중…",
                                                              [&] { return adoptXmlBaselines(input, options); });
            if (!output)
                return;
            if (session != document.sessionGeneration() ||
                revision != (document.project() ? document.project()->revision : 0))
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
            result = std::move(*output);
            dialog.accept();
        } catch (const AppError &e) {
            error->setText(
                QString::fromStdString(e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments.front()));
        }
    });
    prepareEditorDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return result;
}
} // namespace ppm
