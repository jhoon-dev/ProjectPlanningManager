#include "formatting/QtDiagnosticText.h"
#include "ui/ComparisonDialog.h"
#include "ui/OutcomeSummary.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/LevelingDecisionDialog.h"
#include "ui/EditorUi.h"
#include "ui/TaskInput.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QtConcurrent/QtConcurrentRun>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString stamp(const std::optional<std::string> &s) {
    return s ? qs(*s) : "—";
}
QString duration(const std::optional<std::int64_t> &n, int day) {
    return n ? taskDurationText(*n, day) : "—";
}
QString variance(const std::optional<std::int64_t> &n, int day) {
    if (!n)
        return "—";
    const auto sign = *n > 0 ? "+" : *n < 0 ? "−" : "";
    return sign + taskDurationText(std::abs(*n), day) + " (" + sign +
           QString::number(double(std::abs(*n)) / 60, 'g', 10) + "h)";
}
QString state(const ScheduleResult &r) {
    if (r.status == CalculationStatus::Cancelled)
        return "취소";
    if (r.status == CalculationStatus::Stale)
        return "오래된 결과";
    if (r.status == CalculationStatus::Valid)
        return "유효";
    if (r.status == CalculationStatus::Incomplete)
        return "입력 불완전";
    return "충돌";
}
struct Overview {
    ComparisonReport report;
    ScheduleResult logical;
    ResourceLoad before, current;
};
} // namespace
ComparisonDialog::ComparisonDialog(Document &doc, const std::string &id, QWidget *parent)
    : QDialog(parent), document_(doc), session_(doc.sessionGeneration()) {
    setObjectName("comparisonDialog");
    setWindowTitle("기준선 · 현재 계획 · 실적 반영 예측 비교");
    resize(1480, 760);
    auto *layout = new QVBoxLayout(this);
    baseline_ = new QComboBox(this);
    baseline_->setObjectName("comparisonBaseline");
    baseline_->setAccessibleName("비교 기준선");
    baseline_->addItem("기준선 없음 · 현재 계획과 예측만 표시", QString{});
    for (const auto &b : doc.project()->baselines)
        baseline_->addItem("v" + QString::number(b.version) + " · " + qs(b.name) + " · 승인자 " + qs(b.approvedBy),
                           qs(b.id));
    baseline_->setCurrentIndex(std::max(0, baseline_->findData(qs(id))));
    layout->addWidget(baseline_);
    adoption_ = new QLabel(this);
    adoption_->setObjectName("comparisonAdoption");
    adoption_->setTextFormat(Qt::PlainText);
    adoption_->setWordWrap(true);
    layout->addWidget(adoption_);
    outcomes_ = outcomeTable(this, "comparisonOutcomes");
    layout->addWidget(outcomes_);
    auto *links = new QHBoxLayout;
    auto *resources = new QPushButton("자원 후보·부하 비교…", this);
    resources->setObjectName("comparisonResources");
    auto *history = new QPushButton("채택·해제 이력…", this);
    history->setObjectName("comparisonHistory");
    auto *refresh = new QPushButton("요약 다시 계산", this);
    refresh->setObjectName("refreshComparison");
    auto *advanced = new QToolButton(this);
    advanced->setObjectName("comparisonAdvanced");
    advanced->setText("계산 출처·전체 진단 펼치기");
    advanced->setCheckable(true);
    for (auto *button : {resources, history, refresh})
        links->addWidget(button);
    links->addWidget(advanced);
    links->addStretch();
    layout->addLayout(links);
    summary_ = new QLabel(this);
    summary_->setObjectName("comparisonSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);
    table_ = new QTableWidget(0, 18, this);
    table_->setObjectName("comparisonTable");
    table_->setAccessibleName("계획 대비 변경 비교 표");
    table_->setHorizontalHeaderLabels({"작업", "기준선 완료", "현재 계획 완료", "계획 완료 편차", "실제 시작",
                                       "실제 완료", "실제 소요", "잔여 기간", "기간 진척(%)", "예측 완료",
                                       "예측 완료 편차", "변경 항목", "기준선 시작", "현재 계획 시작", "예측 시작",
                                       "잔여 총여유", "잔여 자유여유", "예측 임계/상태"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(
        new QLabel("작업별 근거 · 행을 선택하면 아래에서 실제 날짜·보고·관계·자원 배정 변경을 확인합니다.", this));
    layout->addWidget(table_, 1);
    details_ = new QPlainTextEdit(this);
    details_->setReadOnly(true);
    details_->setObjectName("comparisonDetails");
    details_->setAccessibleName("선택 작업의 기준선 대비 입력 변경");
    details_->setMaximumHeight(100);
    layout->addWidget(details_);
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) {
        details_->setPlainText(
            row >= 0 && table_->item(row, 0) ? table_->item(row, 0)->data(Qt::UserRole + 1).toString() : "");
    });
    diagnostics_ = new QPlainTextEdit(this);
    diagnostics_->setObjectName("comparisonDiagnostics");
    diagnostics_->setReadOnly(true);
    diagnostics_->setMaximumHeight(150);
    layout->addWidget(diagnostics_);
    summary_->hide();
    diagnostics_->hide();
    connect(advanced, &QToolButton::toggled, this, [this](bool show) {
        summary_->setVisible(show);
        diagnostics_->setVisible(show);
    });
    connect(refresh, &QPushButton::clicked, this, [this] { recalculate(); });
    connect(resources, &QPushButton::clicked, this, [this] {
        if (!document_.project() || document_.sessionGeneration() != session_)
            return;
        ResourceComparisonDialog dialog(document_, this, baseline_->currentData().toString().toStdString());
        prepareEditorDialog(dialog);
        dialog.exec();
        recalculate();
    });
    connect(history, &QPushButton::clicked, this, [this] {
        LevelingDecisionDialog dialog(document_, this);
        dialog.exec();
    });
    auto *note = new QLabel("기간 단위: 근무일(d) · 시간(h) · 분(m). 편차는 선택 기준선 달력(+지연 / −앞당김), "
                            "실제·잔여·여유는 현재 작업 달력 기준입니다. —는 기준선·유효 결과 없음 또는 "
                            "비교 범위 밖입니다.\n기간 진척=실제 소요/(실제 소요+잔여), 완료 보고=100%. 미보고 작업은 "
                            "진척 미정이며 계획 기간을 잔여로 가정합니다.\n잔여 여유는 현재 작업 달력의 근무시간이며 "
                            "완료 실적은 적용 대상이 아닙니다. 아래 작업 예측 열은 현재 적용 결과이며 조정 전·후의 "
                            "작업/부하는 자원 비교에서 확인합니다.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(baseline_, &QComboBox::currentIndexChanged, this, [this] { recalculate(); });
    subscription_ = doc.subscribe([this](Document::Event event) {
        if (event == Document::Event::Saved)
            return;
        ++generation_;
        outcomes_->setRowCount(0);
        table_->setRowCount(0);
        details_->clear();
        diagnostics_->clear();
        adoption_->setText("입력이 변경되어 이전 요약은 오래됐습니다. 요약을 다시 계산하세요.");
        summary_->setText(adoption_->text());
    });
    recalculate();
}
ComparisonDialog::~ComparisonDialog() {
    document_.unsubscribe(subscription_);
}
void ComparisonDialog::recalculate() {
    if (!document_.project() || document_.sessionGeneration() != session_) {
        reject();
        return;
    }
    const auto snapshot = *document_.project();
    const auto id = baseline_->currentData().toString().toUtf8().toStdString();
    const auto generation = ++generation_;
    table_->setRowCount(0);
    diagnostics_->clear();
    details_->clear();
    outcomes_->setRowCount(0);
    adoption_->setText("완료일·보고 범위·자원 검증을 계산 중…");
    summary_->setText("비교 계산 중…");
    auto *watcher = new QFutureWatcher<Overview>(this);
    connect(watcher, &QFutureWatcher<Overview>::finished, this, [this, watcher, generation] {
        if (generation == generation_) {
            try {
                auto overview = watcher->result();
                if (!document_.project() || document_.sessionGeneration() != session_ ||
                    document_.project()->revision != overview.report.revision) {
                    adoption_->setText("문서가 변경되었습니다. 비교 창을 다시 열어 최신 내용을 확인하세요.");
                    watcher->deleteLater();
                    return;
                }
                display(overview.report);
                const auto &p = *document_.project();
                const auto *base = outcomeBaseline(p, overview.report.baselineId);
                const auto *calendar = base ? &base->input : static_cast<const PlanInput *>(&p);
                const auto finish = base ? std::optional<std::string>{base->projectFinish} : std::nullopt;
                const auto logical = outcomeFinish(overview.logical);
                adoption_->setText(
                    outcomeAdoption(p) + "\n" + outcomeCoverage(p) +
                    QString("\n배정 작업 %1/%2만 자원 검증 · 편차: 기준선 달력 근무시간 (+지연 / −앞당김)")
                        .arg(overview.current.assignedTasks)
                        .arg(overview.current.totalTasks));
                outcomes_->setRowCount(0);
                outcomeRow(outcomes_, "선택 기준선", finish, calendar, finish, {},
                           base ? "승인·동결 계획" : "기준선 없음", "별도 검증 필요");
                outcomeRow(outcomes_, "현재 계획", outcomeFinish(overview.report.plan), calendar, finish, {},
                           p.planLeveling ? "계획 자원 조정 채택" : state(overview.report.plan), "—");
                outcomeRow(outcomes_, p.statusAt ? "실적 반영 · 조정 전" : "논리 계획 · 조정 전", logical, calendar,
                           finish, logical, "일정 " + state(overview.logical) + " · 자원 조정 전",
                           outcomeLoad(overview.before));
                outcomeRow(
                    outcomes_, "현재 적용 일정", outcomeFinish(overview.report.forecast), calendar, finish, logical,
                    QString(p.planLeveling || p.remainingLeveling ? "채택 중 · " : "채택 없음 · ") +
                        (overview.report.forecast.status != CalculationStatus::Valid ? state(overview.report.forecast)
                         : overview.current.status == CalculationStatus::Valid       ? "배정 자원 검증 통과"
                                                                                     : "자원 과부하/미정"),
                    outcomeLoad(overview.current));
            } catch (const std::exception &e) {
                summary_->setText(qErrorText(e));
                adoption_->setText(qErrorText(e));
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, id] {
        Overview value;
        value.report = compareToBaseline(snapshot, id);
        auto logical = snapshot;
        logical.planLeveling.reset();
        logical.remainingLeveling.reset();
        logical.levelingDecisions.clear();
        value.logical =
            (snapshot.planLeveling || snapshot.remainingLeveling) ? calculate(logical) : value.report.forecast;
        value.before = resourceLoad(logical, value.logical);
        value.current = resourceLoad(snapshot, value.report.forecast);
        return value;
    }));
}
void ComparisonDialog::display(const ComparisonReport &r) {
    if (!document_.project() || document_.project()->documentId != r.documentId ||
        document_.project()->revision != r.revision) {
        summary_->setText("비교 계산 중 문서가 변경되었습니다. 비교 창을 다시 열어 최신 내용을 확인하세요.");
        return;
    }
    const auto &project = *document_.project();
    const auto *baseline = outcomeBaseline(project, r.baselineId);
    const auto projectDay =
        taskDayMinutes(baseline ? baseline->input : static_cast<const PlanInput &>(project), Task{});
    QString metadata;
    for (const auto &b : document_.project()->baselines)
        if (b.id == r.baselineId)
            metadata = "기준선 v" + QString::number(b.version) + " · " + qs(b.name) + " · 승인자 " + qs(b.approvedBy) +
                       " · 사유 " + qs(b.reason) + "\n고정 입력 revision " + QString::number(b.inputRevision) +
                       " · 생성(UTC) " + qs(b.createdAt) + "\n기준선 계산 엔진 " + qs(b.engineVersion);
    if (metadata.isEmpty())
        metadata = "기준선 없음 · 편차 미정";
    QStringList context;
    for (const auto &c : r.contextChanges)
        context << qs(c);
    summary_->setText(
        metadata + "\n현재 계획 계산 엔진 " + qs(r.plan.engineVersion) + " · 예측 계산 엔진 " +
        qs(r.forecast.engineVersion) + "\n현재 문서 revision " + QString::number(r.revision) + " · 상태일 " +
        stamp(document_.project()->statusAt) + " · 보고 범위 " + QString::number(r.reportedTasks) + "/" +
        QString::number(r.totalTasks) + "\n프로젝트 완료: 기준선 " + stamp(r.baselineFinish) + " | 현재 계획 " +
        stamp(r.plan.projectFinish) + " (" + variance(r.planFinishVariance, projectDay) + ") | 실적 반영 예측 " +
        stamp(r.forecast.projectFinish) + " (" + variance(r.forecastFinishVariance, projectDay) + ")" +
        (context.isEmpty() ? "" : "\n프로젝트 변경: " + context.join(" · ")));
    table_->setRowCount(int(r.tasks.size()));
    std::unordered_map<std::string, const TaskSchedule *> forecasts;
    if (r.forecast.isForecast && r.forecast.status == CalculationStatus::Valid)
        for (const auto &row : r.forecast.tasks)
            forecasts.emplace(row.taskId, &row);
    std::unordered_map<std::string, const Task *> currentTasks, baselineTasks;
    for (const auto &task : project.tasks)
        currentTasks.emplace(task.id, &task);
    if (baseline)
        for (const auto &task : baseline->input.tasks)
            baselineTasks.emplace(task.id, &task);
    for (int i = 0; i < int(r.tasks.size()); ++i) {
        const auto &row = r.tasks[std::size_t(i)];
        const auto *task = currentTasks.contains(row.taskId) ? currentTasks.at(row.taskId) : nullptr;
        const auto *oldTask = baselineTasks.contains(row.taskId) ? baselineTasks.at(row.taskId) : nullptr;
        const auto day = task ? taskDayMinutes(project, *task) : 0;
        const auto oldDay = oldTask ? taskDayMinutes(baseline->input, *oldTask) : 0;
        QStringList changes;
        for (const auto &c : row.changes)
            changes << qs(c);
        if (!row.reported)
            changes << "상태일 기준 실적 미보고";
        QStringList values{qs(row.name),
                           stamp(row.baselineFinish),
                           stamp(row.planFinish),
                           variance(row.planFinishVariance, oldDay),
                           stamp(row.actualStart),
                           stamp(row.actualFinish),
                           duration(row.actualMinutes, day),
                           duration(row.remainingMinutes, day),
                           row.durationProgressPercent ? QString::number(*row.durationProgressPercent, 'f', 1) : "미정",
                           stamp(row.forecastFinish),
                           variance(row.forecastFinishVariance, oldDay),
                           changes.isEmpty() ? "계획 입력 동일" : changes.join(" · "),
                           stamp(row.baselineStart),
                           stamp(row.planStart),
                           stamp(row.forecastStart)};
        const auto *future = forecasts.contains(row.taskId) ? forecasts.at(row.taskId) : nullptr;
        const bool floatKnown = future && hasScheduleFloat(r.forecast, *future);
        values << (floatKnown ? duration(future->totalFloatMinutes, day) : "—")
               << (floatKnown ? duration(future->freeFloatMinutes, day) : "—")
               << (!future                   ? "—"
                   : future->completedActual ? "실제 완료"
                   : floatKnown              ? (future->critical ? "예측 임계" : "예측 여유")
                                             : "미산정");
        QStringList oldPred, newPred, oldResources, newResources;
        for (const auto &id : row.baselinePredecessors)
            oldPred << qs(id);
        for (const auto &id : row.planPredecessors)
            newPred << qs(id);
        for (const auto &a : row.baselineAssignments)
            oldResources << qs(a);
        for (const auto &a : row.planAssignments)
            newResources << qs(a);
        const auto detail =
            "작업 ID: " + qs(row.taskId) + "\n이름: " + (row.baselineName.empty() ? "—" : qs(row.baselineName)) +
            " → " + qs(row.name) + " | 계획 기간: " + duration(row.baselineDuration, oldDay) + " → " +
            duration(row.planDuration, day) + "\nWBS: " + qs(row.baselineWbsPath) + " → " + qs(row.planWbsPath) +
            "\n선행 ID·종류·지연: " + (oldPred.isEmpty() ? "없음/미설정" : oldPred.join(", ")) + " → " +
            (newPred.isEmpty() ? "없음/미설정" : newPred.join(", ")) +
            "\n자원 배정: " + (oldResources.isEmpty() ? "없음" : oldResources.join(", ")) + " → " +
            (newResources.isEmpty() ? "없음" : newResources.join(", "));
        for (int col = 0; col < values.size(); ++col) {
            auto *item = new QTableWidgetItem(values[col]);
            item->setData(Qt::UserRole, qs(row.taskId));
            item->setData(Qt::UserRole + 1, detail);
            QString unitHelp;
            if (col == 3 || col == 10)
                unitHelp = oldTask ? "선택 기준선의 작업 달력 · " +
                                         taskDurationHelp(baseline->input, *oldTask).section('\n', 0, 0)
                                   : "선택 기준선에 작업 없음 · 편차 미정";
            else if (task && (col == 6 || col == 7 || col == 15 || col == 16))
                unitHelp = "현재 작업 달력 · " + taskDurationHelp(project, *task).section('\n', 0, 0);
            item->setToolTip("작업 ID: " + qs(row.taskId) + (unitHelp.isEmpty() ? "" : "\n" + unitHelp));
            table_->setItem(i, col, item);
        }
    }
    if (table_->rowCount())
        table_->setCurrentCell(0, 0);
    QString diagnostics = "현재 계획: " + state(r.plan) + " · 실적 반영 예측: " + state(r.forecast) + "\n";
    for (const auto &d : r.plan.diagnostics)
        diagnostics += "[계획/" + qs(d.code) + "] " + qs(diagnosticMessage(d)) + "\n";
    for (const auto &d : r.forecast.diagnostics)
        diagnostics += "[예측/" + qs(d.code) + "] " + qs(diagnosticMessage(d)) + "\n";
    diagnostics_->setPlainText(diagnostics);
}
} // namespace ppm
