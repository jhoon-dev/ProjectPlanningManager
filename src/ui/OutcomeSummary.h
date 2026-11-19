#pragma once
#include "calculation/Resources.h"
#include <QString>
class QTableWidget;
class QWidget;
namespace ppm {
QTableWidget *outcomeTable(QWidget *, const char *name);
const Baseline *outcomeBaseline(const Project &, const std::string &id);
QString outcomeDelta(const PlanInput *, const std::optional<std::string> &from, const std::optional<std::string> &to);
QString outcomeLoad(const ResourceLoad &);
QString outcomeAdoption(const Project &);
QString outcomeCoverage(const Project &);
void outcomeRow(QTableWidget *, const QString &name, const std::optional<std::string> &finish,
                const PlanInput *calendar, const std::optional<std::string> &baseline,
                const std::optional<std::string> &original, const QString &status, const QString &load);
std::optional<std::string> outcomeFinish(const ScheduleResult &);
} // namespace ppm
