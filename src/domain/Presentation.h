#pragma once
#include <string>
namespace jobs {
struct PrintPreferences {
    int paper = 0, orientation = 0, margin = 10, fontPoints = 9, tablePercent = 52, nameWidth = 140, daysPerPage = 14;
    bool selectedOnly = false, fitTime = true, baseline = true, automaticDates = true, relations = false, wbs = false;
    bool networkFit = true;
    int networkScale = 100;
    std::string from, to;
    bool operator==(const PrintPreferences &) const = default;
};
struct NetworkPosition {
    std::string taskId;
    int x = 0, y = 0;
    bool operator==(const NetworkPosition &) const = default;
};
struct Project;
void validatePresentation(const Project &);
} // namespace jobs
