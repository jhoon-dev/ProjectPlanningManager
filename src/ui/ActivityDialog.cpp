#include "ui/ActivityDialog.h"
#include "ui/EditorUi.h"
#include "ui/ExchangeOperation.h"
#include "formatting/QtDiagnosticText.h"
#include "domain/Activities.h"
#include "calculation/ActivitySummary.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QString percent(std::optional<int> v) {
    return v ? QString::number(double(*v) / 100, 'f', 2) + "%" : "미정";
}
void cell(QTableWidget *table, int row, int column, const QString &value, bool editable = true) {
    auto *item = new QTableWidgetItem(value);
    if (!editable)
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, column, item);
}
} // namespace
ActivityDialog::ActivityDialog(Document &document, const std::string &initial, QWidget *parent) : QDialog(parent) {
    setObjectName("activityExtensionDialog");
    setWindowTitle("Steps · LOE · WBS Summary");
    resize(1050, 760);
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Steps는 물리 진척 보고입니다. LOE·WBS Summary는 원본 작업을 요약하는 지원 구간이며, 실행 "
                            "작업·CPM 관계·자원·원가 합계에 추가하지 않습니다.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    auto *stepsPage = new QWidget(tabs);
    auto *stepsLayout = new QVBoxLayout(stepsPage);
    tabs->addTab(stepsPage, "Steps·진척");
    auto *task = new QComboBox(this);
    task->setObjectName("activityTask");
    if (document.project())
        for (const auto &t : document.project()->tasks)
            task->addItem(qs(t.name) + " [" + qs(t.id) + "]", qs(t.id));
    const auto selected = task->findData(qs(initial));
    if (selected >= 0)
        task->setCurrentIndex(selected);
    stepsLayout->addWidget(task);
    auto *physical = new QCheckBox("물리 진척/EVM에 Steps 보고 사용 (수동 물리 진척과 합산하지 않음)", this);
    physical->setObjectName("physicalFromSteps");
    stepsLayout->addWidget(physical);
    auto *steps = new QTableWidget(0, 4, this);
    steps->setObjectName("activitySteps");
    steps->setHorizontalHeaderLabels({"Step ID", "이름", "가중치", "진척(%)"});
    steps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    stepsLayout->addWidget(steps);
    auto *row = new QHBoxLayout;
    stepsLayout->addLayout(row);
    auto button = [&](const char *name, const QString &label, QHBoxLayout *box) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        box->addWidget(b);
        return b;
    };
    auto *addStep = button("addActivityStep", "Step 추가", row);
    auto *removeStep = button("removeActivityStep", "선택 Step 제거", row);
    auto *saveSteps = button("saveActivitySteps", "Steps 정의 저장", row);
    auto *clearSteps = button("clearActivitySteps", "Steps 정의 삭제", row);
    auto *by = new QLineEdit(this);
    by->setObjectName("stepReporter");
    auto *reason = new QLineEdit(this);
    reason->setObjectName("stepReason");
    auto *metadata = new QFormLayout;
    metadata->addRow("보고자", by);
    metadata->addRow("보고/정정 사유", reason);
    stepsLayout->addLayout(metadata);
    auto *record = new QPushButton("현재 상태일의 Steps 진척 보고·정정", this);
    record->setObjectName("recordActivitySteps");
    stepsLayout->addWidget(record);
    auto *metrics = new QLabel(this);
    metrics->setObjectName("activityProgressMetrics");
    metrics->setWordWrap(true);
    stepsLayout->addWidget(metrics);
    auto *supportPage = new QWidget(tabs);
    auto *supportLayout = new QVBoxLayout(supportPage);
    tabs->addTab(supportPage, "LOE·WBS Summary");
    auto *kind = new QComboBox(this);
    kind->setObjectName("supportActivityKind");
    kind->addItem("LOE · 기준 작업 시작~완료", "loe");
    kind->addItem("WBS Summary · 하위 실제 작업 범위", "wbs-summary");
    auto *name = new QLineEdit(this);
    name->setObjectName("supportActivityName");
    auto *start = new QComboBox(this);
    start->setObjectName("supportStartTask");
    auto *finish = new QComboBox(this);
    finish->setObjectName("supportFinishTask");
    for (int i = 0; i < task->count(); ++i) {
        start->addItem(task->itemText(i), task->itemData(i));
        finish->addItem(task->itemText(i), task->itemData(i));
    }
    auto *wbs = new QComboBox(this);
    wbs->setObjectName("supportWbs");
    wbs->addItem("프로젝트 전체", "");
    auto *calendar = new QComboBox(this);
    calendar->setObjectName("supportCalendar");
    calendar->addItem("프로젝트 기본 달력", "");
    if (document.project()) {
        for (const auto &w : document.project()->wbsNodes)
            wbs->addItem(qs(w.name) + " [" + qs(w.id) + "]", qs(w.id));
        for (const auto &c : document.project()->calendars)
            calendar->addItem(qs(c.name), qs(c.id));
    }
    const auto updateKind = [kind, start, finish, wbs] {
        const bool loe = kind->currentData().toString() == "loe";
        start->setEnabled(loe);
        finish->setEnabled(loe);
        wbs->setEnabled(!loe);
    };
    connect(kind, &QComboBox::currentIndexChanged, this, [updateKind](int) { updateKind(); });
    updateKind();
    auto *form = new QFormLayout;
    form->addRow("종류", kind);
    form->addRow("지원 활동 이름", name);
    form->addRow("시작 기준 작업", start);
    form->addRow("완료 기준 작업", finish);
    form->addRow("WBS 범위", wbs);
    form->addRow("기간 계산 달력", calendar);
    supportLayout->addLayout(form);
    auto *supportButtons = new QHBoxLayout;
    supportLayout->addLayout(supportButtons);
    auto *saveSupport = button("saveSupportActivity", "지원 활동 추가", supportButtons);
    auto *deleteSupport = button("deleteSupportActivity", "선택 지원 활동 삭제", supportButtons);
    auto *editSupport = button("editSupportActivity", "선택 정의 보기/편집", supportButtons);
    auto *newSupport = button("newSupportActivity", "새 지원 활동", supportButtons);
    auto *intervals = new QTableWidget(0, 7, this);
    intervals->setObjectName("supportActivityIntervals");
    intervals->setHorizontalHeaderLabels({"이름", "종류", "시작", "완료", "기간(분)", "원본 작업 수", "상태 / ID"});
    intervals->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    supportLayout->addWidget(intervals);
    auto *error = new QLabel(this);
    error->setObjectName("activityExtensionStatus");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    layout->addWidget(error);
    auto *bottom = new QHBoxLayout;
    layout->addLayout(bottom);
    auto *refresh = button("refreshActivities", "최신 입력·일정 다시 읽기", bottom);
    auto *close = button("closeActivities", "닫기", bottom);
    const bool editable = document.project() && !document.readOnly();
    for (auto *b : {addStep, removeStep, saveSteps, clearSteps, record, saveSupport, deleteSupport, newSupport})
        b->setEnabled(editable);
    steps->setEditTriggers(editable ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                    : QAbstractItemView::NoEditTriggers);
    physical->setEnabled(editable);
    // Shared state is owned by the dialog's signal closures, never by an asynchronous worker.
    struct State {
        std::uint64_t session = 0, revision = 0;
        std::string definitionId, supportId, definitionName = "Steps";
    };
    auto state = std::make_shared<State>();
    auto current = [&, state] {
        if (!document.project() || document.sessionGeneration() != state->session ||
            document.project()->revision != state->revision)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    };
    auto loadSteps = [&, state, task, physical, steps, metrics] {
        if (!document.project())
            return;
        state->session = document.sessionGeneration();
        state->revision = document.project()->revision;
        state->definitionId.clear();
        state->definitionName = "Steps";
        const auto id = task->currentData().toString().toStdString();
        steps->setRowCount(0);
        physical->setChecked(true);
        const auto &p = *document.project();
        const auto *reported = p.statusAt ? stepsAsOf(p, id, *p.statusAt) : nullptr;
        for (const auto &a : p.activities)
            if (a.kind == "steps" && a.taskId == id) {
                state->definitionId = a.id;
                state->definitionName = a.name;
                physical->setChecked(a.physicalFromSteps);
                for (const auto &s : a.steps) {
                    const auto r = steps->rowCount();
                    steps->insertRow(r);
                    cell(steps, r, 0, qs(s.id), false);
                    cell(steps, r, 1, qs(s.name));
                    cell(steps, r, 2, QString::number(s.weight));
                    int value = 0;
                    if (reported)
                        for (const auto &o : reported->steps)
                            if (o.step.id == s.id)
                                value = o.basisPoints;
                    cell(steps, r, 3, QString::number(double(value) / 100, 'f', 2));
                }
            }
        const auto progress = activityProgress(p, id);
        metrics->setText("물리 " + percent(progress.physical) + " · 기간 " + percent(progress.duration) + " · 공수 " +
                         percent(progress.work) + "\n" + qs(progress.physicalSource) +
                         " · 보고/분모가 없으면 미정. 공수는 보고된 실제공수와 보고 잔여공수의 비율입니다.");
    };
    auto readDefinition = [state, steps, task, physical] {
        ActivityDefinition a;
        a.id = state->definitionId;
        a.name = state->definitionName;
        a.taskId = task->currentData().toString().toStdString();
        a.physicalFromSteps = physical->isChecked();
        for (int r = 0; r < steps->rowCount(); ++r) {
            if (!steps->item(r, 0) || !steps->item(r, 1) || !steps->item(r, 2))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Steps 입력을 확인하세요."});
            bool ok = false;
            const auto weight = steps->item(r, 2)->text().toInt(&ok);
            if (!ok)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"가중치는 정수입니다."});
            a.steps.push_back(
                {steps->item(r, 0)->text().toStdString(), steps->item(r, 1)->text().trimmed().toStdString(), weight});
        }
        return a;
    };
    auto showFailure = [error](const std::exception &e) { error->setText(qErrorText(e)); };
    auto refreshIntervals = [&, intervals, error] {
        try {
            auto snapshot = document.snapshot();
            if (!snapshot)
                return;
            auto result =
                prepareExchange<std::vector<ActivityInterval>>(this, document, "지원 활동 기간 계산 중…", [snapshot] {
                    return activityIntervals(*snapshot, calculate(*snapshot));
                });
            if (!result)
                return;
            intervals->setRowCount(int(result->size()));
            for (int r = 0; r < int(result->size()); ++r) {
                const auto &a = (*result)[std::size_t(r)];
                const QStringList values{qs(a.name),
                                         qs(a.kind),
                                         a.start ? qs(*a.start) : "미정",
                                         a.finish ? qs(*a.finish) : "미정",
                                         a.durationMinutes ? QString::number(*a.durationMinutes) : "미정",
                                         QString::number(a.sourceTasks),
                                         qs(a.reason.empty() ? a.id : a.reason)};
                for (int c = 0; c < values.size(); ++c)
                    cell(intervals, r, c, values[c], false);
                intervals->item(r, 0)->setData(Qt::UserRole, qs(a.id));
            }
        } catch (const std::exception &e) {
            error->setText(qErrorText(e));
        }
    };
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(task, &QComboBox::currentIndexChanged, this, [loadSteps](int) { loadSteps(); });
    connect(refresh, &QPushButton::clicked, this, [loadSteps, refreshIntervals, error] {
        error->clear();
        loadSteps();
        refreshIntervals();
    });
    connect(addStep, &QPushButton::clicked, this, [steps] {
        const int r = steps->rowCount();
        steps->insertRow(r);
        cell(steps, r, 0, QUuid::createUuid().toString(QUuid::WithoutBraces), false);
        cell(steps, r, 1, "새 Step");
        cell(steps, r, 2, "1");
        cell(steps, r, 3, "0.00");
    });
    connect(removeStep, &QPushButton::clicked, this, [steps] {
        if (steps->currentRow() >= 0)
            steps->removeRow(steps->currentRow());
    });
    connect(saveSteps, &QPushButton::clicked, this, [&, current, readDefinition, loadSteps, showFailure, error] {
        try {
            current();
            document.putActivity(readDefinition());
            loadSteps();
            error->setText("Steps 정의 저장 완료 · 기존 보고의 가중치는 유지합니다.");
        } catch (const std::exception &e) {
            showFailure(e);
        }
    });
    connect(clearSteps, &QPushButton::clicked, this, [&, current, state, loadSteps, showFailure] {
        try {
            current();
            if (!state->definitionId.empty())
                document.removeActivity(state->definitionId);
            loadSteps();
        } catch (const std::exception &e) {
            showFailure(e);
        }
    });
    connect(record, &QPushButton::clicked, this,
            [&, current, readDefinition, loadSteps, showFailure, steps, by, reason, error] {
                try {
                    current();
                    const auto definition = readDefinition();
                    const auto &definitions = document.project()->activities;
                    const auto found = std::find_if(definitions.begin(), definitions.end(),
                                                    [&](const auto &a) { return a.id == definition.id; });
                    if (found == definitions.end() || *found != definition)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"편집한 Steps 정의를 먼저 저장하세요."});
                    StepReport report;
                    report.taskId = definition.taskId;
                    report.reporter = by->text().trimmed().toStdString();
                    report.reason = reason->text().trimmed().toStdString();
                    report.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
                    for (int r = 0; r < steps->rowCount(); ++r) {
                        bool ok = false;
                        const auto value = steps->item(r, 3) ? steps->item(r, 3)->text().toDouble(&ok) : -1;
                        if (!ok || !std::isfinite(value) || value < 0 || value > 100 ||
                            std::abs(value * 100 - std::round(value * 100)) > 0.00001)
                            throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                           {"Step 진척은 소수 둘째 자리까지 0~100%입니다."});
                        report.steps.push_back({definition.steps[std::size_t(r)], int(std::round(value * 100))});
                    }
                    document.recordSteps(std::move(report));
                    loadSteps();
                    error->setText("Steps 진척 보고 완료 · 정정은 새 보고로 기록합니다.");
                } catch (const std::exception &e) {
                    showFailure(e);
                }
            });
    connect(saveSupport, &QPushButton::clicked, this,
            [&, current, loadSteps, refreshIntervals, showFailure, state, kind, name, start, finish, wbs, calendar,
             error, saveSupport] {
                try {
                    current();
                    ActivityDefinition a;
                    a.id = state->supportId;
                    a.kind = kind->currentData().toString().toStdString();
                    a.name = name->text().trimmed().toStdString();
                    a.calendarId = calendar->currentData().toString().toStdString();
                    if (a.kind == "loe") {
                        a.startTaskId = start->currentData().toString().toStdString();
                        a.finishTaskId = finish->currentData().toString().toStdString();
                    } else
                        a.wbsId = wbs->currentData().toString().toStdString();
                    document.putActivity(std::move(a));
                    state->supportId.clear();
                    saveSupport->setText("지원 활동 추가");
                    loadSteps();
                    refreshIntervals();
                    error->setText("지원 활동 저장 완료");
                } catch (const std::exception &e) {
                    showFailure(e);
                }
            });
    connect(editSupport, &QPushButton::clicked, this,
            [&, current, state, showFailure, intervals, name, kind, start, finish, wbs, calendar, saveSupport] {
                try {
                    current();
                    const int selected = intervals->currentRow();
                    if (selected < 0)
                        return;
                    const auto id = intervals->item(selected, 0)->data(Qt::UserRole).toString().toStdString();
                    for (const auto &a : document.project()->activities)
                        if (a.id == id) {
                            state->supportId = id;
                            name->setText(qs(a.name));
                            kind->setCurrentIndex(kind->findData(qs(a.kind)));
                            start->setCurrentIndex(start->findData(qs(a.startTaskId)));
                            finish->setCurrentIndex(finish->findData(qs(a.finishTaskId)));
                            wbs->setCurrentIndex(wbs->findData(qs(a.wbsId)));
                            calendar->setCurrentIndex(calendar->findData(qs(a.calendarId)));
                            saveSupport->setText("선택 지원 활동 저장");
                            break;
                        }
                } catch (const std::exception &e) {
                    showFailure(e);
                }
            });
    connect(newSupport, &QPushButton::clicked, this, [state, name, saveSupport] {
        state->supportId.clear();
        name->clear();
        saveSupport->setText("지원 활동 추가");
    });
    connect(deleteSupport, &QPushButton::clicked, this,
            [&, current, loadSteps, refreshIntervals, showFailure, intervals] {
                try {
                    current();
                    const int row = intervals->currentRow();
                    if (row < 0)
                        return;
                    document.removeActivity(intervals->item(row, 0)->data(Qt::UserRole).toString().toStdString());
                    loadSteps();
                    refreshIntervals();
                } catch (const std::exception &e) {
                    showFailure(e);
                }
            });
    loadSteps();
    // Preparation runs after exec/show, so progress dialogs cannot outlive construction.
    connect(tabs, &QTabWidget::currentChanged, this, [refreshIntervals](int index) {
        if (index == 1)
            refreshIntervals();
    });
    prepareEditorDialog(*this);
}
} // namespace jobs
