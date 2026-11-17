#include "formatting/QtDiagnosticText.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/LevelingWitnessDialog.h"
#include "calculation/WorkCalendar.h"
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QFutureWatcher>
#include <QDateTime>
#include <QSignalBlocker>
#include <QColor>
#include <QtConcurrent/QtConcurrentRun>
#include <unordered_map>
#include <set>
#include <map>
#include <algorithm>
namespace jobs {
namespace {
bool sameLevelingSource(const Project &a, const Project &b) {
    PlanInput x = a, y = b;
    x.planLeveling.reset();
    y.planLeveling.reset();
    return x == y && a.statusAt == b.statusAt && a.progress == b.progress &&
           (a.progressPolicy ? a.progressPolicy->policy : retainedProgressPolicy) ==
               (b.progressPolicy ? b.progressPolicy->policy : retainedProgressPolicy);
}
} // namespace
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
QString hours(std::int64_t n) {
    return QString::number(double(n) / 60, 'f', 2);
}
QString work(std::int64_t n) {
    return QString::number(double(n) / 6000, 'f', 2);
}
QString state(CalculationStatus s) {
    return s == CalculationStatus::Cancelled    ? "취소"
           : s == CalculationStatus::Stale      ? "오래된 결과"
           : s == CalculationStatus::Valid      ? "배정 자원 검증 통과"
           : s == CalculationStatus::Incomplete ? "미정 / 입력 불완전"
                                                : "과부하 / 배치 불가";
}
QTableWidget *table(QWidget *parent, const char *name, const QStringList &columns) {
    auto *t = new QTableWidget(0, columns.size(), parent);
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(columns);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return t;
}
void fillLoad(QTableWidget *t, const Project &p, const ResourceLoad &load) {
    std::unordered_map<std::string, QString> resources, tasks;
    for (const auto &r : p.resources)
        resources.emplace(r.id, qs(r.name));
    for (const auto &r : p.tasks)
        tasks.emplace(r.id, qs(r.name));
    t->setRowCount(int(load.days.size()));
    for (int i = 0; i < t->rowCount(); ++i) {
        const auto &d = load.days[std::size_t(i)];
        QStringList assigned;
        for (const auto &id : d.taskIds)
            assigned << tasks.at(id);
        QString ratio = d.capacityVaries   ? "기간 내 변동"
                        : !d.capacity      ? "미정"
                        : *d.capacity == 0 ? "가용 0%"
                                           : QString::number(double(d.peakUnits) / *d.capacity * 100, 'f', 1) + "%";
        QStringList values{resources.at(d.resourceId),
                           qs(d.date),
                           d.capacityVaries ? (d.capacity ? "기간 내 변동" : "변동·미정 포함")
                           : d.capacity     ? QString::number(*d.capacity) + "%"
                                            : "미정",
                           work(d.demandUnitMinutes),
                           QString::number(d.peakUnits) + "%",
                           ratio,
                           d.capacity ? hours(d.overloadMinutes) : "미정",
                           d.capacity ? work(d.excessUnitMinutes) : "미정",
                           assigned.join(", ")};
        for (int c = 0; c < values.size(); ++c) {
            auto *item = new QTableWidgetItem(values[c]);
            item->setData(Qt::UserRole, qs(d.resourceId));
            item->setToolTip("자원 ID: " + qs(d.resourceId));
            if (!d.capacity) {
                item->setBackground(QColor("#fff2c6"));
                item->setForeground(QColor("#4d3a00"));
            } else if (d.overloadMinutes) {
                item->setBackground(QColor("#ffe0e0"));
                item->setForeground(QColor("#6f1720"));
            }
            t->setItem(i, c, item);
        }
    }
}
std::int64_t overload(const ResourceLoad &l) {
    std::int64_t n = 0;
    for (const auto &d : l.days)
        n += d.overloadMinutes;
    return n;
}
QString overloadText(const ResourceLoad &l) {
    return std::any_of(l.days.begin(), l.days.end(), [](const auto &d) { return !d.capacity; })
               ? "미정 (알려진 구간 " + hours(overload(l)) + "h)"
               : hours(overload(l)) + "h";
}
struct Computed {
    LevelingResult main;
    std::optional<LevelingResult> other;
    std::optional<Project> otherProject;
    QString error;
};
} // namespace
ResourceComparisonDialog::ResourceComparisonDialog(Document &doc, QWidget *parent)
    : QDialog(parent), document_(doc), sourceSession_(doc.sessionGeneration()) {
    setObjectName("resourceComparisonDialog");
    setWindowTitle("기간별 자원 부하 · 평준화 시나리오 비교");
    resize(1480, 850);
    auto *layout = new QVBoxLayout(this);
    auto *sources = new QHBoxLayout;
    layout->addLayout(sources);
    sourceChoice_ = new QComboBox(this);
    sourceChoice_->setObjectName("levelingSource");
    sourceChoice_->addItem("현재 입력 → 새 시나리오 A", QString{});
    otherChoice_ = new QComboBox(this);
    otherChoice_->setObjectName("levelingOther");
    otherChoice_->addItem("B 비교 없음", QString{});
    for (const auto &s : doc.project()->scenarios) {
        auto label = qs(s.name) + " · rev " + QString::number(s.inputRevision) + " · " + qs(s.createdAt);
        sourceChoice_->addItem(label, qs(s.id));
        otherChoice_->addItem(label, qs(s.id));
    }
    sources->addWidget(new QLabel("A 입력", this));
    sources->addWidget(sourceChoice_, 1);
    sources->addWidget(new QLabel("저장된 B", this));
    sources->addWidget(otherChoice_, 1);
    auto *controls = new QHBoxLayout;
    layout->addLayout(controls);
    priority_ = new QComboBox(this);
    priority_->setObjectName("levelingPriority");
    priority_->addItem("작업 행 순서", "task-order");
    priority_->addItem("짧은 잔여 기간 우선", "shortest-first");
    priority_->addItem("긴 잔여 기간 우선", "longest-first");
    priority_->addItem("총여유 작은 작업 우선", "least-float-first");
    priority_->addItem("마감일 빠른 작업 우선", "deadline-first");
    horizon_ = new QSpinBox(this);
    horizon_->setObjectName("levelingHorizon");
    horizon_->setRange(1, 5200);
    horizon_->setValue(260);
    horizon_->setSuffix(" 근무일");
    controls->addWidget(new QLabel("준비된 작업 중 우선순위", this));
    controls->addWidget(priority_);
    controls->addWidget(new QLabel("계획 시작부터 탐색 범위", this));
    controls->addWidget(horizon_);
    auto *run = new QPushButton("부하 계산 / 평준화 비교", this);
    run->setObjectName("runLeveling");
    controls->addWidget(run);
    witnessA_ = new QPushButton("A 총여유 근거", this);
    witnessA_->setObjectName("showLevelingWitnessA");
    witnessB_ = new QPushButton("B 총여유 근거", this);
    witnessB_->setObjectName("showLevelingWitnessB");
    for (auto *button : {witnessA_, witnessB_}) {
        button->setEnabled(false);
        button->setToolTip("구간 재분할의 검증된 증인이 있는 계산 결과에서 열 수 있습니다.");
        controls->addWidget(button);
    }
    controls->addStretch();
    summary_ = new QLabel(this);
    summary_->setObjectName("levelingSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setMinimumHeight(125);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("resourceTabs");
    layout->addWidget(tabs, 1);
    tasks_ = table(this, "levelingTasks",
                   {"작업", "원본 시작", "원본 완료", "A 시작", "A 완료", "잔여 시작 지연(h)", "B 시작", "B 완료",
                    "실제 날짜", "A 총여유(h)", "A 자유여유(h)", "A 임계/상태"});
    tabs->addTab(tasks_, "일정 비교");
    const QStringList columns{"자원",        "날짜",           "가용량",       "수요 공수(h)", "최대 배정률",
                              "최대 사용률", "과부하 시간(h)", "초과 공수(h)", "작업"};
    before_ = table(this, "loadBefore", columns);
    tabs->addTab(before_, "A 원본 부하");
    after_ = table(this, "loadAfter", columns);
    tabs->addTab(after_, "A 평준화 후");
    other_ = table(this, "loadOther", columns);
    tabs->addTab(other_, "B 평준화 후");
    diagnostics_ = new QPlainTextEdit(this);
    diagnostics_->setObjectName("levelingDiagnostics");
    diagnostics_->setReadOnly(true);
    diagnostics_->setMaximumHeight(110);
    layout->addWidget(diagnostics_);
    auto *note = new QLabel("전체 작업 범위 · 작업 유형/공수와 수동 분할·작업/자원 달력 준수 · 자원에 따른 자동 추가 "
                            "분할 없음 · 원본보다 앞당기지 않음 · 같은 우선순위는 행 순서\n상태일 설정 시 잔여 수요만 "
                            "비교하고 실제 날짜는 고정합니다. 부하 공수는 자원별 인시/장비시입니다. 부하 표는 수요가 "
                            "있는 날짜만 표시합니다. 여유·임계의 구간 유지/재생성 기준과 작업별 재배치 증인, "
                            "미산정 사유는 진단에서 확인하세요.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *saveRow = new QHBoxLayout;
    layout->addLayout(saveRow);
    name_ = new QLineEdit(this);
    name_->setObjectName("scenarioName");
    name_->setPlaceholderText("보존할 시나리오 이름");
    save_ = new QPushButton("A 입력·정책을 새 시나리오로 보존", this);
    save_->setObjectName("saveScenario");
    save_->setEnabled(false);
    saveRow->addWidget(name_, 1);
    saveRow->addWidget(save_);
    auto *adoption = new QHBoxLayout;
    reason_ = new QLineEdit(this);
    reason_->setObjectName("levelingReason");
    reason_->setPlaceholderText("채택 사유 (필수)");
    adopt_ = new QPushButton("A 결과를 현재 일정으로 채택", this);
    adopt_->setObjectName("adoptLeveling");
    adopt_->setEnabled(false);
    release_ = new QPushButton("채택 해제", this);
    release_->setObjectName("clearLeveling");
    cancel_ = new QPushButton("계산 / 제출 취소", this);
    cancel_->setObjectName("cancelLeveling");
    cancel_->setEnabled(false);
    layout->addWidget(reason_);
    adoption->addWidget(adopt_);
    adoption->addWidget(release_);
    adoption->addWidget(cancel_);
    adoption->addStretch();
    auto *close = new QPushButton("닫기", this);
    adoption->addWidget(close);
    layout->addLayout(adoption);
    connect(adopt_, &QPushButton::clicked, this, [this] { applySelection(false); });
    connect(release_, &QPushButton::clicked, this, [this] { applySelection(true); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    connect(close, &QPushButton::clicked, this, &ResourceComparisonDialog::reject);
    connect(run, &QPushButton::clicked, this, [this] { recalculate(); });
    connect(priority_, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
    connect(horizon_, &QSpinBox::valueChanged, this, [this] { invalidate(); });
    connect(sourceChoice_, &QComboBox::currentIndexChanged, this, [this] { recalculate(); });
    connect(otherChoice_, &QComboBox::currentIndexChanged, this, [this] { recalculate(); });
    connect(save_, &QPushButton::clicked, this, [this] { saveScenario(); });
    connect(witnessA_, &QPushButton::clicked, this, [this] { openWitness(false); });
    connect(witnessB_, &QPushButton::clicked, this, [this] { openWitness(true); });
    witnessListener_ = document_.subscribe([this](Document::Event event) {
        if (event != Document::Event::Saved || !anchor_ || !document_.project() ||
            document_.sessionGeneration() != sourceSession_ || document_.project()->documentId != anchor_->documentId ||
            document_.project()->revision != anchor_->revision) {
            closeWitnesses();
            witnessA_->setEnabled(false);
            witnessB_->setEnabled(false);
            witnessA_->setToolTip("문서가 변경되었습니다. 평준화 비교를 다시 계산하세요.");
            witnessB_->setToolTip(witnessA_->toolTip());
        }
    });
    recalculate();
}
void ResourceComparisonDialog::invalidate() {
    closeWitnesses();
    witnessA_->setEnabled(false);
    witnessB_->setEnabled(false);
    witnessOther_.reset();
    witnessOtherSource_.reset();
    if (control_)
        control_->cancel();
    cancel_->setEnabled(false);
    if (!posting_) {
        priority_->setEnabled(sourceChoice_->currentData().toString().isEmpty());
        horizon_->setEnabled(sourceChoice_->currentData().toString().isEmpty());
    }
    ++generation_;
    result_.reset();
    source_.reset();
    anchor_.reset();
    adopt_->setEnabled(false);
    release_->setEnabled(!posting_ && !document_.readOnly() && document_.project() &&
                         (document_.project()->planLeveling || document_.project()->remainingLeveling));
    save_->setEnabled(false);
    tasks_->setRowCount(0);
    before_->setRowCount(0);
    after_->setRowCount(0);
    other_->setRowCount(0);
    diagnostics_->clear();
    summary_->setText("조건을 변경했습니다. 부하 계산 / 평준화 비교를 실행하세요.");
}
void ResourceComparisonDialog::recalculate() {
    if (posting_ || !document_.project())
        return;
    invalidate();
    control_ = std::make_shared<CalculationControl>();
    cancel_->setEnabled(true);
    const auto session = document_.sessionGeneration();
    const auto generation = generation_;
    const Project current = *document_.project();
    Project snapshot = current;
    const auto selected = str(sourceChoice_->currentData().toString()),
               otherId = str(otherChoice_->currentData().toString());
    std::optional<LevelingScenario> secondary;
    std::string replayPolicy;
    for (const auto &s : current.scenarios) {
        if (s.id == selected) {
            snapshot = scenarioProject(s);
            replayPolicy = s.policy;
            QSignalBlocker b1(priority_), b2(horizon_);
            priority_->setCurrentIndex(priority_->findData(qs(s.priority)));
            horizon_->setValue(s.horizonWorkDays);
        }
        if (s.id == otherId)
            secondary = s;
    }
    priority_->setEnabled(selected.empty());
    horizon_->setEnabled(selected.empty());
    auto priority = str(priority_->currentData().toString());
    int horizon = horizon_->value();
    summary_->setText("자원 수요·시나리오 계산 중…");
    auto *watcher = new QFutureWatcher<Computed>(this);
    connect(watcher, &QFutureWatcher<Computed>::finished, this,
            [this, watcher, generation, snapshot, current, selected, session, replayPolicy] {
                auto computed = watcher->result();
                watcher->deleteLater();
                if (generation != generation_)
                    return;
                cancel_->setEnabled(false);
                if (!computed.error.isEmpty()) {
                    summary_->setText(computed.error);
                    return;
                }
                if (!document_.project() || document_.sessionGeneration() != session ||
                    document_.project()->documentId != current.documentId ||
                    document_.project()->revision != current.revision) {
                    summary_->setText("계산 중 문서가 변경되었습니다. 다시 계산하세요.");
                    return;
                }
                source_ = snapshot;
                result_ = computed.main;
                anchor_ = current;
                sourceSession_ = session;
                candidatePolicy_ = replayPolicy.empty() ? levelingPolicyFor(snapshot) : replayPolicy;
                const auto &r = *result_;
                adopt_->setEnabled(!document_.readOnly() && sameLevelingSource(snapshot, current) &&
                                   r.leveled.status == CalculationStatus::Valid &&
                                   r.after.status == CalculationStatus::Valid);
                save_->setEnabled(selected.empty() && !document_.readOnly());
                auto finish = [](const ScheduleResult &s) { return s.projectFinish ? qs(*s.projectFinish) : "—"; };
                auto units = [&](const ResourceLoad &l) {
                    std::map<std::string, std::int64_t> totals;
                    for (const auto &d : l.days)
                        for (const auto &resource : snapshot.resources)
                            if (resource.id == d.resourceId)
                                totals[resource.type] += d.demandUnitMinutes;
                    return "인력 " + work(totals["labor"]) + " 인시 · 장비 " + work(totals["equipment"]) + " 장비시";
                };
                QString summary =
                    "A 원본 rev " + QString::number(snapshot.revision) + " · " + qs(snapshot.documentId) + " · " +
                    (snapshot.statusAt ? "잔여 예측 / 상태일 " + qs(*snapshot.statusAt) : "계획") + "\n완료: 원본 " +
                    finish(r.original) + " → A " + finish(r.leveled) + " · " +
                    state(r.leveled.status == CalculationStatus::Valid ? r.after.status : r.leveled.status) +
                    "\n배정 작업 " + QString::number(r.before.assignedTasks) + "/" +
                    QString::number(r.before.totalTasks) + " · 자원별 과부하 시간 합계 " + overloadText(r.before) +
                    " → " + (r.leveled.status == CalculationStatus::Valid ? overloadText(r.after) : "—") + " · 수요 " +
                    units(r.before) + (r.leveled.status == CalculationStatus::Valid ? " → " + units(r.after) : "") +
                    "\n정책 " + qs(r.leveled.calculationPolicy) + " · 탐색 " + QString::number(r.horizonWorkDays) +
                    " 기본 달력 근무일 · 채택은 검증 후 명시 적용 · 일정 입력 변경 시 채택 해제";
                if (snapshot.progressPolicy)
                    summary += "\n진척 정책 " + qs(snapshot.progressPolicy->policy) + " · 담당자 " +
                               qs(snapshot.progressPolicy->adoptedBy) + " · " + qs(snapshot.progressPolicy->reason) +
                               (snapshot.statusAt ? "" : " · 비활성");
                if (current.planLeveling || current.remainingLeveling)
                    summary += "\n현재 " + QString(current.planLeveling ? "계획" : "잔여 예측") +
                               " 평준화 채택 중 · 비교 원본은 채택 전 논리 일정";
                if (!sameLevelingSource(snapshot, current))
                    summary += "\nA 입력이 현재 일정과 다릅니다. 현재 일정으로 직접 채택할 수 없습니다.";
                if (computed.other && computed.otherProject) {
                    summary += "\nB 원본 rev " + QString::number(computed.otherProject->revision) + " · " +
                               qs(computed.other->priority) + " / " + QString::number(computed.other->horizonWorkDays) +
                               " 근무일 · 완료 " + finish(computed.other->leveled) + " · " +
                               state(computed.other->leveled.status == CalculationStatus::Valid
                                         ? computed.other->after.status
                                         : computed.other->leveled.status);
                    if (static_cast<const PlanInput &>(snapshot) !=
                            static_cast<const PlanInput &>(*computed.otherProject) ||
                        snapshot.statusAt != computed.otherProject->statusAt ||
                        snapshot.progress != computed.otherProject->progress)
                        summary += " · A/B 입력이 다릅니다: 우선순위만의 효과로 해석할 수 없습니다.";
                }
                summary_->setText(summary);
                fillLoad(before_, snapshot, r.before);
                fillLoad(after_, snapshot, r.after);
                if (computed.otherProject && computed.other)
                    fillLoad(other_, *computed.otherProject, computed.other->after);
                std::unordered_map<std::string, const TaskSchedule *> original, leveled, other;
                for (const auto &row : r.original.tasks)
                    original.emplace(row.taskId, &row);
                for (const auto &row : r.leveled.tasks)
                    leveled.emplace(row.taskId, &row);
                if (computed.other)
                    for (const auto &row : computed.other->leveled.tasks)
                        other.emplace(row.taskId, &row);
                std::vector<Task> all = snapshot.tasks;
                if (computed.otherProject)
                    for (const auto &t : computed.otherProject->tasks)
                        if (std::none_of(all.begin(), all.end(), [&](const auto &a) { return a.id == t.id; }))
                            all.push_back(t);
                using CalendarKey = std::pair<std::string, std::vector<std::pair<std::string, std::string>>>;
                std::map<CalendarKey, WorkCalendar> taskCalendars;
                tasks_->setRowCount(int(all.size()));
                for (int i = 0; i < tasks_->rowCount(); ++i) {
                    const auto &t = all[std::size_t(i)];
                    const auto *a = original.contains(t.id) ? original.at(t.id) : nullptr;
                    const auto *b = leveled.contains(t.id) ? leveled.at(t.id) : nullptr;
                    const auto *c = other.contains(t.id) ? other.at(t.id) : nullptr;
                    QString delay = "—";
                    if (a && b) {
                        CalendarKey key{t.calendarId, {}};
                        if (isEffort(snapshot))
                            for (const auto &gap : t.breaks)
                                key.second.emplace_back(gap.start, gap.finish);
                        const auto &calendar = taskCalendars.try_emplace(std::move(key), snapshot, t).first->second;
                        delay = hours(calendar.offset(b->remainingStart.value_or(b->start)) -
                                      calendar.offset(a->remainingStart.value_or(a->start)));
                    }
                    const auto *actual = progressAsOf(snapshot, t.id);
                    QStringList values{qs(t.name),
                                       a ? qs(a->start) : "—",
                                       a ? qs(a->finish) : "—",
                                       b ? qs(b->start) : "—",
                                       b ? qs(b->finish) : "—",
                                       delay,
                                       c ? qs(c->start) : "—",
                                       c ? qs(c->finish) : "—",
                                       actual && actual->actualStart ? "고정 보존" : "—",
                                       b && hasScheduleFloat(r.leveled, *b) ? hours(b->totalFloatMinutes) : "—",
                                       b && hasScheduleFloat(r.leveled, *b) ? hours(b->freeFloatMinutes) : "—",
                                       b && hasScheduleFloat(r.leveled, *b)
                                           ? (b->critical ? "평준화 임계" : "평준화 여유")
                                           : "미산정"};
                    for (int col = 0; col < values.size(); ++col) {
                        auto *item = new QTableWidgetItem(values[col]);
                        item->setData(Qt::UserRole, qs(t.id));
                        item->setToolTip(qs(t.id));
                        tasks_->setItem(i, col, item);
                    }
                }
                QStringList messages;
                auto append = [&](const QString &label, const std::vector<Diagnostic> &ds) {
                    for (const auto &d : ds) {
                        QStringList ids;
                        for (const auto &id : d.taskIds)
                            ids << qs(id);
                        messages << label + " [" + qs(d.code) + "] " + qs(diagnosticMessage(d)) +
                                        (ids.empty() ? "" : " · " + ids.join(", "));
                    }
                };
                append("A 원본 일정", r.original.diagnostics);
                append("A 원본 부하", r.before.diagnostics);
                append("A 평준화", r.diagnostics);
                append("A 결과", r.after.diagnostics);
                if (computed.other)
                    append("B 평준화", computed.other->diagnostics);
                diagnostics_->setPlainText(messages.join('\n'));
                auto evidence = [](const ScheduleResult &schedule) {
                    return schedule.status == CalculationStatus::Valid && schedule.levelingFloatCalculated &&
                           schedule.levelingWitnesses && !schedule.levelingWitnesses->tasks.empty();
                };
                witnessA_->setEnabled(evidence(r.leveled));
                witnessA_->setToolTip(
                    witnessA_->isEnabled()
                        ? "현재 날짜와 작업별 총여유 재배치 날짜를 비교합니다."
                        : "이 결과에는 구간 재분할 증인이 없습니다. 미산정 사유와 계산 기준은 진단에서 확인하세요.");
                if (computed.other && computed.otherProject) {
                    witnessB_->setEnabled(evidence(computed.other->leveled));
                    witnessOther_ = std::move(computed.other->leveled);
                    witnessOtherSource_ = std::move(computed.otherProject);
                }
                witnessB_->setToolTip(witnessB_->isEnabled()
                                          ? "저장된 B 입력의 검증된 총여유 재배치 날짜를 비교합니다."
                                          : "B 비교를 선택하고 구간 재분할 증인이 있는 결과를 계산하세요.");
            });
    watcher->setFuture(QtConcurrent::run([snapshot, priority, horizon, secondary, replayPolicy, control = control_] {
        Computed c;
        try {
            EvaluationScope evaluation(control);
            c.main = levelResources(snapshot, priority, horizon, replayPolicy);
            if (secondary) {
                c.otherProject = scenarioProject(*secondary);
                c.other =
                    levelResources(*c.otherProject, secondary->priority, secondary->horizonWorkDays, secondary->policy);
            }
        } catch (const CalculationCancelled &) {
            c.error = "평준화 계산을 취소했습니다.";
        } catch (const std::exception &e) {
            c.error = qErrorText(e);
        }
        return c;
    }));
}
ResourceComparisonDialog::~ResourceComparisonDialog() {
    document_.unsubscribe(witnessListener_);
    if (control_)
        control_->cancel();
}
void ResourceComparisonDialog::reject() {
    closeWitnesses();
    if (control_)
        control_->cancel();
    QDialog::reject();
}
void ResourceComparisonDialog::applySelection(bool clear) {
    try {
        if (posting_ || document_.readOnly())
            return;
        if (document_.sessionGeneration() != sourceSession_)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        if (!clear &&
            (!anchor_ || !source_ || !result_ || document_.sessionGeneration() != sourceSession_ ||
             document_.project()->documentId != anchor_->documentId ||
             document_.project()->revision != anchor_->revision || !sameLevelingSource(*source_, *document_.project())))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        EditCommand command{std::in_place_type<ClearLevelingEdit>};
        if (!clear) {
            LevelingSelection s;
            s.priority = result_->priority;
            s.horizonWorkDays = result_->horizonWorkDays;
            s.policy = candidatePolicy_;
            s.reason = str(reason_->text().trimmed());
            s.adoptedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
            s.sourceDocumentId = anchor_->documentId;
            s.sourceRevision = anchor_->revision;
            command = EditCommand{std::in_place_type<AdoptLevelingEdit>, AdoptLevelingEdit{s}};
        }
        auto request = document_.makeEditRequest(std::move(command));
        if (control_)
            control_->cancel();
        ++generation_;
        control_ = std::make_shared<CalculationControl>();
        posting_ = true;
        closeWitnesses();
        witnessA_->setEnabled(false);
        witnessB_->setEnabled(false);
        save_->setEnabled(false);
        adopt_->setEnabled(false);
        release_->setEnabled(false);
        cancel_->setEnabled(true);
        sourceChoice_->setEnabled(false);
        otherChoice_->setEnabled(false);
        priority_->setEnabled(false);
        horizon_->setEnabled(false);
        summary_->setText("평준화 채택 / 해제 검증 중…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>(this);
        connect(watcher, &QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>::finished, this, [this, watcher] {
            auto prepared = watcher->result();
            watcher->deleteLater();
            posting_ = false;
            cancel_->setEnabled(false);
            const auto state = document_.applyPreparedEdit(*prepared);
            sourceChoice_->setEnabled(true);
            otherChoice_->setEnabled(true);
            if (state == EditState::Applied || state == EditState::NoChange) {
                {
                    QSignalBlocker block(sourceChoice_);
                    sourceChoice_->setCurrentIndex(0);
                }
                recalculate();
            } else {
                invalidate();
                summary_->setText(prepared->failure() ? qs(diagnosticMessage(*prepared->failure()))
                                  : state == EditState::Cancelled
                                      ? "평준화 채택 / 해제를 취소했습니다."
                                      : "문서가 변경되어 적용하지 않았습니다. 다시 계산하세요.");
            }
        });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), control = control_]() mutable {
            return Document::prepareEdit(std::move(request), control);
        }));
    } catch (const std::exception &e) {
        diagnostics_->setPlainText(qErrorText(e));
    }
}
void ResourceComparisonDialog::closeWitnesses() {
    for (auto *window : {&witnessAWindow_, &witnessBWindow_}) {
        if (*window)
            (*window)->close();
        *window = nullptr;
    }
}
void ResourceComparisonDialog::openWitness(bool other) {
    if (posting_ || !anchor_ || !document_.project() || document_.sessionGeneration() != sourceSession_ ||
        document_.project()->documentId != anchor_->documentId || document_.project()->revision != anchor_->revision) {
        closeWitnesses();
        witnessA_->setEnabled(false);
        witnessB_->setEnabled(false);
        return;
    }
    const auto *project =
        other ? (witnessOtherSource_ ? &*witnessOtherSource_ : nullptr) : (source_ ? &*source_ : nullptr);
    const auto *schedule =
        other ? (witnessOther_ ? &*witnessOther_ : nullptr) : (result_ ? &result_->leveled : nullptr);
    if (!project || !schedule || !schedule->levelingWitnesses || schedule->levelingWitnesses->tasks.empty())
        return;
    auto &window = other ? witnessBWindow_ : witnessAWindow_;
    if (window && window->isVisible()) {
        window->raise();
        window->activateWindow();
        return;
    }
    const auto focus =
        tasks_->currentItem() ? str(tasks_->currentItem()->data(Qt::UserRole).toString()) : std::string{};
    window = new LevelingWitnessDialog(
        *project, *schedule, focus,
        other ? "B · " + otherChoice_->currentText() : "A · " + sourceChoice_->currentText(), this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}
void ResourceComparisonDialog::saveScenario() {
    if (!source_ || !result_ || !sourceChoice_->currentData().toString().isEmpty())
        return;
    try {
        auto id = document_.saveScenario(str(name_->text().trimmed()), result_->priority, result_->horizonWorkDays,
                                         QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString(),
                                         source_->documentId, source_->revision);
        const auto &s = document_.project()->scenarios.back();
        auto label = qs(s.name) + " · rev " + QString::number(s.inputRevision) + " · " + qs(s.createdAt);
        {
            QSignalBlocker b(sourceChoice_);
            sourceChoice_->addItem(label, qs(id));
            otherChoice_->addItem(label, qs(id));
            sourceChoice_->setCurrentIndex(sourceChoice_->count() - 1);
        }
        recalculate();
    } catch (const std::exception &e) {
        diagnostics_->setPlainText(qErrorText(e));
    }
}
} // namespace jobs
