#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/TaskTableModel.h"
#include "ui/EditorUi.h"
#include "ui/ClassificationDialogs.h"
#include <QAction>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace ppm {
void MainWindow::setupNavigationUi() {
    QMenu *resourceMenu = nullptr, *viewMenu = nullptr;
    for (auto *action : menuBar()->actions()) {
        if (action->menu() && action->text().startsWith("자원"))
            resourceMenu = action->menu();
        if (action->menu() && action->text().startsWith("보기"))
            viewMenu = action->menu();
    }
    if (resourceMenu) {
        resourceMenu->setObjectName("resourceMenu");
        for (auto *action : resourceMenu->actions())
            resourceMenu->removeAction(action);
        const auto add = [this](QMenu *menu, std::initializer_list<const char *> names) {
            for (const auto *name : names)
                if (auto *action = findChild<QAction *>(name))
                    menu->addAction(action);
        };
        resourceMenu->addSection("주간 자원 검토");
        add(resourceMenu,
            {"manageResources", "assignResources", "remainingAvailability", "resourceComparison", "levelingDecisions"});
        auto *periods = resourceMenu->addMenu("기간별 공수·실적");
        periods->setObjectName("resourcePeriodsMenu");
        add(periods, {"datedRemainingWork", "projectUsage", "periodActualInput"});
        auto *materials = resourceMenu->addMenu("자재·역할");
        materials->setObjectName("resourceMaterialsMenu");
        add(materials, {"resourcePlan", "materialActuals", "materialInventory"});
        auto *advanced = resourceMenu->addMenu("고급 설정·곡선");
        advanced->setObjectName("resourceAdvancedMenu");
        add(advanced, {"resourcePlanning", "resourceViews", "assignmentPeriods", "activityExtensions",
                       "manageResourceFields", "editResourceAttributes"});
    }
    auto *row = findChild<QHBoxLayout *>("workspaceViewRow");
    auto *preset = new QComboBox(centralWidget());
    preset->setObjectName("taskColumnPreset");
    preset->setAccessibleName("작업 표 빠른 열 보기");
    preset->addItems({"열: 전체·직접 배치", "열: 일정 핵심", "열: 주간 편차·보고", "열: 자원 확인"});
    preset->setToolTip("보기만 변경합니다. 주간 열은 조회 전용이며 선택 기준선·상태일을 따릅니다. 전체·직접 배치로 "
                       "돌아오면 이전 열을 복원합니다.");
    alignEditorControl(*preset, std::max(32, fontMetrics().height() + 16));
    row->insertWidget(2, preset);
    connect(preset, &QComboBox::currentIndexChanged, this, [this] { runAfterEdit([this] { applyColumnPreset(); }); });
    connect(taskModel_, &QAbstractItemModel::modelReset, this, [this, preset] {
        if (preset->currentIndex())
            QTimer::singleShot(0, this, [this] { applyColumnPreset(); });
    });
    if (viewMenu) {
        viewMenu->addSeparator();
        for (int i = 1; i <= 3; ++i) {
            auto *action = viewMenu->addAction(preset->itemText(i));
            action->setObjectName(QString("taskColumnsPreset%1").arg(i));
            connect(action, &QAction::triggered, preset, [preset, i] { preset->setCurrentIndex(i); });
        }
    }
    auto *context = new QWidget(centralWidget());
    context->setObjectName("navigationContext");
    auto *contextLayout = new QVBoxLayout(context);
    contextLayout->setContentsMargins(4, 2, 4, 2);
    auto *contextRow = new QHBoxLayout;
    contextLayout->addLayout(contextRow);
    auto *label = new QLabel(context);
    label->setObjectName("navigationScope");
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contextRow->addWidget(label, 1);
    auto *toggle = new QToolButton(context);
    toggle->setObjectName("navigationDetailsToggle");
    toggle->setText("범위 상세");
    toggle->setCheckable(true);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    alignEditorControl(*toggle, preset->height());
    contextRow->addWidget(toggle);
    auto *details = new QLabel(context);
    details->setObjectName("navigationDetails");
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->hide();
    contextLayout->addWidget(details);
    connect(toggle, &QToolButton::toggled, details, [details, toggle](bool open) {
        details->setVisible(open);
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    auto *finish = new QAction("표시 완료일 보기", this);
    finish->setObjectName("focusGanttFinish");
    finish->setToolTip(
        "현재 표/간트에 표시된 작업 중 가장 늦은 완료일로 시간축을 이동합니다. 작업 선택과 필터는 유지합니다.");
    connect(finish, &QAction::triggered, this, [this] { gantt_->focusFinish(); });
    if (viewMenu)
        viewMenu->addAction(finish);
    auto *button = new QToolButton(context);
    button->setObjectName("focusGanttFinishButton");
    button->setDefaultAction(finish);
    alignEditorControl(*button, preset->height());
    contextRow->addWidget(button);
    auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    layout->insertWidget(layout->indexOf(workflow_) + 1, context);
    gantt_->navigationChanged = [this] { refreshNavigationUi(); };
}
void MainWindow::applyColumnPreset() {
    auto *preset = findChild<QComboBox *>("taskColumnPreset");
    if (!preset)
        return;
    const auto choice = preset->currentIndex();
    auto *header = table_->horizontalHeader();
    if (choice && !preset->property("previousPreset").toInt())
        preset->setProperty("previousColumns", header->saveState());
    taskModel_->setWeeklyColumns(choice == 2, selectedBaseline_);
    if (!choice) {
        if (!header->restoreState(preset->property("previousColumns").toByteArray()))
            applyTaskColumns(*table_, *taskModel_, {});
    } else {
        std::vector<std::pair<std::string, int>> columns;
        if (choice == 1)
            columns = {{"name", 155}, {"duration", 65}, {"predecessors", 125}, {"start", 130}, {"finish", 130}};
        else if (choice == 2)
            columns = {{"name", 140},
                       {"finish", 130},
                       {"$baselineFinish", 130},
                       {"$finishVariance", 120},
                       {"$reportState", 120}};
        else
            columns = {{"name", 155}, {"duration", 65}, {"finish", 130}, {"resources", 165}, {"critical", 125}};
        for (int c = 0; c < taskModel_->columnCount(); ++c)
            table_->setColumnHidden(c, true);
        int visual = 0;
        for (const auto &[key, width] : columns) {
            const auto c = taskModel_->columnForKey(key);
            if (c < 0)
                continue;
            header->moveSection(header->visualIndex(c), visual++);
            header->setSectionResizeMode(c, QHeaderView::Interactive);
            table_->setColumnWidth(c, width);
            table_->setColumnHidden(c, false);
        }
    }
    preset->setProperty("previousPreset", choice);
    table_->horizontalScrollBar()->setValue(0);
    refreshNavigationUi();
}
void MainWindow::refreshNavigationUi() {
    auto *context = findChild<QWidget *>("navigationContext");
    auto *preset = findChild<QComboBox *>("taskColumnPreset");
    auto *label = findChild<QLabel *>("navigationScope");
    auto *details = findChild<QLabel *>("navigationDetails");
    auto *finish = findChild<QAction *>("focusGanttFinish");
    if (!context || !preset || !label || !details || !finish)
        return;
    const auto &p = document_.project();
    context->setVisible(p.has_value() && property("workspaceLayoutMode").toInt() != 1);
    preset->setEnabled(p.has_value());
    taskModel_->setWeeklyColumns(preset->currentIndex() == 2, selectedBaseline_);
    const auto &calculation = document_.calculation();
    const bool current = p && calculation && calculation->status == CalculationStatus::Valid &&
                         calculation->inputRevision == p->revision;
    finish->setEnabled(current && gantt_->hasSchedule());
    if (!p)
        return;
    QString selected = "선택 없음";
    const int row = table_->currentRow();
    if (!selectedTask_.empty() && row >= 0 && std::size_t(row) < visibleRows_.size() &&
        visibleRows_[std::size_t(row)] < p->tasks.size()) {
        const auto &task = p->tasks[visibleRows_[std::size_t(row)]];
        selected = "선택: " + QString::fromStdString(task.name) + " (ID " + QString::fromStdString(task.id) + ")";
    }
    const auto brief = QString("표시 %1/%2개 · 선택 %3개 · 계산 전체 %2개")
                           .arg(visibleRows_.size())
                           .arg(p->tasks.size())
                           .arg(selectedTaskIds_.size());
    auto text =
        QString("표시 %1/%2개 · 선택 %3개 · ")
            .arg(visibleRows_.size())
            .arg(p->tasks.size())
            .arg(selectedTaskIds_.size()) +
        selected +
        QString("\n계산·자원 비교는 전체 %1개 · 필터/WBS는 표·간트 보기만 제한 · 상태일 ").arg(p->tasks.size()) +
        (p->statusAt ? QString::fromStdString(*p->statusAt) : "미설정") +
        (current && calculation->projectFinish ? " · 전체 완료 " + QString::fromStdString(*calculation->projectFinish)
                                               : " · 전체 완료 미정");
    if (current && gantt_->hasSchedule())
        text += "\n간트 표시 완료 " + gantt_->displayedFinish() +
                (gantt_->finishVisible() ? " · 화면 안" : " · 화면 밖 → 표시 완료일 보기 또는 전체 맞춤");
    else
        text += "\n유효한 간트 날짜 없음 · F9로 전체 일정을 계산하세요.";
    label->setText(brief);
    label->setToolTip(text);
    details->setText(text);
    taskViewStatus_->setToolTip(text);
}
} // namespace ppm
