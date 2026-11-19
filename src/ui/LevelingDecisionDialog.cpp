#include "ui/LevelingDecisionDialog.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s);
}
QString actionLabel(const std::string &s) {
    if (s == "adopt")
        return "채택";
    if (s == "release")
        return "해제";
    if (s == "invalidate")
        return "입력 변경으로 무효화";
    return s == "undo" ? "실행 취소" : "다시 실행";
}
QString scope(const std::optional<LevelingSelection> &plan, const std::optional<LevelingSelection> &remaining) {
    return plan ? "계획" : remaining ? "잔여 예측" : "논리 일정";
}
QString settings(const std::optional<LevelingSelection> &plan, const std::optional<LevelingSelection> &remaining) {
    const auto &s = plan ? plan : remaining;
    if (!s)
        return "논리 일정";
    const QString priority = s->priority == "task-order"       ? "작업 순서"
                             : s->priority == "shortest-first" ? "짧은 작업 우선"
                                                               : "긴 작업 우선";
    return scope(plan, remaining) + " · " + priority + " · 탐색 " + QString::number(s->horizonWorkDays) +
           " 근무일\n정책: " + q(s->policy) + "\n채택 시각 (UTC): " + q(s->adoptedAt) + "\n채택 사유: " + q(s->reason) +
           "\n채택 원본: " + q(s->sourceDocumentId) + " / revision " + QString::number(s->sourceRevision);
}
class DecisionModel final : public QAbstractTableModel {
public:
    explicit DecisionModel(QObject *parent) : QAbstractTableModel(parent) {}
    void reset(std::shared_ptr<const Project> p) {
        beginResetModel();
        project = std::move(p);
        endResetModel();
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return !parent.isValid() && project ? int(project->levelingDecisions.size()) : 0;
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 6;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Vertical)
            return section + 1;
        const QStringList headers{"기록 시각 (UTC)", "동작", "일정 전환", "사유", "원본 문서", "Revision 전환"};
        return headers.value(section);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() ||
            (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        const auto &e = project->levelingDecisions[std::size_t(index.row())];
        switch (index.column()) {
        case 0:
            return q(e.recordedAt);
        case 1:
            return actionLabel(e.action);
        case 2:
            return scope(e.beforePlan, e.beforeRemaining) + " → " + scope(e.afterPlan, e.afterRemaining);
        case 3:
            return q(e.reason);
        case 4:
            return q(e.sourceDocumentId);
        case 5:
            return QString::number(e.sourceRevision) + " → " + QString::number(e.recordedRevision);
        default:
            return {};
        }
    }
    std::shared_ptr<const Project> project;
};
} // namespace
LevelingDecisionDialog::LevelingDecisionDialog(Document &document, QWidget *parent)
    : QDialog(parent), document_(document) {
    setObjectName("levelingDecisionDialog");
    setWindowTitle("평준화 결정 이력");
    resize(1060, 680);
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("채택·해제·자동 무효화와 실행 취소/다시 실행을 기록 순서로 보존합니다. "
                            "행을 선택하면 변경 전후의 설정과 출처를 확인할 수 있습니다.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *summary = new QLabel(this);
    summary->setObjectName("levelingDecisionSummary");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    auto *table = new QTableView(splitter);
    table->setObjectName("levelingDecisionTable");
    auto *model = new DecisionModel(table);
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const int widths[]{180, 160, 180, 250, 240, 130};
    for (int c = 0; c < 6; ++c)
        table->setColumnWidth(c, widths[c]);
    auto *detail = new QPlainTextEdit(splitter);
    detail->setObjectName("levelingDecisionDetail");
    detail->setReadOnly(true);
    splitter->setSizes({320, 250});
    layout->addWidget(splitter, 1);
    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [model, detail](const QModelIndex &index) {
                if (!index.isValid() || !model->project) {
                    detail->clear();
                    return;
                }
                const auto &e = model->project->levelingDecisions.at(std::size_t(index.row()));
                detail->setPlainText(actionLabel(e.action) + " · " + q(e.recordedAt) + " (UTC)\n사유: " + q(e.reason) +
                                     "\n결정 ID: " + q(e.id) + "\n이전 결정 ID: " + q(e.previousEntryId) +
                                     "\n원본 문서: " + q(e.sourceDocumentId) + " / revision " +
                                     QString::number(e.sourceRevision) + " → " + QString::number(e.recordedRevision) +
                                     "\n\n변경 전 (상태일: " + (e.beforeStatusAt ? q(*e.beforeStatusAt) : "없음") +
                                     ")\n" + settings(e.beforePlan, e.beforeRemaining) +
                                     "\n\n변경 후 (상태일: " + (e.afterStatusAt ? q(*e.afterStatusAt) : "없음") +
                                     ")\n" + settings(e.afterPlan, e.afterRemaining));
            });
    const auto refresh = [this, model, table, detail, summary] {
        model->reset(document_.snapshot());
        detail->clear();
        summary->setText(model->rowCount()
                             ? "기록 " + QString::number(model->rowCount()) + "건 · " + q(model->project->name)
                             : "기록이 없습니다. 이전 버전의 채택은 다음 설정 변경부터 기록합니다.");
        if (model->rowCount()) {
            table->setCurrentIndex(model->index(model->rowCount() - 1, 0));
            table->scrollToBottom();
        }
    };
    subscription_ = document_.subscribe([refresh](Document::Event) { refresh(); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refresh();
    prepareEditorDialog(*this);
}
LevelingDecisionDialog::~LevelingDecisionDialog() {
    document_.unsubscribe(subscription_);
}
} // namespace ppm
