#include "domain/Project.h"
#include "domain/Diagnostics.h"
#include <unordered_set>
#include <chrono>
namespace ppm {
void validatePresentation(const Project &p) {
    auto fail = [] { throw AppError(Diagnostic{"PRESENTATION_INVALID", {}, {}}); };
    if (p.printPreferences) {
        const auto &s = *p.printPreferences;
        if (s.paper < 0 || s.paper > 1 || s.orientation < 0 || s.orientation > 1 || s.margin < 5 || s.margin > 30 ||
            s.fontPoints < 8 || s.fontPoints > 12 || s.tablePercent < 40 || s.tablePercent > 70 || s.nameWidth < 80 ||
            s.nameWidth > 240 || s.daysPerPage < 1 || s.daysPerPage > 366 || s.networkScale < 25 ||
            s.networkScale > 200 ||
            (s.automaticDates ? (!s.from.empty() || !s.to.empty())
                              : (!validDate(s.from) || !validDate(s.to) || s.from > s.to)))
            fail();
        if (!s.automaticDates) {
            auto day = [](const std::string &v) {
                using namespace std::chrono;
                return sys_days{year{std::stoi(v.substr(0, 4))} / unsigned(std::stoi(v.substr(5, 2))) /
                                unsigned(std::stoi(v.substr(8, 2)))};
            };
            if (day(s.to) - day(s.from) > std::chrono::days{7320})
                fail();
        }
    }
    if (p.networkPositions.size() > 10000)
        fail();
    std::unordered_set<std::string> tasks, seen;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    for (const auto &v : p.networkPositions)
        if (!tasks.contains(v.taskId) || !seen.insert(v.taskId).second || v.x < -1000000 || v.x > 1000000 ||
            v.y < -1000000 || v.y > 1000000)
            fail();
}
} // namespace ppm
