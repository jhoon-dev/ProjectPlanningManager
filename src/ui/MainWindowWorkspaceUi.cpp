#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/EditorUi.h"
#include "ui/AsyncEditController.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStyle>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace jobs {
void MainWindow::setupWorkspaceUi(QToolBar *tools, QComboBox *zoom) {
    const int controlHeight = std::max(32, fontMetrics().height() + 16);
    tools->setObjectName("primaryTools");
    tools->setIconSize({18, 18});
    tools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (auto *action : tools->actions())
        tools->removeAction(action);
    const auto add = [this, tools](QAction *action, const QString &label, QStyle::StandardPixmap icon) {
        action->setIcon(style()->standardIcon(icon));
        action->setIconText(label);
        tools->addAction(action);
    };
    add(findChild<QAction *>("newProject"), "새로 만들기", QStyle::SP_FileIcon);
    add(findChild<QAction *>("openProject"), "열기", QStyle::SP_DialogOpenButton);
    add(save_, "저장", QStyle::SP_DialogSaveButton);
    tools->addSeparator();
    add(undo_, "실행 취소", QStyle::SP_ArrowBack);
    add(redo_, "다시 실행", QStyle::SP_ArrowForward);
    for (auto *action : {undo_, redo_})
        if (auto *button = qobject_cast<QToolButton *>(tools->widgetForAction(action)))
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->addSeparator();
    add(add_, "작업 추가", QStyle::SP_FileDialogNewFolder);
    auto *taskMenu = new QMenu(this);
    for (auto *action : {insertTasks_, duplicateTasks_, deleteTasks_, copyTasks_, pasteTasks_})
        taskMenu->addAction(action);
    taskMenu->addSeparator();
    for (auto *action :
         {constraints_, effort_, taskCalendar_, assignWbs_, assignResources_, taskCost_, progressEdit_, costReport_})
        taskMenu->addAction(action);
    auto *taskButton = new QToolButton(tools);
    taskButton->setObjectName("selectedTaskMenu");
    taskButton->setText("작업 편집");
    taskButton->setAccessibleName("선택 작업 편집 메뉴");
    taskButton->setPopupMode(QToolButton::InstantPopup);
    taskButton->setMenu(taskMenu);
    tools->addWidget(taskButton);
    tools->addSeparator();
    add(weeklyUpdate_, "주간 갱신", QStyle::SP_FileDialogDetailedView);
    add(calculate_, "일정 계산 · F9", QStyle::SP_MediaPlay);
    for (auto *action : {cancelCalculation_, cancelEdit_}) {
        auto *button = new QToolButton(tools);
        button->setDefaultAction(action);
        button->setObjectName(action->objectName() + "Button");
        auto *presentation = tools->addWidget(button);
        presentation->setVisible(action->isEnabled());
        connect(action, &QAction::changed, presentation,
                [action, presentation] { presentation->setVisible(action->isEnabled()); });
    }
    auto *analysis = new QToolButton(tools);
    analysis->setObjectName("analysisMenu");
    analysis->setText("분석·보고");
    analysis->setPopupMode(QToolButton::InstantPopup);
    auto *analysisMenu = new QMenu(analysis);
    for (auto *action : {reports_, comparison_, resourceComparison_, evm_, periodEvm_, externalEvm_, financialPeriods_,
                         scheduleIssues_})
        analysisMenu->addAction(action);
    analysis->setMenu(analysisMenu);
    tools->addWidget(analysis);
    auto *space = new QWidget(tools);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(space);
    auto *help = findChild<QAction *>("taskEditingHelp");
    help->setIconText("도움말 · F1");
    tools->addAction(help);

    auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    layout->removeWidget(wbsSummary_);
    wbsSummary_->setWordWrap(false);
    wbsSummary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *view = new QHBoxLayout;
    view->setObjectName("workspaceViewRow");
    auto *wbs = new QToolButton(centralWidget());
    wbs->setObjectName("toggleWbs");
    wbs->setText("WBS 탐색");
    wbs->setCheckable(true);
    wbs->setChecked(true);
    wbs->setToolTip("WBS 탐색 표시/숨기기 · 선택한 범위는 유지됩니다.");
    connect(wbs, &QToolButton::toggled, wbsTree_, &QWidget::setVisible);
    view->addWidget(wbs);
    view->addWidget(wbsSummary_, 1);
    view->addWidget(new QLabel("간트 배율", centralWidget()));
    view->addWidget(zoom);
    auto *navigation = menuBar()->addMenu("보기(&V)");
    const auto navigationButton = [&](const char *id, const QString &label, const QKeySequence &key, auto callback) {
        auto *action = navigation->addAction(label);
        action->setObjectName(id);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, callback);
        auto *button = new QToolButton(centralWidget());
        button->setDefaultAction(action);
        alignEditorControl(*button, controlHeight);
        view->addWidget(button);
    };
    navigationButton("focusGanttSelection", "선택 보기", QKeySequence("Ctrl+Shift+J"),
                     [this] { gantt_->focusSelectedTask(); });
    navigationButton("fitGanttSchedule", "전체 맞춤", QKeySequence("Ctrl+Shift+F"), [this, zoom] {
        const QSignalBlocker block(zoom);
        if (zoom->findData("fit") < 0)
            zoom->addItem("맞춤", "fit");
        zoom->setCurrentIndex(zoom->findData("fit"));
        gantt_->fitSchedule();
    });
    baselineChoice_->setMinimumWidth(150);
    view->addWidget(baselineChoice_);
    layout->insertLayout(3, view);

    workflow_ = new QWidget(centralWidget());
    workflow_->setObjectName("workflowStatus");
    auto *workflowRow = new QHBoxLayout(workflow_);
    workflowRow->setContentsMargins(8, 6, 8, 6);
    auto *states = new QVBoxLayout;
    states->setSpacing(2);
    auto *stateRow = new QHBoxLayout;
    fileState_ = new QLabel(workflow_);
    fileState_->setObjectName("fileSaveState");
    scheduleState_ = new QLabel(workflow_);
    scheduleState_->setObjectName("scheduleFreshnessState");
    for (auto *label : {fileState_, scheduleState_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        stateRow->addWidget(label, 1);
    }
    states->addLayout(stateRow);
    workflowHint_ = new QLabel(workflow_);
    workflowHint_->setObjectName("workflowNextStep");
    workflowHint_->setTextFormat(Qt::PlainText);
    workflowHint_->setWordWrap(true);
    states->addWidget(workflowHint_);
    workflowRow->addLayout(states, 1);
    workflowSave_ = new QToolButton(workflow_);
    workflowSave_->setObjectName("workflowSave");
    workflowSave_->setText("파일 저장");
    connect(workflowSave_, &QToolButton::clicked, save_, &QAction::trigger);
    workflowCalculate_ = new QToolButton(workflow_);
    workflowCalculate_->setObjectName("workflowCalculate");
    workflowCalculate_->setText("일정 계산 · F9");
    connect(workflowCalculate_, &QToolButton::clicked, calculate_, &QAction::trigger);
    for (auto *button : {workflowSave_, workflowCalculate_}) {
        alignEditorControl(*button, controlHeight);
        workflowRow->addWidget(button);
    }
    auto *compare = new QToolButton(workflow_);
    compare->setObjectName("workflowCompare");
    comparison_->setIconText("결과 비교");
    compare->setDefaultAction(comparison_);
    compare->setText("결과 비교");
    compare->setToolTip("기준선·예측·현재 채택 완료일과 편차를 비교하고 작업·자원 근거를 확인합니다.");
    alignEditorControl(*compare, controlHeight);
    workflowRow->addWidget(compare);
    layout->insertWidget(layout->indexOf(documentNotice_) + 1, workflow_);

    auto *diagnosticBar = new QHBoxLayout;
    diagnosticBar->setObjectName("workspaceDiagnosticRow");
    auto *status = new QLabel(centralWidget());
    status->setObjectName("calculationStatus");
    status->setTextFormat(Qt::PlainText);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status->setText("일정 계산 · 프로젝트를 열고 F9로 계산하세요");
    diagnosticsToggle_ = new QToolButton(centralWidget());
    diagnosticsToggle_->setObjectName("toggleDiagnostics");
    diagnosticsToggle_->setCheckable(true);
    diagnosticsToggle_->setText("진단 보기");
    diagnosticsToggle_->setArrowType(Qt::RightArrow);
    diagnosticsToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(diagnosticsToggle_, &QToolButton::toggled, this, [this](bool open) {
        diagnostics_->setVisible(open);
        diagnosticsToggle_->setText(open ? "진단 접기" : "진단 보기");
        diagnosticsToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(
        diagnostics_, &QPlainTextEdit::textChanged, status,
        [this, status, revealed = std::pair{~std::uint64_t{0}, ~std::uint64_t{0}}]() mutable {
            auto text = diagnostics_->document()->firstBlock().text();
            const auto &result = document_.calculation();
            if (result && result->status != CalculationStatus::Stale && !result->diagnostics.empty())
                text += QString(" · 진단 %1건").arg(result->diagnostics.size());
            status->setText(result && result->status != CalculationStatus::Stale && !result->diagnostics.empty()
                                ? QString("진단 %1건").arg(result->diagnostics.size())
                                : "진단 없음");
            status->setToolTip(text);
            const auto key = std::pair{document_.sessionGeneration(), document_.calculationGeneration()};
            if (result && revealed != key &&
                (result->status == CalculationStatus::Conflict || result->status == CalculationStatus::Incomplete)) {
                revealed = key;
                diagnosticsToggle_->setChecked(true);
            }
        });
    diagnosticBar->addWidget(status, 1);
    diagnosticBar->addWidget(diagnosticsToggle_);
    layout->addLayout(diagnosticBar);
    layout->addWidget(diagnostics_);
    diagnostics_->hide();
    summary_->setWordWrap(false);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    projectTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    wbsTree_->setAlternatingRowColors(true);
    wbsTree_->setUniformRowHeights(true);
    wbsTree_->header()->setFixedHeight(GanttView::HeaderHeight);
    wbsTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    wbsTree_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    for (const auto *name : {"taskSearch", "taskFilter", "taskSort", "resetTaskView", "toggleWbs", "ganttZoom",
                             "baselineChoice", "toggleDiagnostics"})
        if (auto *control = findChild<QWidget *>(name))
            alignEditorControl(*control, controlHeight);
    for (auto *button : tools->findChildren<QToolButton *>())
        if (button->objectName() != "qt_toolbar_ext_button")
            alignEditorControl(*button, controlHeight);
    workspaceSplit_->setHandleWidth(7);
    workspaceSplit_->setStretchFactor(0, 0);
    workspaceSplit_->setStretchFactor(1, 1);
    workspaceSplit_->setStretchFactor(2, 1);
    setStyleSheet(
        // Native status-item frames leave bright vertical strokes around empty
        // progress labels, especially with the Windows dark palette.
        "QStatusBar::item { border: 0; }"
        "QToolBar { spacing: 4px; padding: 6px; border: 0; border-bottom: 1px solid palette(midlight); }"
        "QToolBar[compactWorkspace=\"true\"] { padding: 2px 6px; }"
        "QToolBar QToolButton { padding: 6px 8px; }"
        "QLabel#projectTitle { font-size: 21px; font-weight: 600; padding: 2px 0; }"
        "QLabel#documentNotice { padding: 6px; border: 1px solid palette(mid); }"
        "QWidget#workflowStatus { border: 1px solid palette(mid); border-radius: 4px; }"
        "QWidget#workflowStatus QLabel, QWidget#workflowStatus QToolButton { border: 0; }"
        "QLabel#fileSaveState, QLabel#scheduleFreshnessState { font-weight: 600; }"
        "QWidget#workflowStatus QToolButton { padding: 6px 8px; background: palette(button); }"
        "QLineEdit#taskSearch { padding: 6px 8px; border: 1px solid palette(mid); border-radius: 4px; }"
        "QWidget#planningWorkspace QComboBox { padding: 4px 6px; }"
        "QTreeWidget#wbsTree::item { height: 28px; }"
        "QHeaderView::section { padding: 4px; border: 0; border-bottom: 1px solid palette(midlight); }"
        "QTableView#taskTable, QTreeWidget#wbsTree, QGraphicsView#ganttView { border: 1px solid palette(midlight); }"
        "QToolButton#toggleWbs, QToolButton#toggleDiagnostics { padding: 5px 8px; }"
        "QLineEdit#taskSearch:focus { border: 1px solid palette(highlight); }");
}
void MainWindow::refreshWorkspaceUi() {
    refreshWorkflowUi();
    refreshNavigationUi();
    const auto &project = document_.project();
    if (auto *fit = findChild<QAction *>("fitGanttSchedule"))
        fit->setEnabled(gantt_->hasSchedule());
    if (auto *focus = findChild<QAction *>("focusGanttSelection"))
        focus->setEnabled(gantt_->hasSchedule() && !selectedTask_.empty());
    if (!projectTitle_)
        return;
    if (!project) {
        projectTitle_->setText("프로젝트를 시작하세요");
        projectTitle_->setToolTip(QString("새로 만들기 %1 · 프로젝트 열기 %2")
                                      .arg(QKeySequence(QKeySequence::New).toString(QKeySequence::NativeText),
                                           QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)));
        summary_->setToolTip({});
        documentNotice_->hide();
    }
}
void MainWindow::refreshWorkflowUi() {
    if (!workflow_)
        return;
    const auto &p = document_.project();
    workflow_->setVisible(p.has_value() && property("workspaceLayoutMode").toInt() != 1);
    if (!p)
        return;
    const bool pending = editController_ && editController_->busy();
    const bool saveNeeded = document_.dirty() || document_.path().empty() || document_.requiresSaveAs();
    const bool copy = document_.readOnly() || document_.requiresSaveAs();
    fileState_->setText(document_.readOnly()         ? "파일 · 읽기 전용"
                        : document_.requiresSaveAs() ? "파일 · 새 파일에 저장 필요"
                        : document_.path().empty()   ? "파일 · 처음 저장 필요"
                        : document_.dirty()          ? "파일 · 미저장 변경 있음"
                                                     : "파일 · 저장됨");
    fileState_->setToolTip(QString::fromStdString(document_.path()) +
                           "\n파일 상태는 입력의 저장 여부입니다. 자동복구 저장과 일정 계산은 별개입니다.");
    workflowSave_->setText(copy ? "사본 저장…" : "파일 저장");
    workflowSave_->setEnabled(!pending);
    workflowSave_->setToolTip("입력과 보고 이력을 파일에 저장합니다. 일정 계산은 별도로 실행합니다.");
    workflowCalculate_->setText(p->statusAt ? "재예측 · F9" : "일정 계산 · F9");
    workflowCalculate_->setEnabled(!calculating_ && !pending);
    workflowCalculate_->setToolTip("현재 입력으로 일정을 계산합니다. 파일은 저장하지 않습니다.");
    const auto &result = document_.calculation();
    QString hint, next;
    if (pending) {
        scheduleState_->setText("일정 · 편집 적용 대기 중");
        hint = "편집이 끝나면 파일 저장과 일정 계산 상태를 다시 확인하세요.";
        next = "편집 적용을 기다리세요.";
    } else if (calculating_) {
        scheduleState_->setText("일정 · 계산 중");
        hint = "현재 입력으로 계산 중입니다. 파일 저장 여부는 왼쪽에서 따로 확인하세요.";
        next = "일정 계산을 기다리세요.";
    } else if (!result || result->status == CalculationStatus::Stale ||
               result->status == CalculationStatus::Cancelled) {
        scheduleState_->setText(!result ? "일정 · 계산 필요"
                                : result->status == CalculationStatus::Stale ? "일정 · 재계산 필요 (이전 결과 오래됨)"
                                                                             : "일정 · 계산 취소됨 · 다시 계산 필요");
        hint = saveNeeded
                   ? "다음: 입력을 파일에 저장하고, 오른쪽 버튼으로 현재 입력의 일정을 계산하세요."
                   : "다음: 오른쪽 버튼으로 현재 입력의 일정을 계산하세요. 저장·재열기만으로 계산되지는 않습니다.";
        next = saveNeeded ? "다음: 파일 저장 · 일정 계산" : "다음: 일정 계산";
    } else if (result->status == CalculationStatus::Valid) {
        scheduleState_->setText(result->isForecast ? "일정 · 현재 입력의 재예측 완료" : "일정 · 현재 입력의 계산 완료");
        hint = saveNeeded ? "다음: 계산은 완료됐습니다. 입력·보고 이력을 보존하려면 파일을 저장하세요."
                          : "입력 저장과 일정 계산이 완료됐습니다. 기준선 비교·자원 검토·보고로 이어갈 수 있습니다.";
        next = saveNeeded ? "다음: 파일을 저장하세요." : "다음: 결과 비교 · 자원 검토 · 회의 자료";
    } else {
        scheduleState_->setText(result->status == CalculationStatus::Conflict ? "일정 · 계산 충돌 · 완료일 없음"
                                                                              : "일정 · 입력 불완전 · 완료일 없음");
        hint = "다음: 진단 보기에서 원인을 확인하고 입력을 수정한 뒤 다시 계산하세요. 파일 저장은 별도로 수행합니다.";
        next = "다음: 진단 보기에서 원인을 확인하세요.";
    }
    if (document_.readOnly()) {
        hint += " 읽기 전용 입력을 보존하려면 사본으로 저장하세요.";
        next += " 사본 저장 가능";
    }
    workflowHint_->setText(next);
    workflowHint_->setToolTip(hint);
    scheduleState_->setToolTip(hint);
}
} // namespace jobs
