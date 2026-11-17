#include "ui/MainWindow.h"
#include "ui/ClassificationDialogs.h"
#include "ui/XmlCodeDialogs.h"
#include "ui/TaskTableModel.h"
#include "domain/Classification.h"
#include "persistence/ClassificationJson.h"
#include <QAction>
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
QStringList values(const std::vector<std::string> &path) {
    QStringList result;
    for (const auto &v : path)
        result << qs(v);
    return result;
}
QString pathToken(const std::vector<std::string> &path) {
    QString result;
    for (const auto &v : path)
        result += QString::number(v.size()) + ":" + qs(v);
    return result;
}
} // namespace
void MainWindow::setupClassification() {
    QMenu *project = nullptr, *view = nullptr;
    for (auto *menu : menuBar()->findChildren<QMenu *>()) {
        if (menu->title().startsWith("프로젝트"))
            project = menu;
        if (menu->title().startsWith("보기"))
            view = menu;
    }
    if (!project || !view)
        return;
    auto add = [&](QMenu *menu, const QString &label, const char *name, std::function<void()> callback) {
        auto *action = menu->addAction(label);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, callback] {
            runAfterEdit([this, callback] {
                if (!document_.project())
                    return;
                try {
                    callback();
                    refresh();
                } catch (const std::exception &e) {
                    report(e);
                }
            });
        });
    };
    project->addSeparator();
    add(project, "사용자 정의 필드…", "manageCustomFields", [this] { manageCustomFields(document_, this); });
    add(project, "프로젝트 사용자 필드 정의…", "manageProjectFields",
        [this] { manageCustomFields(document_, this, FieldScope::Project); });
    add(project, "프로젝트 사용자 필드 값…", "editProjectAttributes",
        [this] { editObjectAttributes(document_, FieldScope::Project, this); });
    add(project, "배정 사용자 필드 정의…", "manageAssignmentFields",
        [this] { manageCustomFields(document_, this, FieldScope::Assignment); });
    add(project, "배정 사용자 필드 값…", "editAssignmentAttributes",
        [this] { editObjectAttributes(document_, FieldScope::Assignment, this); });
    for (auto *menu : menuBar()->findChildren<QMenu *>())
        if (menu->title().startsWith("자원")) {
            add(menu, "자원 사용자 필드 정의…", "manageResourceFields",
                [this] { manageCustomFields(document_, this, FieldScope::Resource); });
            add(menu, "자원 사용자 필드 값…", "editResourceAttributes",
                [this] { editObjectAttributes(document_, FieldScope::Resource, this); });
            break;
        }
    add(project, "XML WBS 마스크·자동 코드…", "editXmlWbsCodes", [this] { editXmlWbsCodes(document_, this); });
    add(project, "작업 코드 사전…", "manageActivityCodes", [this] { manageActivityCodes(document_, this); });
    add(project, "선택 작업 분류 값…", "editTaskAttributes",
        [this] { editTaskAttributes(document_, selectedTaskIds(), this); });
    view->addSeparator();
    add(view, "분류·필터·정렬·열 배치…", "configureTaskView", [this] { configureTaskView(); });
    add(view, "현재 보기 저장…", "saveTaskView", [this] { saveTaskView(); });
    add(view, "저장된 보기 적용…", "applySavedTaskView", [this] { chooseSavedTaskView(false); });
    add(view, "저장된 보기 삭제…", "deleteSavedTaskView", [this] { chooseSavedTaskView(true); });
    table_->horizontalHeader()->setSectionsMovable(true);
}
bool MainWindow::classificationActive() const {
    return !classificationView_.groupBy.empty() || !classificationView_.attributeFilters.empty() ||
           !classificationView_.sortKey.empty();
}
std::string MainWindow::classificationToken() const {
    auto settings = classificationView_;
    settings.columns.clear();
    return QJsonDocument(json::encodeTaskViewSettings(settings)).toJson(QJsonDocument::Compact).toStdString();
}
TaskViewSettings MainWindow::currentTaskViewSettings() const {
    auto s = classificationView_;
    s.wbsId = selectedWbs_;
    s.query = str(taskSearch_->text());
    s.filter = taskFilter_->currentIndex();
    s.sort = taskSort_->currentIndex();
    s.columns.clear();
    auto *header = table_->horizontalHeader();
    for (int visual = 0; visual < header->count(); ++visual) {
        const int column = header->logicalIndex(visual);
        auto width = table_->columnWidth(column);
        if (width == 0)
            width = std::max(40, header->sectionSizeHint(column));
        s.columns.push_back(
            {taskModel_->columnKey(column), std::clamp(width, 40, 2000), table_->isColumnHidden(column)});
    }
    return s;
}
void applyTaskColumns(QTableView &table, const TaskTableModel &model, const std::vector<ViewColumn> &columns) {
    auto *header = table.horizontalHeader();
    for (int column = 0; column < model.columnCount(); ++column) {
        header->moveSection(header->visualIndex(column), column);
        table.setColumnHidden(column, column == 8);
    }
    int visual = 0;
    for (const auto &c : columns) {
        const int column = model.columnForKey(c.key);
        if (column < 0)
            continue;
        header->moveSection(header->visualIndex(column), visual++);
        header->setSectionResizeMode(column, QHeaderView::Interactive);
        table.setColumnWidth(column, c.width);
        table.setColumnHidden(column, c.hidden);
    }
}
void MainWindow::applyTaskViewSettings(const TaskViewSettings &s) {
    validateTaskView(*document_.project(), s);
    classificationView_ = s;
    classificationView_.columns.clear();
    selectedWbs_ = s.wbsId;
    {
        const QSignalBlocker a(taskSearch_), b(taskFilter_), c(taskSort_);
        taskSearch_->setText(qs(s.query));
        taskFilter_->setCurrentIndex(s.filter);
        taskSort_->setCurrentIndex(s.sort);
    }
    refresh();
    applyTaskColumns(*table_, *taskModel_, s.columns);
}
void MainWindow::configureTaskView() {
    const auto snapshot = document_.snapshot();
    std::vector<std::pair<std::string, QString>> columns;
    for (int column = 0; column < taskModel_->columnCount(); ++column)
        columns.push_back({taskModel_->columnKey(column), taskModel_->headerData(column, Qt::Horizontal).toString()});
    auto settings = editTaskViewSettings(*snapshot, currentTaskViewSettings(), columns, this);
    if (!settings)
        return;
    if (!document_.project() || document_.project()->documentId != snapshot->documentId ||
        document_.project()->revision != snapshot->revision)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"설정 중 문서가 변경되었습니다. 다시 설정하세요."});
    applyTaskViewSettings(*settings);
}
void MainWindow::saveTaskView() {
    if (document_.readOnly())
        return;
    const auto snapshot = document_.snapshot();
    auto settings = currentTaskViewSettings();
    bool okay = false;
    auto name = QInputDialog::getText(this, "현재 작업 보기 저장", "보기 이름", QLineEdit::Normal, {}, &okay).trimmed();
    if (!okay)
        return;
    if (document_.project()->documentId != snapshot->documentId || document_.project()->revision != snapshot->revision)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"보기 저장 중 문서가 변경되었습니다."});
    SavedTaskView view{{}, str(name), std::move(settings)};
    for (const auto &v : snapshot->savedTaskViews)
        if (v.name == view.name) {
            if (QMessageBox::question(this, "작업 보기 저장", "같은 이름의 보기를 변경하시겠습니까?") !=
                QMessageBox::Yes)
                return;
            if (document_.project()->revision != snapshot->revision ||
                document_.project()->documentId != snapshot->documentId)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"보기 저장 중 문서가 변경되었습니다."});
            view.id = v.id;
            break;
        }
    document_.putSavedTaskView(std::move(view));
}
void MainWindow::chooseSavedTaskView(bool remove) {
    const auto snapshot = document_.snapshot();
    if (snapshot->savedTaskViews.empty()) {
        QMessageBox::information(this, "저장된 작업 보기", "저장된 보기가 없습니다.");
        return;
    }
    QStringList names;
    for (const auto &v : snapshot->savedTaskViews)
        names << qs(v.name);
    bool okay = false;
    const auto name =
        QInputDialog::getItem(this, remove ? "저장된 보기 삭제" : "저장된 보기 적용", "보기", names, 0, false, &okay);
    if (!okay)
        return;
    if (document_.project()->documentId != snapshot->documentId || document_.project()->revision != snapshot->revision)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"선택 중 문서가 변경되었습니다."});
    const auto &v = snapshot->savedTaskViews.at(std::size_t(names.indexOf(name)));
    if (remove)
        document_.removeSavedTaskView(v.id);
    else
        applyTaskViewSettings(v.settings);
}
void MainWindow::refreshClassificationGroups(const TaskViewProjection &projection) {
    taskGroups_ = projection.groups;
    const QSignalBlocker blocker(wbsTree_);
    const bool wasGroup = wbsTree_->currentItem() && wbsTree_->currentItem()->data(0, Qt::UserRole + 3).toBool();
    QTreeWidgetItem *old = nullptr, *anchor = nullptr;
    for (QTreeWidgetItemIterator it(wbsTree_); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole + 4).toBool())
            old = *it;
        if (!(*it)->data(0, Qt::UserRole + 3).toBool() && str((*it)->data(0, Qt::UserRole).toString()) == selectedWbs_)
            anchor = *it;
    }
    if (old)
        delete old;
    if (!anchor)
        anchor = wbsTree_->topLevelItem(0);
    if (!anchor)
        return;
    if (classificationView_.groupBy.empty()) {
        if (wasGroup)
            wbsTree_->setCurrentItem(anchor);
        return;
    }
    auto *root = new QTreeWidgetItem(anchor, {"분류별 보기 · 전체"});
    root->setData(0, Qt::UserRole, qs(selectedWbs_));
    root->setData(0, Qt::UserRole + 3, true);
    root->setData(0, Qt::UserRole + 4, true);
    root->setData(0, Qt::UserRole + 2, QStringList{});
    root->setExpanded(true);
    root->setToolTip(0, "현재 WBS·검색·필터 범위의 고유 작업 · 그룹을 선택하면 표·간트에 같은 작업이 표시됩니다.");
    std::unordered_map<std::string, QTreeWidgetItem *> parents;
    parents.emplace("", root);
    QTreeWidgetItem *selected = classificationView_.groupPath.empty() ? root : nullptr;
    for (const auto &g : projection.groups) {
        auto parentPath = g.path;
        parentPath.pop_back();
        auto *item = new QTreeWidgetItem(parents.at(str(pathToken(parentPath))),
                                         {qs(g.label) + QString(" · %1개").arg(g.tasks)});
        parents.emplace(str(pathToken(g.path)), item);
        item->setData(0, Qt::UserRole, qs(selectedWbs_));
        item->setData(0, Qt::UserRole + 3, true);
        item->setData(0, Qt::UserRole + 2, values(g.path));
        item->setExpanded(g.path.size() < 2);
        item->setToolTip(0, QString("작업 %1개 · 기간 합계 %2h · 기간 미정 %3개\n기간 합계는 경과기간이 아닙니다.")
                                    .arg(g.tasks)
                                    .arg(double(g.durationMinutes) / 60, 0, 'g', 12)
                                    .arg(g.missingDurations) +
                                "\n" + qs(classificationView_.groupBy[g.path.size() - 1]) + ": " + qs(g.path.back()) +
                                "\n" + qs(g.rollUpText));
        if (g.path == classificationView_.groupPath)
            selected = item;
    }
    if (wasGroup || !classificationView_.groupPath.empty())
        wbsTree_->setCurrentItem(selected ? selected : root);
}
} // namespace jobs
