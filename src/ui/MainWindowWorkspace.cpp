#include "ui/MainWindow.h"
#include "ui/NetworkDialog.h"
#include "ui/AsyncEditController.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <algorithm>

namespace ppm {
bool MainWindow::selectTaskById(const std::string &taskId) {
    if (!document_.project() || std::none_of(document_.project()->tasks.begin(), document_.project()->tasks.end(),
                                             [&](const auto &task) { return task.id == taskId; }))
        return false;
    if (editController_ && editController_->busy()) {
        runAfterEdit([this, taskId] { selectTaskById(taskId); });
        return true;
    }
    commitPendingCell();
    if (!document_.project() || std::none_of(document_.project()->tasks.begin(), document_.project()->tasks.end(),
                                             [&](const auto &task) { return task.id == taskId; }))
        return false;
    selectedWbs_.clear();
    classificationView_.groupPath.clear();
    classificationView_.attributeFilters.clear();
    selectedTask_ = taskId;
    selectedTaskIds_ = {taskId};
    {
        const QSignalBlocker search(taskSearch_), filter(taskFilter_), sort(taskSort_);
        taskSearch_->clear();
        taskFilter_->setCurrentIndex(0);
        taskSort_->setCurrentIndex(0);
    }
    taskViewReset_ = false;
    rendered_ = false;
    refresh();
    for (std::size_t row = 0; row < visibleRows_.size(); ++row)
        if (document_.project()->tasks[visibleRows_[row]].id == taskId) {
            table_->setCurrentCell(int(row), 0);
            table_->selectRow(int(row));
            table_->scrollTo(table_->model()->index(int(row), 0));
            syncNetworkSelection(this, taskId);
            return true;
        }
    return false;
}
} // namespace ppm
