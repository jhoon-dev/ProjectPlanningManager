#include "ui/DatedRemainingWorkDialog.h"
#include "ui/EditorUi.h"
#include "calculation/PlannedWork.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <unordered_map>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
struct Loaded {
    std::optional<RemainingWorkDescription> value;
    QString error;
};
struct Previewed {
    std::shared_ptr<Document::PreparedEdit> edit;
    QString summary;
};
} // namespace
DatedRemainingWorkDialog::DatedRemainingWorkDialog(Document &doc, const std::string &taskId, QWidget *parent)
    : QDialog(parent), doc_(doc) {
    setObjectName("datedRemainingWorkDialog");
    setWindowTitle("날짜별 잔여 공수 · 계획/실적 분리");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "고정 기간·고정 공수·고정 단위 작업의 현재 상태일 이후 날짜별 잔여 목표입니다.\n"
        "빈칸은 기존 계획 곡선의 잔여 공수 사용, 0은 그 날짜의 수요 없음입니다. 시간(h)은 소수 4자리까지, "
        "1/100 자원분으로 반올림합니다. 작업·자원 달력/휴일·중단을 반영합니다.\n"
        "공수 기준은 명시 잔여 총공수에서 날짜 목표를 먼저 확보하고 미입력 날짜의 기존 곡선으로 나머지를 채워 "
        "완료를 계산합니다. 목표 합계가 총공수를 넘으면 거절합니다. 기간 기준은 잔여 기간을 유지합니다.\n"
        "계획 예산·기간·실제공수·진척은 변경하지 않습니다. 날짜는 고정 입력이며 상태일 변경 시 재검토합니다. "
        "이 창은 평준화 채택 전 논리 예측의 선택 배정을 검산합니다. 가용률을 넘는 입력은 부하에서 확인하세요.\n"
        "다른 배정에 재검토할 입력이 남으면 전체 잔여 예측은 충돌 상태를 유지합니다.",
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    assignment_ = new QComboBox(this);
    assignment_->setObjectName("datedRemainingAssignment");
    layout->addWidget(assignment_);
    basisLabel_ = new QLabel(this);
    basisLabel_->setWordWrap(true);
    basisLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(basisLabel_);
    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName("datedRemainingTable");
    table_->setHorizontalHeaderLabels(
        {"날짜", "자원 가능 근무분", "기존 곡선 잔여(h)", "목표 잔여(h) · 빈칸 상속", "검토"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    auto *dateRow = new QHBoxLayout;
    date_ = new QDateEdit(QDate::currentDate(), this);
    date_->setObjectName("datedRemainingNewDate");
    date_->setDisplayFormat("yyyy-MM-dd");
    date_->setCalendarPopup(true);
    dateRow->addWidget(date_);
    addDate_ = new QPushButton("목표 날짜 추가", this);
    addDate_->setObjectName("addDatedRemainingDate");
    dateRow->addWidget(addDate_);
    dateRow->addStretch();
    layout->addLayout(dateRow);
    connect(addDate_, &QPushButton::clicked, this, [this] {
        if (!basis_ || !current() || doc_.readOnly() || busy_ || basis_->days.size() >= 1024)
            return;
        const auto day = date_->date().toString(Qt::ISODate).toStdString();
        if (std::any_of(basis_->days.begin(), basis_->days.end(), [&](const auto &d) { return d.date == day; }))
            return;
        basis_->days.push_back({day, 0, 0, {}, false});
        const QSignalBlocker block(table_);
        const auto row = table_->rowCount();
        table_->insertRow(row);
        const QStringList cells{q(day), "검산 시 확인", "", "", "추가 날짜 · 미리보기로 검산"};
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            if (c != 3)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, c, item);
        }
        changed();
        table_->scrollToBottom();
    });
    auto *tools = new QHBoxLayout;
    auto *refresh = new QPushButton("최신 문서 불러오기", this);
    refresh->setObjectName("reloadDatedRemainingWork");
    tools->addWidget(refresh);
    clear_ = new QPushButton("선택 배정의 날짜 입력 해제", this);
    clear_->setObjectName("clearDatedRemainingWork");
    tools->addWidget(clear_);
    clearAll_ = new QPushButton("전체 날짜 입력 해제", this);
    clearAll_->setObjectName("clearAllDatedRemainingWork");
    tools->addWidget(clearAll_);
    layout->addLayout(tools);
    message_ = new QLabel(this);
    message_->setObjectName("datedRemainingMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    preview_ = buttons->addButton("검산 미리보기", QDialogButtonBox::ActionRole);
    preview_->setObjectName("previewDatedRemainingWork");
    apply_ = buttons->addButton("문서에 적용", QDialogButtonBox::ActionRole);
    apply_->setObjectName("applyDatedRemainingWork");
    cancel_ = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
    cancel_->setObjectName("cancelDatedRemainingWork");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &DatedRemainingWorkDialog::reject);
    connect(assignment_, &QComboBox::currentIndexChanged, this, [this] { load(); });
    connect(table_, &QTableWidget::itemChanged, this, [this] { changed(); });
    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(preview_, &QPushButton::clicked, this, [this] { prepare(); });
    connect(apply_, &QPushButton::clicked, this, [this] { commit(); });
    connect(clear_, &QPushButton::clicked, this, [this] { prepare(1); });
    connect(clearAll_, &QPushButton::clicked, this, [this] { prepare(2); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
        prepared_.reset();
    });
    listener_ = doc_.subscribe([this](Document::Event) {
        if (committing_ || current())
            return;
        ++generation_;
        if (control_)
            control_->cancel();
        prepared_.reset();
        message_->setText("문서·권한이 변경되었습니다. 최신 문서를 불러온 뒤 다시 검산하세요.");
        controls();
    });
    reload();
    for (int i = 0; i < assignment_->count(); ++i)
        if (source_->assignments[assignment_->itemData(i).toULongLong()].taskId == taskId) {
            assignment_->setCurrentIndex(i);
            break;
        }
    resize(1100, 700);
    prepareEditorDialog(*this);
}
DatedRemainingWorkDialog::~DatedRemainingWorkDialog() {
    if (control_)
        control_->cancel();
    doc_.unsubscribe(listener_);
}
void DatedRemainingWorkDialog::reject() {
    ++generation_;
    if (control_)
        control_->cancel();
    prepared_.reset();
    QDialog::reject();
}
bool DatedRemainingWorkDialog::current() const {
    return source_ && doc_.project() && doc_.sessionGeneration() == session_ &&
           doc_.project()->documentId == source_->documentId && doc_.project()->revision == source_->revision;
}
const Assignment *DatedRemainingWorkDialog::selected() const {
    return source_ && assignment_->currentIndex() >= 0
               ? &source_->assignments.at(assignment_->currentData().toULongLong())
               : nullptr;
}
void DatedRemainingWorkDialog::controls() {
    const bool editable = current() && !doc_.readOnly() && !busy_;
    preview_->setEnabled(editable && basis_.has_value());
    apply_->setEnabled(editable && prepared_);
    clear_->setEnabled(editable && selected());
    clearAll_->setEnabled(editable && source_ && !source_->remainingWork.empty());
    assignment_->setEnabled(!busy_);
    cancel_->setEnabled(busy_);
    addDate_->setEnabled(editable && basis_.has_value() && basis_->days.size() < 1024);
    date_->setEnabled(editable && basis_.has_value());
    table_->setEditTriggers(editable ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                     : QAbstractItemView::NoEditTriggers);
}
void DatedRemainingWorkDialog::reload() {
    ++generation_;
    if (control_)
        control_->cancel();
    prepared_.reset();
    busy_ = false;
    source_ = doc_.snapshot();
    session_ = doc_.sessionGeneration();
    QSignalBlocker block(assignment_);
    assignment_->clear();
    if (source_) {
        std::unordered_map<std::string, std::string> tasks, resources;
        for (const auto &t : source_->tasks)
            tasks[t.id] = t.name;
        for (const auto &r : source_->resources)
            resources[r.id] = r.name;
        for (std::size_t i = 0; i < source_->assignments.size(); ++i) {
            const auto &a = source_->assignments[i];
            assignment_->addItem(q(tasks[a.taskId] + " · " + resources[a.resourceId]), qulonglong(i));
        }
    }
    load();
}
void DatedRemainingWorkDialog::changed() {
    ++generation_;
    if (control_)
        control_->cancel();
    prepared_.reset();
    message_->setText("목표를 수정했습니다. 검산 미리보기 후 문서에 적용하세요.");
    controls();
}
void DatedRemainingWorkDialog::load() {
    const auto generation = ++generation_;
    if (control_)
        control_->cancel();
    prepared_.reset();
    basis_.reset();
    {
        QSignalBlocker block(table_);
        table_->setRowCount(0);
    }
    basisLabel_->clear();
    if (!current() || !selected()) {
        busy_ = false;
        message_->setText("자원 배정이 없습니다.");
        controls();
        return;
    }
    const auto a = *selected();
    busy_ = true;
    control_ = std::make_shared<CalculationControl>();
    controls();
    message_->setText("잔여 실행·자원 달력 확인 중…");
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, generation] {
        auto out = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        busy_ = false;
        if (!current()) {
            controls();
            return;
        }
        if (!out.value) {
            message_->setText(out.error);
            controls();
            return;
        }
        basis_ = std::move(out.value);
        date_->setDate(QDate::fromString(q(basis_->dates.finish.substr(0, 10)), Qt::ISODate).addDays(1));
        const QSignalBlocker block(table_);
        table_->setRowCount(int(basis_->days.size()));
        for (int i = 0; i < table_->rowCount(); ++i) {
            const auto &d = basis_->days[std::size_t(i)];
            QStringList cells{q(d.date), QString::number(d.availableMinutes), q(plannedWorkHours(d.inheritedWork)),
                              d.target ? q(plannedWorkHours(*d.target)) : QString{},
                              d.outside            ? "실행 구간 밖 · 입력 해제 필요"
                              : d.availableMinutes ? ""
                                                   : "자원 근무시간 없음"};
            for (int c = 0; c < cells.size(); ++c) {
                auto *item = new QTableWidgetItem(cells[c]);
                if (c != 3)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table_->setItem(i, c, item);
            }
        }
        basisLabel_->setText(
            QString("현재 상태일 %1 · 논리 잔여 %2 → %3%4")
                .arg(q(source_->statusAt.value_or("미설정")),
                     q(basis_->dates.remainingStart.value_or(basis_->dates.start)), q(basis_->dates.finish),
                     basis_->storedStatusAt.empty() ? QString{}
                                                    : QString(" · 저장 입력 기준 %1").arg(q(basis_->storedStatusAt))));
        message_->setText(doc_.readOnly()
                              ? "읽기 전용 · 날짜별 입력과 기존 곡선을 조회할 수 있습니다."
                              : "빈칸은 기존 곡선, 0은 수요 없음입니다. 검산 전에는 문서에 적용되지 않습니다.");
        controls();
    });
    watcher->setFuture(QtConcurrent::run([source = source_, a, control = control_] {
        Loaded out;
        try {
            out.value = describeRemainingWork(*source, a.taskId, a.resourceId, control);
        } catch (const CalculationCancelled &) {
            out.error = "계산 취소";
        } catch (const std::exception &e) {
            out.error = qErrorText(e);
        }
        return out;
    }));
}
void DatedRemainingWorkDialog::prepare(int mode) {
    if (!current() || doc_.readOnly() || busy_ || (!selected() && mode != 2))
        return;
    try {
        RemainingWorkPlan plan;
        if (selected()) {
            plan.taskId = selected()->taskId;
            plan.resourceId = selected()->resourceId;
        }
        plan.statusAt = source_->statusAt.value_or("");
        std::int64_t total = 0;
        if (mode == 0) {
            if (!basis_)
                return;
            for (int i = 0; i < table_->rowCount(); ++i) {
                const auto value = table_->item(i, 3)->text().trimmed();
                if (value.isEmpty())
                    continue;
                const auto work = parsePlannedWorkHours(value.toStdString());
                if (total > maxWork - work)
                    throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"입력 공수 합계 범위를 확인하세요."});
                total += work;
                plan.days.push_back({basis_->days[std::size_t(i)].date, work});
            }
        }
        const auto count = plan.days.size();
        std::sort(plan.days.begin(), plan.days.end(), [](const auto &a, const auto &b) { return a.date < b.date; });
        const auto taskId = plan.taskId;
        auto request = doc_.makeEditRequest(DatedRemainingWorkEdit{std::move(plan), mode == 2});
        const auto generation = ++generation_;
        prepared_.reset();
        control_ = std::make_shared<CalculationControl>();
        busy_ = true;
        controls();
        message_->setText("날짜별 공수·한도·실행 구간·원자 변경 검산 중…");
        auto *watcher = new QFutureWatcher<Previewed>(this);
        connect(watcher, &QFutureWatcher<Previewed>::finished, this, [this, watcher, generation, mode, count, total] {
            auto out = watcher->result();
            auto result = out.edit;
            watcher->deleteLater();
            if (generation != generation_)
                return;
            busy_ = false;
            if (!current()) {
                controls();
                return;
            }
            if (control_->cancelRequested.load() || result->state() == EditState::Cancelled)
                message_->setText("계산을 취소했습니다. 입력은 변경하지 않았습니다.");
            else if (result->failure())
                showEditorError(*message_, AppError(*result->failure()));
            else if (result->state() == EditState::Ready || result->state() == EditState::NoChange) {
                prepared_ = result;
                message_->setText(QString("검산 완료 · 명시 입력 %1일, 합계 %2 h · 빈 날짜는 기존 곡선을 "
                                          "사용합니다. 아직 문서에는 적용하지 않았습니다.")
                                      .arg(count)
                                      .arg(q(plannedWorkHours(total))) +
                                  out.summary);
                if (mode != 0) {
                    commit();
                    return;
                }
            }
            controls();
        });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), control = control_, taskId]() mutable {
            Previewed out;
            out.edit = Document::prepareEdit(std::move(request), control);
            if (out.edit->state() == EditState::Ready && out.edit->previewProject()) {
                try {
                    EvaluationScope scope(control);
                    const auto p = out.edit->previewProject();
                    const auto result = calculate(*p);
                    for (const auto &r : result.tasks)
                        if (r.taskId == taskId)
                            out.summary =
                                QString("\n검산 잔여 %1 → %2").arg(q(r.remainingStart.value_or(r.start)), q(r.finish));
                } catch (const std::exception &) {
                    // PreparedEdit owns the authoritative cancellation/failure result.
                }
            }
            return out;
        }));
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
        controls();
    }
}
void DatedRemainingWorkDialog::commit() {
    if (!current() || doc_.readOnly() || busy_ || !prepared_)
        return;
    committing_ = true;
    const auto state = doc_.applyPreparedEdit(*prepared_);
    committing_ = false;
    prepared_.reset();
    if (state == EditState::Applied || state == EditState::NoChange) {
        source_ = doc_.snapshot();
        session_ = doc_.sessionGeneration();
        load();
    } else {
        message_->setText("문서·권한 변경 또는 취소로 적용하지 않았습니다. 다시 불러와 검산하세요.");
        controls();
    }
}
} // namespace jobs
