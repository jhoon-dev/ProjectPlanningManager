#include "ui/AssignmentPeriodsDialog.h"
#include "ui/EditorUi.h"
#include "ui/PlannedWorkDialog.h"
#include "calculation/AssignmentPeriods.h"
#include "calculation/Effort.h"
#include "calculation/WorkContourInverse.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <set>
#include <unordered_map>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
struct Preview {
    std::optional<TaskSchedule> dates;
    std::int64_t work = 0, duration = 0;
    std::optional<std::int64_t> remaining;
    QString error;
    std::vector<QString> labels;
};
} // namespace
class AssignmentCurveView final : public QWidget {
public:
    std::vector<AssignmentPeriod> periods;
    std::int64_t duration = 0;
    int units = 100;
    explicit AssignmentCurveView(QWidget *parent) : QWidget(parent) {
        setObjectName("assignmentCurveView");
        setMinimumHeight(150);
        setAccessibleName("작업 근무시간에 따른 계획 배정률 곡선");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        if (duration <= 0)
            return;
        const QRectF plot(65, 24, std::max(1, width() - 90), std::max(1, height() - 60));
        int peak = units;
        for (const auto &v : periods)
            if (v.fromMinute < duration)
                peak = std::max(peak, v.units);
        peak = std::max(100, peak);
        p.setPen(palette().mid().color());
        p.drawRect(plot);
        p.setPen(palette().text().color());
        p.drawText(QRectF(0, 5, 60, 25), Qt::AlignRight, QString::number(peak) + "%");
        p.drawText(QRectF(0, plot.bottom() - 20, 60, 25), Qt::AlignRight, "0%");
        p.drawText(QRectF(plot.left(), plot.bottom() + 5, plot.width(), 25), Qt::AlignCenter,
                   QString("작업 시작 이후 0 ~ %1 근무분").arg(duration));
        auto segment = [&](std::int64_t from, std::int64_t to, int rate) {
            to = std::min(to, duration);
            if (from >= to)
                return;
            const auto x = plot.left() + plot.width() * double(from) / double(duration);
            const auto w = plot.width() * double(to - from) / double(duration);
            const auto h = plot.height() * rate / peak;
            p.fillRect(QRectF(x, plot.bottom() - h, w, h), QColor(51, 126, 190, 120));
            p.setPen(QPen(QColor(31, 100, 164), 2));
            p.drawLine(QPointF(x, plot.bottom() - h), QPointF(x + w, plot.bottom() - h));
        };
        std::int64_t at = 0;
        for (const auto &v : periods) {
            segment(at, v.fromMinute, units);
            segment(v.fromMinute, v.toMinute, v.units);
            at = v.toMinute;
        }
        segment(at, duration, units);
    }
};
AssignmentPeriodsDialog::AssignmentPeriodsDialog(Document &doc, const std::string &taskId, QWidget *parent)
    : QDialog(parent), doc_(doc), source_(doc.snapshot()), session_(doc.sessionGeneration()) {
    setObjectName("assignmentPeriodsDialog");
    setWindowTitle("기간별 계획 배정 · 자원 곡선");
    auto *layout = new QVBoxLayout(this);
    auto *note =
        new QLabel("고정 기간·공수·단위 작업 · 100% = 자원 1단위 · 구간은 실행 시작 기준 근무분 [시작, 끝)입니다.\n"
                   "빈 구간은 기본 배정률, 0%는 수요 없음. 달력·휴일·중단을 제외하며 작업 이동 시 곡선도 이동합니다.\n"
                   "기간 밖 구간은 보존합니다. 기간 기준 잔여는 뒤쪽 곡선, 공수 기준 잔여는 현재 실행의 0분부터 "
                   "곡선을 재생하고 보고된 잔여 공수에 정확히 맞춥니다.",
                   this);
    note->setWordWrap(true);
    layout->addWidget(note);
    assignment_ = new QComboBox(this);
    assignment_->setObjectName("periodAssignment");
    assignment_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    assignment_->setMinimumContentsLength(36);
    if (source_) {
        std::unordered_map<std::string, std::string> tasks, resources;
        for (const auto &t : source_->tasks)
            tasks[t.id] = t.name;
        for (const auto &r : source_->resources)
            resources[r.id] = r.name;
        for (std::size_t i = 0; i < source_->assignments.size(); ++i) {
            const auto &a = source_->assignments[i];
            assignment_->addItem(q(tasks.at(a.taskId) + " · " + resources.at(a.resourceId)), qulonglong(i));
        }
    }
    layout->addWidget(assignment_);
    basis_ = new QLabel(this);
    basis_->setWordWrap(true);
    layout->addWidget(basis_);
    auto *tools = new QHBoxLayout;
    curve_ = new QComboBox(this);
    curve_->setObjectName("assignmentCurvePreset");
    curve_->addItems({"균등 (구간 제거)", "앞쪽 집중", "뒤쪽 집중", "중앙 집중"});
    tools->addWidget(curve_);
    preset_ = new QPushButton("곡선 생성", this);
    preset_->setObjectName("generateAssignmentCurve");
    tools->addWidget(preset_);
    period_ = new QComboBox(this);
    period_->setObjectName("assignmentPeriodUnit");
    period_->addItems({"일", "주 (월~일)", "월"});
    tools->addWidget(period_);
    generate_ = new QPushButton("기간 행 생성", this);
    generate_->setObjectName("generateAssignmentPeriods");
    tools->addWidget(generate_);
    layout->addLayout(tools);
    auto *hint = new QLabel("곡선 생성은 현재 초안을 대체합니다. 기간 행 생성은 현재 배정률을 보존해 날짜 경계에서 "
                            "나눕니다. 혼합률은 별도 행으로 유지됩니다.\n"
                            "구간을 모두 지우면 균등 배정입니다. 변경 후 ‘문서에 적용’을 눌러야 저장됩니다.",
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("assignmentPeriodsTable");
    table_->setHorizontalHeaderLabels(
        {"시작 근무분 (포함)", "끝 근무분 (제외)", "대체 배정률 (%)", "계획 날짜·시간 (작업 달력)"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int c = 0; c < 3; ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    auto *rows = new QHBoxLayout;
    add_ = new QPushButton("구간 추가", this);
    add_->setObjectName("addAssignmentPeriod");
    remove_ = new QPushButton("선택 구간 삭제", this);
    remove_->setObjectName("removeAssignmentPeriod");
    reset_ = new QPushButton("저장된 값 불러오기", this);
    reset_->setObjectName("resetAssignmentPeriods");
    rows->addWidget(add_);
    rows->addWidget(remove_);
    rows->addWidget(reset_);
    work_ = new QPushButton("계획 공수로 배정률 생성…", this);
    work_->setObjectName("editPlannedWork");
    rows->addWidget(work_);
    layout->addLayout(rows);
    view_ = new AssignmentCurveView(this);
    layout->addWidget(view_);
    preview_ = new QLabel(this);
    preview_->setObjectName("assignmentPeriodsPreview");
    preview_->setWordWrap(true);
    layout->addWidget(preview_);
    message_ = new QLabel(this);
    message_->setObjectName("assignmentPeriodsMessage");
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    layout->addWidget(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    apply_ = buttons->addButton("문서에 적용", QDialogButtonBox::ActionRole);
    apply_->setObjectName("applyAssignmentPeriods");
    cancel_ = buttons->addButton("적용 계산 취소", QDialogButtonBox::ActionRole);
    cancel_->setObjectName("cancelAssignmentPeriods");
    layout->addWidget(buttons);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(200);
    connect(timer_, &QTimer::timeout, this, [this] { preview(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &AssignmentPeriodsDialog::reject);
    connect(assignment_, &QComboBox::currentIndexChanged, this, [this] { load(); });
    connect(table_, &QTableWidget::itemChanged, this, [this] { changed(); });
    connect(reset_, &QPushButton::clicked, this, [this] { load(); });
    connect(preset_, &QPushButton::clicked, this, [this] {
        try {
            if (selected() && task())
                fill(assignmentCurve(previewDuration_, selected()->units, AssignmentCurve(curve_->currentIndex())));
        } catch (const std::exception &e) {
            showEditorError(*message_, e);
        }
    });
    connect(generate_, &QPushButton::clicked, this, [this] { generate(); });
    connect(work_, &QPushButton::clicked, this, [this] { plannedWork(); });
    connect(add_, &QPushButton::clicked, this, [this] {
        try {
            auto values = read();
            if (values.size() >= 1024)
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"구간 수 초과"});
            const auto from = values.empty() ? 0 : values.back().toMinute;
            values.push_back({from, from + 1, selected()->units});
            fill(values);
            table_->selectRow(table_->rowCount() - 1);
        } catch (const std::exception &e) {
            showEditorError(*message_, e);
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.row() > b.row(); });
        for (const auto &row : rows)
            table_->removeRow(row.row());
        changed();
    });
    connect(apply_, &QPushButton::clicked, this, [this] { submit(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    listener_ = doc_.subscribe([this](Document::Event) {
        if (committing_ || current())
            return;
        if (control_)
            control_->cancel();
        message_->setText("문서가 변경되었습니다. 창을 다시 열어 최신 값으로 편집하세요.");
        controls();
    });
    for (int i = 0; i < assignment_->count(); ++i)
        if (source_->assignments[assignment_->itemData(i).toULongLong()].taskId == taskId) {
            assignment_->setCurrentIndex(i);
            break;
        }
    load();
    resize(1000, 840);
    prepareEditorDialog(*this);
}
AssignmentPeriodsDialog::~AssignmentPeriodsDialog() {
    if (control_)
        control_->cancel();
    if (previewControl_)
        previewControl_->cancel();
    doc_.unsubscribe(listener_);
}
void AssignmentPeriodsDialog::reject() {
    if (control_)
        control_->cancel();
    if (previewControl_)
        previewControl_->cancel();
    QDialog::reject();
}
const Assignment *AssignmentPeriodsDialog::selected() const {
    if (!source_ || assignment_->currentIndex() < 0)
        return nullptr;
    return &source_->assignments.at(assignment_->currentData().toULongLong());
}
const Task *AssignmentPeriodsDialog::task() const {
    const auto *a = selected();
    if (!a)
        return nullptr;
    for (const auto &t : source_->tasks)
        if (t.id == a->taskId)
            return &t;
    return nullptr;
}
bool AssignmentPeriodsDialog::current() const {
    return source_ && doc_.project() && !doc_.readOnly() && doc_.sessionGeneration() == session_ &&
           doc_.project()->documentId == source_->documentId && doc_.project()->revision == source_->revision;
}
void AssignmentPeriodsDialog::controls() {
    const auto *t = task();
    const bool editable = current() && !busy_ && t &&
                          (usesWork(*t) ? t->workUnitMinutes.has_value() : t->durationMinutes.value_or(0) > 0);
    for (auto *b : {apply_, add_, remove_, reset_})
        b->setEnabled(editable);
    preset_->setEnabled(editable && previewDuration_ > 0);
    work_->setEnabled(editable && t->taskType == TaskType::FixedDuration);
    work_->setToolTip("고정 공수·단위는 작업의 기간/공수 입력과 곡선 편집으로 역산합니다.");
    generate_->setEnabled(editable && dates_.has_value() && previewDuration_ > 0);
    table_->setEditTriggers(editable ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                     : QAbstractItemView::NoEditTriggers);
    assignment_->setEnabled(!busy_);
    curve_->setEnabled(editable);
    period_->setEnabled(editable);
    cancel_->setEnabled(busy_);
}
void AssignmentPeriodsDialog::load() {
    dates_.reset();
    previewDuration_ = task() && !usesWork(*task()) ? task()->durationMinutes.value_or(0) : 0;
    message_->clear();
    const auto *a = selected();
    const auto *t = task();
    basis_->setText(!a ? "자원 배정이 없습니다. 먼저 작업에 자원을 배정하세요."
                       : QString("기본 %1% · 입력 기간 %2 근무분 · 배정당 최대 1,024개 구간 · %3")
                             .arg(a->units)
                             .arg(t->durationMinutes.value_or(0))
                             .arg(doc_.readOnly()
                                      ? "읽기 전용"
                                      : "고정 공수는 공수 유지 · 고정 단위의 곡선 수정은 공수 유지 후 완료일 역산"));
    fill(a ? a->periods : std::vector<AssignmentPeriod>{});
    controls();
}
void AssignmentPeriodsDialog::fill(const std::vector<AssignmentPeriod> &values) {
    QSignalBlocker block(table_);
    table_->setRowCount(int(values.size()));
    for (int i = 0; i < int(values.size()); ++i) {
        const auto &v = values[std::size_t(i)];
        const QStringList cells{QString::number(v.fromMinute), QString::number(v.toMinute), QString::number(v.units)};
        for (int c = 0; c < 3; ++c)
            table_->setItem(i, c, new QTableWidgetItem(cells[c]));
        auto *label = new QTableWidgetItem("계산 중…");
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        table_->setItem(i, 3, label);
    }
    changed();
}
std::vector<AssignmentPeriod> AssignmentPeriodsDialog::read() const {
    std::vector<AssignmentPeriod> values;
    std::int64_t end = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        std::int64_t n[3];
        for (int c = 0; c < 3; ++c) {
            bool ok = false;
            n[c] = table_->item(row, c) ? table_->item(row, c)->text().trimmed().toLongLong(&ok) : -1;
            if (!ok)
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                               {"행 " + std::to_string(row + 1) + ": 정수를 입력하세요."});
        }
        if (n[0] < end || n[1] <= n[0] || n[1] > 5000000 || n[2] < 0 || n[2] > 100000)
            throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                           {"행 " + std::to_string(row + 1) + ": 정렬·비중복 구간과 배정률 범위를 확인하세요."});
        values.push_back({n[0], n[1], int(n[2])});
        end = n[1];
    }
    return values;
}
void AssignmentPeriodsDialog::changed() {
    ++previewGeneration_;
    if (previewControl_)
        previewControl_->cancel();
    if (!selected() || !task()) {
        timer_->stop();
        view_->duration = 0;
        view_->update();
        preview_->clear();
        return;
    }
    try {
        view_->periods = read();
        view_->duration = previewDuration_;
        view_->units = selected() ? selected()->units : 100;
        view_->update();
        preview_->setText("달력을 반영한 계획 공수 계산 중…");
        timer_->start();
    } catch (const std::exception &e) {
        timer_->stop();
        preview_->setText(qErrorText(e));
        view_->duration = 0;
        view_->update();
    }
}
void AssignmentPeriodsDialog::plannedWork() {
    if (!current() || busy_ || !selected())
        return;
    try {
        const auto generation = previewGeneration_;
        auto draft = std::make_shared<Project>(*source_);
        draft->assignments.at(assignment_->currentData().toULongLong()).periods = read();
        draft->calculationProfile = effortProfile;
        draft->planLeveling.reset();
        draft->remainingLeveling.reset();
        draft->levelingDecisions.clear();
        PlannedWorkDialog dialog(doc_, draft, selected()->taskId, selected()->resourceId, this);
        if (dialog.exec() == QDialog::Accepted && dialog.converted() && current() && generation == previewGeneration_)
            fill(dialog.converted()->periods);
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
    }
}
void AssignmentPeriodsDialog::preview() {
    if (!selected() || !task())
        return;
    const auto generation = previewGeneration_;
    const auto source = source_;
    const auto profile = *selected();
    const auto values = read();
    previewControl_ = std::make_shared<CalculationControl>();
    auto *watcher = new QFutureWatcher<Preview>(this);
    connect(watcher, &QFutureWatcher<Preview>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != previewGeneration_)
            return;
        dates_ = result.dates;
        previewDuration_ = result.duration;
        view_->duration = previewDuration_;
        view_->update();
        const QSignalBlocker block(table_);
        for (int i = 0; i < int(result.labels.size()) && i < table_->rowCount(); ++i) {
            table_->item(i, 3)->setText(result.labels[std::size_t(i)]);
            table_->item(i, 3)->setToolTip(result.labels[std::size_t(i)]);
        }
        if (!result.error.isEmpty())
            preview_->setText(result.error);
        else
            preview_->setText(QString("계획 %1 → %2 · 계산 기간 %5 근무분 · 자원 달력 반영 공수 %3 h%4\n곡선 면적은 "
                                      "작업 달력 기준이며 자원 "
                                      "비근무시간은 공수에서 제외됩니다. 실제공수 보고와는 별개입니다.")
                                  .arg(q(dates_->start), q(dates_->finish))
                                  .arg(double(result.work) / 6000., 0, 'f', 4)
                                  .arg(result.remaining
                                           ? QString(" · 잔여 %1 h").arg(double(*result.remaining) / 6000., 0, 'f', 4)
                                           : QString{})
                                  .arg(result.duration));
        controls();
    });
    watcher->setFuture(QtConcurrent::run([source, profile, values, control = previewControl_] {
        Preview out;
        try {
            EvaluationScope evaluation(control);
            Project p = *source;
            changeAssignmentContour(p, profile.taskId, profile.resourceId, values);
            p.calculationProfile = effortProfile;
            p.planLeveling.reset();
            p.remainingLeveling.reset();
            p.levelingDecisions.clear();
            const auto plan = calculatePlan(p);
            if (plan.status != CalculationStatus::Valid)
                throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
            const auto t =
                std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == profile.taskId; });
            out.dates = *std::find_if(plan.tasks.begin(), plan.tasks.end(),
                                      [&](const auto &v) { return v.taskId == profile.taskId; });
            TaskExecution model(p, *t);
            out.duration = model.duration(*out.dates);
            const auto origin = model.calendar().offset(out.dates->start);
            for (const auto &v : values) {
                if (v.fromMinute >= out.duration)
                    out.labels.push_back("현재 기간 밖 · 보존됨");
                else
                    out.labels.push_back(
                        q(model.calendar().boundary(origin + v.fromMinute, false)) + " → " +
                        q(model.calendar().boundary(origin + std::min(v.toMinute, out.duration), true)) +
                        (v.toMinute > out.duration ? " · 끝부분 기간 밖" : ""));
            }
            for (const auto &s : model.spans(*out.dates))
                if (s.resourceId == profile.resourceId)
                    out.work += (s.finish - s.start) * s.units;
            if (p.statusAt) {
                const auto future = calculate(p);
                if (future.status == CalculationStatus::Valid) {
                    out.remaining = 0;
                    const auto row = std::find_if(future.tasks.begin(), future.tasks.end(),
                                                  [&](const auto &v) { return v.taskId == profile.taskId; });
                    TaskExecution pending(p, pendingTask(p, *t));
                    for (const auto &s : pending.spans(*row))
                        if (s.resourceId == profile.resourceId)
                            *out.remaining += (s.finish - s.start) * s.units;
                }
            }
        } catch (const CalculationCancelled &) {
            out.error = "미리보기 계산을 취소했습니다.";
        } catch (const std::exception &e) {
            out.error = qErrorText(e);
        }
        return out;
    }));
}
void AssignmentPeriodsDialog::generate() {
    try {
        if (!current() || !selected() || !task() || !dates_)
            return;
        auto p = *source_;
        p.calculationProfile = effortProfile;
        const WorkCalendar calendar(p, *task());
        const auto origin = calendar.offset(dates_->start), duration = previewDuration_;
        const auto values = read();
        std::set<std::int64_t> cuts{0, duration};
        for (const auto &v : values) {
            cuts.insert(v.fromMinute);
            cuts.insert(v.toMinute);
        }
        auto at = QDate::fromString(q(dates_->start.substr(0, 10)), Qt::ISODate);
        const auto last = QDate::fromString(q(dates_->finish.substr(0, 10)), Qt::ISODate);
        while (at <= last) {
            QDate next = period_->currentIndex() == 0   ? at.addDays(1)
                         : period_->currentIndex() == 1 ? at.addDays(8 - at.dayOfWeek())
                                                        : QDate(at.year(), at.month(), 1).addMonths(1);
            const auto offset = calendar.offset((next.toString(Qt::ISODate) + " 00:00").toStdString()) - origin;
            if (offset > 0 && offset < duration)
                cuts.insert(offset);
            at = next;
        }
        std::vector<AssignmentPeriod> result;
        for (auto it = cuts.begin(), next = std::next(it); next != cuts.end(); ++it, ++next) {
            int units = selected()->units;
            for (const auto &v : values)
                if (v.fromMinute <= *it && *it < v.toMinute) {
                    units = v.units;
                    break;
                }
            if (*it < duration || std::any_of(values.begin(), values.end(),
                                              [&](const auto &v) { return v.fromMinute <= *it && *it < v.toMinute; }))
                result.push_back({*it, *next, units});
        }
        if (result.size() > 1024)
            throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"구간 수 초과. 주 또는 월 단위를 선택하세요."});
        fill(result);
        message_->setText("현재 배정률을 보존해 날짜 경계로 나눴습니다. 근무분 구간의 배정률을 수정한 후 적용하세요.");
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
    }
}
void AssignmentPeriodsDialog::submit() {
    try {
        if (!current() || busy_ || !selected())
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        const auto a = *selected();
        auto request = doc_.makeEditRequest(AssignmentPeriodsEdit{a.taskId, a.resourceId, read()});
        control_ = std::make_shared<CalculationControl>();
        busy_ = true;
        controls();
        message_->setText("기간별 배정 검증·적용 준비 중…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>(this);
        connect(watcher, &QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>::finished, this, [this, watcher] {
            auto result = watcher->result();
            watcher->deleteLater();
            committing_ = true;
            const auto state = doc_.applyPreparedEdit(*result);
            committing_ = false;
            busy_ = false;
            if (state == EditState::Applied || state == EditState::NoChange) {
                source_ = doc_.snapshot();
                load();
                message_->setText("문서에 반영했습니다. 파일 저장으로 보존하세요.");
            } else if (result->failure())
                showEditorError(*message_, AppError(*result->failure()));
            else
                message_->setText(state == EditState::Cancelled
                                      ? "적용을 취소했습니다."
                                      : "문서가 변경되어 적용하지 않았습니다. 창을 다시 열어 주세요.");
            controls();
        });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), control = control_]() mutable {
            return Document::prepareEdit(std::move(request), control);
        }));
    } catch (const std::exception &e) {
        showEditorError(*message_, e);
    }
}
} // namespace ppm
