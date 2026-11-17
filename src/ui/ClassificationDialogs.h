#pragma once
#include "application/Document.h"
#include <QString>
class QWidget;
class QTableView;
namespace jobs {
class TaskTableModel;
void applyTaskColumns(QTableView &, const TaskTableModel &, const std::vector<ViewColumn> &);
void manageCustomFields(Document &, QWidget *parent, FieldScope = FieldScope::Task);
void editObjectAttributes(Document &, FieldScope, QWidget *parent);
void manageActivityCodes(Document &, QWidget *parent);
void editTaskAttributes(Document &, const std::vector<std::string> &ids, QWidget *parent);
std::optional<TaskViewSettings> editTaskViewSettings(const Project &, const TaskViewSettings &,
                                                     const std::vector<std::pair<std::string, QString>> &columns,
                                                     QWidget *parent);
} // namespace jobs
