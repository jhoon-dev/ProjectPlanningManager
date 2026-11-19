#pragma once
#include "domain/Project.h"
#include <QStringList>
namespace ppm {
int taskDayMinutes(const PlanInput &, const Task &);
QString taskDurationText(std::int64_t minutes, int dayMinutes = 480);
QString taskDurationHelp(const PlanInput &, const Task &);
std::optional<std::int64_t> taskDurationInput(const QString &, int dayMinutes = 480);
std::vector<Dependency> taskDependencyInput(const QString &, const Project &, const std::string &taskId);
QString encodeTaskTsv(const QList<QStringList> &);
QList<QStringList> decodeTaskTsv(const QString &);
} // namespace ppm
