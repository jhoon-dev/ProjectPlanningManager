#pragma once
#include "application/Document.h"
#include <QString>
namespace ppm {
struct TaskViewSpec {
    std::string wbs;
    QString query;
    int filter = 0, sort = 0;
    TaskViewSettings classification = {};
};
struct TaskGroup {
    std::vector<std::string> path;
    std::string label;
    std::size_t tasks = 0, missingDurations = 0;
    std::int64_t durationMinutes = 0;
    std::string rollUpText;
};
struct TaskViewProjection {
    std::vector<std::size_t> rows;
    std::size_t scopeCount = 0;
    std::vector<TaskGroup> groups;
};
TaskViewProjection projectTaskView(const Project &, const ScheduleResult *, const TaskViewSpec &,
                                   bool calculateSchedule = false);
void prepareTaskView(BulkView &, const Project &before, const Project &after, const TaskViewSpec &,
                     std::vector<std::size_t> previousRows, std::string token, std::uint64_t calculationGeneration);
} // namespace ppm
