#include "ui/PeriodActualDialog.h"
#include "ui/EditorUi.h"
#include "domain/PeriodActuals.h"
#include "formatting/Money.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/DiagnosticText.h"
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <set>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
QString hours(const std::optional<std::int64_t> &n) {
    return n ? QString::number(*n) + "u" : "";
}
QString cell(QTableWidget &t, int row, int column) {
    return t.item(row, column) ? t.item(row, column)->text().trimmed() : QString{};
}
class ActualHistoryModel final : public QAbstractTableModel {
    std::shared_ptr<const Project> source_;
    std::vector<std::size_t> rows_;

public:
    ActualHistoryModel(std::shared_ptr<const Project> source, const std::string &task, QObject *parent)
        : QAbstractTableModel(parent), source_(std::move(source)) {
        for (std::size_t i = source_->periodActuals.size(); i-- > 0;)
            if (source_->periodActuals[i].taskId == task)
                rows_.push_back(i);
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex & = {}) const override {
        return 10;
    }
    QVariant data(const QModelIndex &i, int role = Qt::DisplayRole) const override {
        if (!i.isValid() || std::size_t(i.row()) >= rows_.size())
            return {};
        const auto &e = source_->periodActuals[rows_[std::size_t(i.row())]];
        if (role == Qt::UserRole)
            return qulonglong(rows_[std::size_t(i.row())]);
        if (role == Qt::ToolTipRole)
            return "누계 보고 ID: " + qs(e.costEntryId) + "\n원본 " + qs(e.sourceDocumentId) + " rev " +
                   QString::number(e.recordedRevision);
        if (role != Qt::DisplayRole)
            return {};
        const QStringList values{qs(e.date),
                                 qs(e.resourceId.empty() ? "작업 직접비" : e.resourceId + " / " + e.resourceType),
                                 e.workUnitMinutes ? QString::number(double(*e.workUnitMinutes) / 6000, 'f', 6)
                                                   : "미보고",
                                 moneyui::format(e.actualMinor, source_->currency, "미보고"),
                                 qs(e.statusAt),
                                 qs(e.reporter),
                                 qs(e.reason),
                                 qs(e.recordedAt),
                                 qs(e.id),
                                 qs(e.previousEntryId)};
        return values.value(i.column());
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
        const QStringList labels{"실제 날짜", "자원/종류", "공수 h",   "배분 원가", "상태일",
                                 "작성자",    "사유",      "기록 UTC", "보고 ID",   "이전 보고 ID"};
        return orientation == Qt::Horizontal && role == Qt::DisplayRole ? QVariant(labels.value(section)) : QVariant{};
    }
};
} // namespace
PeriodActualDialog::PeriodActualDialog(Document &document, const std::string &task, QWidget *parent)
    : QDialog(parent), document_(document) {
    setObjectName("periodActualDialog");
    setWindowTitle("기간별 실제공수·실제원가 입력 / 정정");
    if (document_.project())
        source_ = std::make_shared<const Project>(*document_.project());
    sourceSession_ = document_.sessionGeneration();
    auto *body = new QVBoxLayout(this);
    auto *note = new QLabel(
        "기간 원가는 기존 누적 실제원가에 포함되는 배분액입니다. 원가를 입력하면 아래 누계도 함께 제출합니다.\n"
        "기존 누계 80에 30을 배분: 누계 80 / 미배분 50. 신규 지출 10을 더했다면 누계도 90으로 입력하세요.\n"
        "공수는 시간(h) 또는 정수 %·분(u): 예 1.25 / 6001u. 빈칸은 미보고, 0은 명시 0입니다.\n"
        "제출은 일반 실행 취소로 삭제하지 않습니다. 이력에서 행을 불러와 정정하세요. 실제공수는 잔여 일정을 자동 "
        "변경하지 않습니다.",
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    body->addWidget(note);
    task_ = new QComboBox(this);
    task_->setObjectName("actualTask");
    if (source_)
        for (const auto &t : source_->tasks)
            task_->addItem(qs(t.name), qs(t.id));
    task_->setCurrentIndex(std::max(0, task_->findData(qs(task))));
    body->addWidget(task_);
    balance_ = new QLabel(this);
    balance_->setObjectName("actualBalance");
    balance_->setWordWrap(true);
    balance_->setTextFormat(Qt::PlainText);
    body->addWidget(balance_);
    entries_ = new QTableWidget(0, 4, this);
    entries_->setObjectName("periodActualRows");
    entries_->setHorizontalHeaderLabels(
        {"실제 날짜 YYYY-MM-DD", "자원 / 작업 직접비", "실제공수 h 또는 u", "배분 실제원가"});
    entries_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    entries_->setSelectionBehavior(QAbstractItemView::SelectRows);
    body->addWidget(entries_, 1);
    auto *rowActions = new QHBoxLayout;
    add_ = new QPushButton("행 추가", this);
    add_->setObjectName("addActualRow");
    remove_ = new QPushButton("선택 초안 행 제거", this);
    remove_->setObjectName("removeActualRow");
    rowActions->addWidget(add_);
    rowActions->addWidget(remove_);
    rowActions->addStretch();
    body->addLayout(rowActions);
    auto *form = new QFormLayout;
    total_ = new QLineEdit(this);
    total_->setObjectName("actualCumulative");
    total_->setPlaceholderText("기존 금액을 포함한 누계 · 빈칸은 미정");
    reporter_ = new QLineEdit(this);
    reporter_->setObjectName("actualReporter");
    reason_ = new QLineEdit(this);
    reason_->setObjectName("actualReason");
    form->addRow("누적 실제원가 총계 (배분액 포함)", total_);
    form->addRow("작성자", reporter_);
    form->addRow("제출 / 정정 사유", reason_);
    body->addLayout(form);
    auto *historyLabel =
        new QLabel("제출 원장 · 행을 선택해 정정 초안으로 불러오기 (최신 상태일 이하의 값만 Usage에 반영)", this);
    historyLabel->setWordWrap(true);
    body->addWidget(historyLabel);
    history_ = new QTableView(this);
    history_->setObjectName("periodActualHistory");
    resourceChoices_ = new QStandardItemModel(this);
    history_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history_->setSelectionBehavior(QAbstractItemView::SelectRows);
    history_->setSelectionMode(QAbstractItemView::SingleSelection);
    history_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    body->addWidget(history_, 1);
    correct_ = new QPushButton("선택 기록을 정정 초안으로 불러오기", this);
    correct_->setObjectName("loadActualCorrection");
    body->addWidget(correct_);
    message_ = new QLabel(this);
    message_->setObjectName("periodActualError");
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    body->addWidget(message_);
    auto *actions = new QHBoxLayout;
    submit_ = new QPushButton("기간 실적 제출", this);
    submit_->setObjectName("submitPeriodActuals");
    cancel_ = new QPushButton("제출 준비 취소", this);
    cancel_->setObjectName("cancelPeriodActuals");
    actions->addWidget(submit_);
    actions->addWidget(cancel_);
    actions->addStretch();
    auto *close = new QPushButton("닫기", this);
    close->setObjectName("closePeriodActuals");
    actions->addWidget(close);
    body->addLayout(actions);
    connect(close, &QPushButton::clicked, this, &PeriodActualDialog::reject);
    connect(task_, &QComboBox::currentIndexChanged, this, [this] { loadTask(); });
    connect(add_, &QPushButton::clicked, this, [this] { addRow(); });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (entries_->currentRow() >= 0)
            entries_->removeRow(entries_->currentRow());
    });
    connect(correct_, &QPushButton::clicked, this, [this] {
        if (!source_ || !history_->currentIndex().isValid())
            return;
        const auto i = history_->currentIndex().data(Qt::UserRole).toULongLong();
        entries_->setRowCount(0);
        addRow(&source_->periodActuals.at(std::size_t(i)));
    });
    connect(submit_, &QPushButton::clicked, this, [this] { submit(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    listener_ = document_.subscribe([this](Document::Event) {
        if (committing_ || current())
            return;
        if (control_)
            control_->cancel();
        message_->setText("문서/저장 상태가 변경되었습니다. 창을 다시 열어 최신 입력으로 제출하세요.");
        controls();
    });
    loadTask();
    resize(1120, 860);
    prepareEditorDialog(*this);
}
PeriodActualDialog::~PeriodActualDialog() {
    if (control_)
        control_->cancel();
    document_.unsubscribe(listener_);
}
bool PeriodActualDialog::current() const {
    return source_ && document_.project() && !document_.readOnly() && document_.sessionGeneration() == sourceSession_ &&
           document_.project()->documentId == source_->documentId && document_.project()->revision == source_->revision;
}
void PeriodActualDialog::controls() {
    const bool editable = current() && source_->statusAt && !busy_;
    for (auto *w :
         std::initializer_list<QWidget *>{entries_, add_, remove_, correct_, total_, reporter_, reason_, submit_})
        w->setEnabled(editable);
    task_->setEnabled(!busy_);
    cancel_->setEnabled(busy_);
}
void PeriodActualDialog::addRow(const PeriodActualEntry *entry) {
    if (!source_ || entries_->rowCount() >= 1000)
        return;
    const int row = entries_->rowCount();
    entries_->insertRow(row);
    auto *resources = new QComboBox(entries_);
    resources->setModel(resourceChoices_);
    if (entry)
        resources->setCurrentIndex(std::max(0, resources->findData(qs(entry->resourceId))));
    else if (resources->count() > 1)
        resources->setCurrentIndex(1);
    entries_->setCellWidget(row, 1, resources);
    entries_->setItem(row, 0,
                      new QTableWidgetItem(entry ? qs(entry->date)
                                                 : qs(source_->statusAt.value_or(source_->startDate).substr(0, 10))));
    entries_->setItem(row, 2, new QTableWidgetItem(entry ? hours(entry->workUnitMinutes) : ""));
    entries_->setItem(row, 3,
                      new QTableWidgetItem(entry ? moneyui::format(entry->actualMinor, source_->currency, "") : ""));
}
void PeriodActualDialog::loadTask() {
    entries_->setRowCount(0);
    resourceChoices_->clear();
    if (!source_) {
        controls();
        return;
    }
    const auto task = str(task_->currentData().toString());
    std::set<std::string> assigned;
    for (const auto &a : source_->assignments)
        if (a.taskId == task)
            assigned.insert(a.resourceId);
    for (const auto &e : source_->periodActuals)
        if (e.taskId == task && !e.resourceId.empty())
            assigned.insert(e.resourceId);
    const auto choice = [&](const QString &name, const QString &id) {
        auto *item = new QStandardItem(name);
        item->setData(id, Qt::UserRole);
        resourceChoices_->appendRow(item);
    };
    choice("작업 직접비 / 자원 미배분", QString{});
    for (const auto &r : source_->resources)
        if (assigned.contains(r.id))
            choice(qs(r.name) + " · " + qs(r.id), qs(r.id));
    const auto allocation = source_->statusAt ? actualCostAllocations(*source_, *source_->statusAt)
                                              : std::map<std::string, ActualCostAllocation>{};
    const auto a = allocation.contains(task) ? allocation.at(task) : ActualCostAllocation{};
    total_->setText(moneyui::format(a.cumulative, source_->currency, ""));
    balance_->setText("상태일 " + qs(source_->statusAt.value_or("미설정 · 먼저 상태일을 설정하세요")) + " · 통화 " +
                      qs(source_->currency) + " · 누계 " + moneyui::format(a.cumulative, source_->currency) +
                      " · 전기간 배분 " + moneyui::format(a.allocated, source_->currency) + " · 미배분 " +
                      moneyui::format(a.unallocated, source_->currency));
    auto *oldModel = history_->model();
    history_->setModel(new ActualHistoryModel(source_, task, history_));
    if (oldModel)
        oldModel->deleteLater();
    addRow();
    controls();
}
void PeriodActualDialog::submit() {
    try {
        if (!current() || busy_ || !source_->statusAt)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        PeriodActualSubmission s;
        s.reporter = str(reporter_->text().trimmed());
        s.reason = str(reason_->text().trimmed());
        s.recordedAt = str(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"));
        const auto task = str(task_->currentData().toString());
        bool costs = false;
        std::map<PeriodActualKey, const PeriodActualEntry *> old;
        for (const auto &e : source_->periodActuals)
            old[periodActualKey(e)] = &e;
        for (int row = 0; row < entries_->rowCount(); ++row) {
            PeriodActualEntry e;
            e.taskId = task;
            e.date = str(cell(*entries_, row, 0));
            e.resourceId = str(qobject_cast<QComboBox *>(entries_->cellWidget(row, 1))->currentData().toString());
            const auto w = cell(*entries_, row, 2);
            if (!w.isEmpty())
                e.workUnitMinutes = parseActualHours(str(w));
            e.actualMinor = moneyui::parse(cell(*entries_, row, 3), source_->currency);
            costs = costs || e.actualMinor.has_value() ||
                    (old.contains(periodActualKey(e)) && old.at(periodActualKey(e))->actualMinor.has_value());
            s.entries.push_back(std::move(e));
        }
        if (costs)
            s.totals.push_back({task, moneyui::parse(total_->text(), source_->currency)});
        auto request = document_.makeEditRequest(PeriodActualEdit{std::move(s)});
        control_ = std::make_shared<CalculationControl>();
        busy_ = true;
        controls();
        message_->setText("기간 실적 검증·제출 준비 중…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>(this);
        connect(watcher, &QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>::finished, this, [this, watcher] {
            auto result = watcher->result();
            watcher->deleteLater();
            busy_ = false;
            committing_ = true;
            const auto state = document_.applyPreparedEdit(*result);
            committing_ = false;
            if (state == EditState::Applied) {
                source_ = std::make_shared<const Project>(*document_.project());
                sourceSession_ = document_.sessionGeneration();
                loadTask();
                message_->setText("기간 실적을 제출했습니다. 원본/누계 보고는 보존되며 정정은 새 기록으로 남습니다.");
            } else if (result->failure())
                showEditorError(*message_, std::runtime_error(diagnosticMessage(*result->failure())));
            else
                message_->setText(state == EditState::Cancelled
                                      ? "기간 실적 제출을 취소했습니다."
                                      : "문서가 변경되어 제출하지 않았습니다. 다시 열어 주세요.");
            controls();
        });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), control = control_]() mutable {
            return Document::prepareEdit(std::move(request), control);
        }));
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
    }
}
void PeriodActualDialog::reject() {
    if (control_)
        control_->cancel();
    QDialog::reject();
}
} // namespace ppm
