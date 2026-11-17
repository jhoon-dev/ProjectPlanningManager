#include "ui/ProgressPolicyDialog.h"
#include "persistence/ProgressPolicyJson.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "ui/ExchangeOperation.h"
#include "ui/SandboxAccess.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPointer>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <unordered_map>
namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QString stamp(const std::optional<std::string> &s) {
    return s ? qs(*s) : "—";
}
QString hours(const std::optional<std::int64_t> &n) {
    return n ? QString::number(double(*n) / 60, 'g', 10) : "—";
}
QString state(const ScheduleResult &s) {
    return s.status == CalculationStatus::Valid ? "유효" : s.status == CalculationStatus::Conflict ? "충돌" : "미산정";
}
QString flag(const ScheduleResult &r, const std::optional<TaskSchedule> &row) {
    return !row                        ? "미산정"
           : row->completedActual      ? "완료 실적"
           : hasScheduleFloat(r, *row) ? (row->critical ? "임계" : "여유")
                                       : "미산정";
}
QString floats(const ScheduleResult &r, const std::optional<TaskSchedule> &row, bool free) {
    return row && hasScheduleFloat(r, *row) ? hours(free ? row->freeFloatMinutes : row->totalFloatMinutes) : "—";
}
class Changes final : public QSortFilterProxyModel {
    bool changed_ = false;

public:
    explicit Changes(QObject *parent) : QSortFilterProxyModel(parent) {
        setFilterRole(Qt::UserRole + 1);
        setFilterKeyColumn(0);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
    }
    void changedOnly(bool value) {
        changed_ = value;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        return (!changed_ || sourceModel()->index(row, 0, parent).data(Qt::UserRole + 2).toBool()) &&
               QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};
struct Prepared {
    std::shared_ptr<const ProgressPolicyComparison> result;
    QString error;
    double milliseconds = 0;
};
} // namespace
class ProgressPolicyModel final : public QAbstractTableModel {
    std::shared_ptr<const ProgressPolicyComparison> report_;
    bool relations_;
    std::unordered_map<std::string, QString> names_;

public:
    ProgressPolicyModel(bool relations, QObject *parent) : QAbstractTableModel(parent), relations_(relations) {}
    void set(std::shared_ptr<const ProgressPolicyComparison> result) {
        beginResetModel();
        report_ = std::move(result);
        names_.clear();
        if (report_)
            for (const auto &t : report_->tasks)
                names_.emplace(t.id, qs(t.name));
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() || !report_ ? 0 : int(relations_ ? report_->ignored.size() : report_->tasks.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : relations_ ? 5 : 21;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            const QStringList names =
                relations_
                    ? QStringList{"선행 작업 / ID", "진행 중 후행 / ID", "관계", "지연(분)", "B의 처리"}
                    : QStringList{"작업",         "A 잔여 재개",  "B 잔여 재개",   "A 완료",        "B 완료",
                                  "완료 편차(h)", "A 총여유(h)",  "B 총여유(h)",   "A 상태",        "B 상태",
                                  "실제 시작",    "실제 완료",    "A 자유여유(h)", "B 자유여유(h)", "작업 ID",
                                  "변경",         "재개 편차(h)", "C 잔여 재개",   "C 완료",        "C 총여유(h)",
                                  "C 상태"};
            return names.value(section);
        }
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!report_ || !i.isValid() || i.row() < 0 || i.row() >= rowCount())
            return {};
        if (role == Qt::ToolTipRole)
            return "<qt>" + data(i, Qt::DisplayRole).toString().toHtmlEscaped() + "</qt>";
        const auto index = std::size_t(i.row());
        if (relations_) {
            const auto &d = report_->ignored[index];
            if (role != Qt::DisplayRole)
                return {};
            switch (i.column()) {
            case 0:
                return names_.at(d.predecessorId) + " [" + qs(d.predecessorId) + "]";
            case 1:
                return names_.at(d.taskId) + " [" + qs(d.taskId) + "]";
            case 2:
                return qs(enumToken(d.type));
            case 3:
                return qlonglong(d.lagMinutes);
            case 4:
                return "잔여 계산의 수신 관계 비활성";
            default:
                return {};
            }
        }
        const auto &t = report_->tasks[index];
        if (role == Qt::UserRole)
            return qs(t.id);
        if (role == Qt::UserRole + 1)
            return qs(t.id) + "\n" + qs(t.name);
        if (role == Qt::UserRole + 2)
            return t.changed;
        if (role != Qt::DisplayRole)
            return {};
        switch (i.column()) {
        case 0:
            return qs(t.name);
        case 1:
            return t.retained ? stamp(t.retained->remainingStart) : "—";
        case 2:
            return t.overridden ? stamp(t.overridden->remainingStart) : "—";
        case 3:
            return t.retained ? qs(t.retained->finish) : "—";
        case 4:
            return t.overridden ? qs(t.overridden->finish) : "—";
        case 5:
            return hours(t.finishDeltaMinutes);
        case 6:
            return floats(report_->retained, t.retained, false);
        case 7:
            return floats(report_->overridden, t.overridden, false);
        case 8:
            return flag(report_->retained, t.retained);
        case 9:
            return flag(report_->overridden, t.overridden);
        case 10:
            return stamp(t.actualStart);
        case 11:
            return stamp(t.actualFinish);
        case 12:
            return floats(report_->retained, t.retained, true);
        case 13:
            return floats(report_->overridden, t.overridden, true);
        case 14:
            return qs(t.id);
        case 15:
            return !t.comparable ? "비교 불가" : t.changed ? "변경" : "동일";
        case 16:
            return hours(t.resumeDeltaMinutes);
        case 17:
            return t.actualDates ? stamp(t.actualDates->remainingStart) : "—";
        case 18:
            return t.actualDates ? qs(t.actualDates->finish) : "—";
        case 19:
            return floats(report_->actualDates, t.actualDates, false);
        case 20:
            return flag(report_->actualDates, t.actualDates);
        default:
            return {};
        }
    }
};
ProgressPolicyDialog::ProgressPolicyDialog(Document &doc, std::string initial, QWidget *parent)
    : QDialog(parent), document_(doc), selected_(std::move(initial)) {
    setObjectName("progressPolicyDialog");
    setWindowTitle("진척 정책 비교·채택 / P6 미검증");
    resize(1380, 850);
    auto *body = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    body->addLayout(controls);
    auto *refresh = new QPushButton("최신 입력으로 비교", this);
    refresh->setObjectName("refreshProgressPolicy");
    controls->addWidget(refresh);
    cancel_ = new QPushButton("취소", this);
    cancel_->setObjectName("cancelProgressPolicy");
    controls->addWidget(cancel_);
    auto *search = new QLineEdit(this);
    search->setObjectName("progressPolicySearch");
    search->setPlaceholderText("작업 ID / 이름 검색");
    controls->addWidget(search, 1);
    auto *changed = new QCheckBox("A/B 변경 작업만", this);
    changed->setObjectName("progressPolicyChangedOnly");
    controls->addWidget(changed);
    export_ = new QPushButton("비교 JSON 저장…", this);
    export_->setObjectName("exportProgressPolicy");
    controls->addWidget(export_);
    summary_ = new QLabel(this);
    summary_->setObjectName("progressPolicySummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(summary_);
    auto *tabs = new QTabWidget(this);
    body->addWidget(tabs, 1);
    tasksModel_ = new ProgressPolicyModel(false, this);
    auto *proxy = new Changes(this);
    proxy->setSourceModel(tasksModel_);
    tasks_ = new QTableView(this);
    tasks_->setObjectName("progressPolicyTasks");
    tasks_->setModel(proxy);
    tasks_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tasks_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasks_->setSelectionMode(QAbstractItemView::SingleSelection);
    tasks_->setWordWrap(false);
    tasks_->verticalHeader()->setDefaultSectionSize(25);
    for (int c = 0; c < 21; ++c)
        tasks_->setColumnWidth(c, c == 0 ? 170 : c < 5 ? 150 : 105);
    tabs->addTab(tasks_, "작업 A/B/C 비교");
    relationsModel_ = new ProgressPolicyModel(true, this);
    relations_ = new QTableView(this);
    relations_->setObjectName("progressPolicyIgnored");
    relations_->setModel(relationsModel_);
    relations_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    relations_->setWordWrap(false);
    relations_->setColumnWidth(0, 340);
    relations_->setColumnWidth(1, 340);
    relations_->setColumnWidth(4, 230);
    tabs->addTab(relations_, "B에서 비활성한 원본 관계");
    details_ = new QPlainTextEdit(this);
    details_->setObjectName("progressPolicyDetails");
    details_->setReadOnly(true);
    details_->setMaximumHeight(80);
    body->addWidget(details_);
    diagnostics_ = new QPlainTextEdit(this);
    diagnostics_->setObjectName("progressPolicyDiagnostics");
    diagnostics_->setReadOnly(true);
    diagnostics_->setMaximumHeight(125);
    body->addWidget(diagnostics_);
    auto *adoption = new QHBoxLayout;
    body->addLayout(adoption);
    adoptA_ = new QPushButton("A 잔여 논리 유지 채택…", this);
    adoptA_->setObjectName("adoptRetainedProgressPolicy");
    adoptB_ = new QPushButton("B 진행 중 수신 관계 무시 채택…", this);
    adoptB_->setObjectName("adoptOverrideProgressPolicy");
    clear_ = new QPushButton("진척 정책 채택 해제", this);
    clear_->setObjectName("clearProgressPolicy");
    adoption->addWidget(adoptA_);
    adoption->addWidget(adoptB_);
    adoptC_ = new QPushButton("C Actual Dates 채택…", this);
    adoptC_->setObjectName("adoptActualDatesProgressPolicy");
    adoption->addWidget(adoptC_);
    connect(adoptC_, &QPushButton::clicked, this, [this] { applyPolicy(actualDatesProgressPolicy); });
    adoption->addWidget(clear_);
    adoption->addStretch();
    connect(adoptA_, &QPushButton::clicked, this, [this] { applyPolicy(retainedProgressPolicy); });
    connect(adoptB_, &QPushButton::clicked, this, [this] { applyPolicy(overrideProgressPolicy); });
    connect(clear_, &QPushButton::clicked, this, [this] { applyPolicy(nullptr); });
    auto *bottom = new QHBoxLayout;
    body->addLayout(bottom);
    jump_ = new QPushButton("원본 표에서 찾기", this);
    jump_->setObjectName("jumpProgressPolicyTask");
    bottom->addWidget(jump_);
    auto *note = new QLabel("A: 기존 잔여 논리 유지 · B: 진행 중 수신 관계 무시 · 편차=B−A, 프로젝트 달력 근무시간 · "
                            "채택하면 표/간트에 적용 · 원본 관계/기준선 보존 · 정책 채택/해제 시 기존 평준화 채택 해제",
                            this);
    note->setWordWrap(true);
    bottom->addWidget(note, 1);
    auto *close = new QPushButton("닫기", this);
    bottom->addWidget(close);
    connect(close, &QPushButton::clicked, this, &ProgressPolicyDialog::reject);
    connect(refresh, &QPushButton::clicked, this, &ProgressPolicyDialog::recalculate);
    connect(cancel_, &QPushButton::clicked, this, [this] { invalidate("진척 정책 비교를 취소했습니다."); });
    connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(changed, &QCheckBox::toggled, proxy, &Changes::changedOnly);
    connect(tasks_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { selected(); });
    connect(jump_, &QPushButton::clicked, this, [this] {
        if (current() && !selected_.empty() && taskActivated)
            taskActivated(selected_);
    });
    connect(export_, &QPushButton::clicked, this, &ProgressPolicyDialog::save);
    listener_ = document_.subscribe([this](Document::Event event) {
        if (event != Document::Event::Saved || document_.sessionGeneration() != session_ || (result_ && !current()))
            invalidate("입력/문서가 바뀌었습니다. 최신 입력으로 다시 비교하세요.");
    });
    recalculate();
}
ProgressPolicyDialog::~ProgressPolicyDialog() {
    if (control_)
        control_->cancel();
    document_.unsubscribe(listener_);
}
void ProgressPolicyDialog::reject() {
    if (control_)
        control_->cancel();
    ++generation_;
    QDialog::reject();
}
bool ProgressPolicyDialog::current() const {
    return result_ && document_.project() && document_.sessionGeneration() == session_ &&
           document_.project()->documentId == result_->documentId && document_.project()->revision == result_->revision;
}
void ProgressPolicyDialog::invalidate(const QString &message) {
    if (control_)
        control_->cancel();
    ++generation_;
    result_.reset();
    tasksModel_->set({});
    relationsModel_->set({});
    details_->clear();
    diagnostics_->clear();
    summary_->setText(message);
    cancel_->setEnabled(false);
    export_->setEnabled(false);
    jump_->setEnabled(false);
    setProperty("progressPolicyRows", 0);
    setProperty("progressPolicyReady", false);
    updateActions();
}
void ProgressPolicyDialog::recalculate() {
    if (posting_)
        return;
    invalidate("전체 입력의 두 native 진척 정책을 비교 중…");
    if (!document_.project())
        return;
    const auto source = document_.snapshot();
    session_ = document_.sessionGeneration();
    const auto generation = generation_;
    control_ = std::make_shared<CalculationControl>();
    cancel_->setEnabled(true);
    auto *watcher = new QFutureWatcher<Prepared>(this);
    connect(watcher, &QFutureWatcher<Prepared>::finished, this, [this, watcher, generation, source] {
        auto prepared = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        cancel_->setEnabled(false);
        if (control_->cancelRequested.load() || !document_.project() || document_.sessionGeneration() != session_ ||
            document_.project()->documentId != source->documentId || document_.project()->revision != source->revision)
            return;
        if (!prepared.error.isEmpty()) {
            summary_->setText(prepared.error);
            return;
        }
        result_ = std::move(prepared.result);
        setProperty("progressPolicyBuildMs", prepared.milliseconds);
        display();
        setProperty("progressPolicyReady", true);
    });
    watcher->setFuture(QtConcurrent::run([source, control = control_] {
        Prepared out;
        try {
            QElapsedTimer timer;
            timer.start();
            out.result = std::make_shared<const ProgressPolicyComparison>(compareProgressPolicies(*source, control));
            out.milliseconds = double(timer.nsecsElapsed()) / 1000000;
        } catch (const CalculationCancelled &) {
            out.error = "비교 취소";
        } catch (const std::exception &e) {
            out.error = qErrorText(e);
        }
        return out;
    }));
}
void ProgressPolicyDialog::display() {
    if (!current())
        return;
    QElapsedTimer timer;
    timer.start();
    tasksModel_->set(result_);
    relationsModel_->set(result_);
    if (!result_->computed)
        summary_->setText(qs(result_->blockingReason));
    else
        summary_->setText(QString("상태일 %1 · rev %2 · 변경 %3 / %4 작업 · 비교 불가 %5 · B 비활성 관계 %6\n"
                                  "A %7 / 완료 %8 | B %9 / 완료 %10\nnative 비교 정책 · P6 실제 왕복 미실행")
                              .arg(qs(result_->statusAt))
                              .arg(result_->revision)
                              .arg(result_->changedTasks)
                              .arg(result_->tasks.size())
                              .arg(result_->uncomparedTasks)
                              .arg(result_->ignored.size())
                              .arg(state(result_->retained), stamp(result_->retained.projectFinish),
                                   state(result_->overridden), stamp(result_->overridden.projectFinish)));
    summary_->setText(
        summary_->text() + "\nC Actual Dates: " + state(result_->actualDates) + " / 완료 " +
        stamp(result_->actualDates.projectFinish) +
        QString(" · 이력 관계 %1개 · 실제 끝점 보존, 미래 완료 관계 유지").arg(result_->actualDatesIgnored.size()));
    const auto &choice = document_.project()->progressPolicy;
    summary_->setText(summary_->text() + "\n현재 정책: " +
                      (choice ? qs(choice->policy) + " · 담당자 " + qs(choice->adoptedBy) + " · " + qs(choice->reason) +
                                    " · UTC " + qs(choice->adoptedAt) +
                                    (document_.project()->statusAt ? "" : " · 상태일 미설정: 비활성")
                              : "기본 잔여 논리 유지 (명시적 채택 없음)"));
    updateActions();
    QStringList diagnostics;
    for (const auto *lane : {&result_->originalPlan, &result_->retained, &result_->overridden, &result_->actualDates}) {
        diagnostics << qs(lane->calculationPolicy) + " · " + state(*lane);
        std::size_t shown = 0;
        for (const auto &d : lane->diagnostics) {
            if (shown++ == 500) {
                diagnostics << "화면 진단은 정책별 500개까지 표시합니다. 전체 진단은 JSON 비교 기록을 확인하세요.";
                break;
            }
            diagnostics << qs(d.code) + ": " + qs(diagnosticMessage(d));
        }
    }
    diagnostics_->setPlainText(diagnostics.join('\n'));
    export_->setEnabled(result_->computed);
    for (int row = 0; row < tasks_->model()->rowCount(); ++row)
        if (tasks_->model()->index(row, 0).data(Qt::UserRole).toString() == qs(selected_)) {
            tasks_->setCurrentIndex(tasks_->model()->index(row, 0));
            tasks_->selectRow(row);
            break;
        }
    if (!tasks_->currentIndex().isValid() && tasks_->model()->rowCount())
        tasks_->setCurrentIndex(tasks_->model()->index(0, 0));
    selected();
    setProperty("progressPolicyRows", tasksModel_->rowCount());
    setProperty("progressPolicyRenderMs", double(timer.nsecsElapsed()) / 1000000);
}
void ProgressPolicyDialog::selected() {
    const auto index = tasks_->currentIndex();
    jump_->setEnabled(current() && index.isValid());
    if (!current() || !index.isValid()) {
        details_->clear();
        return;
    }
    selected_ = index.data(Qt::UserRole).toString().toStdString();
    details_->setPlainText("원본 작업 ID: " + qs(selected_) + "\nA: " + qs(result_->retained.calculationPolicy) +
                           "\nC: " + qs(result_->actualDates.calculationPolicy) +
                           "\nB: " + qs(result_->overridden.calculationPolicy));
}
void ProgressPolicyDialog::updateActions() {
    const bool editable = submit_ && !posting_ && document_.project() && !document_.readOnly();
    const bool comparable = editable && current() && result_->computed;
    adoptA_->setEnabled(comparable && result_->retained.status == CalculationStatus::Valid);
    adoptB_->setEnabled(comparable && result_->overridden.status == CalculationStatus::Valid);
    adoptC_->setEnabled(comparable && result_->actualDates.status == CalculationStatus::Valid);
    clear_->setEnabled(editable && document_.project()->progressPolicy.has_value());
}
void ProgressPolicyDialog::applyPolicy(const char *policy) {
    if (!submit_ || posting_ || !document_.project() || document_.readOnly())
        return;
    try {
        const auto source = document_.snapshot();
        const auto session = document_.sessionGeneration();
        const auto generation = generation_;
        EditCommand command{std::in_place_type<ClearProgressPolicyEdit>};
        if (policy) {
            if (!current() || !result_->computed)
                return;
            QDialog metadata(this);
            metadata.setObjectName("progressPolicyMetadata");
            metadata.setWindowTitle("진척 정책 채택 근거");
            auto *form = new QFormLayout(&metadata);
            auto *by = new QLineEdit(&metadata);
            by->setObjectName("progressPolicyAdopter");
            by->setMaxLength(256);
            auto *reason = new QLineEdit(&metadata);
            reason->setObjectName("progressPolicyReason");
            reason->setMaxLength(1024);
            form->addRow("정책", new QLabel(qs(policy), &metadata));
            if (source->planLeveling || source->remainingLeveling)
                form->addRow(
                    new QLabel("기존 평준화 채택을 해제합니다. 새 정책으로 다시 평준화해 검토하세요.", &metadata));
            form->addRow("담당자", by);
            form->addRow("사유", reason);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &metadata);
            form->addRow(buttons);
            const auto valid = [=] {
                buttons->button(QDialogButtonBox::Ok)
                    ->setEnabled(!by->text().trimmed().isEmpty() && !reason->text().trimmed().isEmpty());
            };
            connect(by, &QLineEdit::textChanged, &metadata, valid);
            connect(reason, &QLineEdit::textChanged, &metadata, valid);
            valid();
            connect(buttons, &QDialogButtonBox::accepted, &metadata, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &metadata, &QDialog::reject);
            if (metadata.exec() != QDialog::Accepted)
                return;
            if (!current() || generation != generation_)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            command = EditCommand{std::in_place_type<AdoptProgressPolicyEdit>,
                                  AdoptProgressPolicyEdit{
                                      {policy, by->text().trimmed().toStdString(),
                                       QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString(),
                                       reason->text().trimmed().toStdString(), source->documentId, source->revision}}};
        }
        if (!document_.project() || document_.readOnly() || session != document_.sessionGeneration() ||
            source->documentId != document_.project()->documentId || source->revision != document_.project()->revision)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        if (control_)
            control_->cancel();
        posting_ = true;
        invalidate("진척 정책 채택·저장을 검증 중…");
        cancel_->setEnabled(true);
        QPointer<ProgressPolicyDialog> guard(this);
        control_ = submit_(std::move(command), [guard](EditState state, const QString &message) {
            if (!guard)
                return;
            guard->posting_ = false;
            if (state == EditState::Applied || state == EditState::NoChange)
                guard->recalculate();
            else
                guard->invalidate(message.isEmpty() ? "문서 변경 또는 취소로 적용하지 않았습니다. 다시 비교하세요."
                                                    : message);
        });
    } catch (const std::exception &e) {
        posting_ = false;
        diagnostics_->setPlainText(qErrorText(e));
        updateActions();
    }
}
void ProgressPolicyDialog::save() {
    if (!current() || !result_->computed)
        return;
    const auto source = result_;
    const auto generation = generation_;
    const auto path = QFileDialog::getSaveFileName(this, "진척 정책 비교 JSON 저장", "progress-policy-comparison.json",
                                                   "JSON (*.json)");
    if (path.isEmpty())
        return;
    if (!current() || generation != generation_ || source != result_) {
        summary_->setText("파일 선택 중 입력이 바뀌어 내보내지 않았습니다.");
        return;
    }
    if (!authorizeSandboxFile(this, path))
        return;
    try {
        if (runExchangeOperation(
                this, document_, "비교 JSON 기록 중…", [source, path] { saveProgressPolicyJson(*source, path); },
                true) &&
            current())
            summary_->setText(summary_->text() + "\nJSON 저장: " + path);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "비교 내보내기", qErrorText(e));
    }
}
} // namespace jobs
