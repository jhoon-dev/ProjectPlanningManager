#include "calculation/Scheduler.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
jobs::Project fixture(int count, const std::string &profile) {
    jobs::Project p;
    p.documentId = "forecast-benchmark";
    p.name = "Layered remaining network";
    p.startDate = "2026-11-02";
    p.statusAt = "2026-11-02 18:00";
    p.calculationProfile = profile == "legacy" ? "native-fs0-workminutes-v1"
                           : profile == "pdm"  ? jobs::constraintProfile
                                               : jobs::effortProfile;
    if (profile == "effort") {
        p.calendars = {{"pm", "Afternoon", {1, 2, 3, 4, 5}, {{780, 1080}}, {}}};
        for (int i = 0; i < 8; ++i) {
            jobs::Resource r{"R" + std::to_string(i), "Resource", "labor", 100, {}};
            if (i % 2)
                r.calendarId = "pm";
            p.resources.push_back(r);
        }
    }
    for (int i = 0; i < count; ++i) {
        jobs::Task t{"T" + std::to_string(i), "Task", 60 + i % 3 * 15, {}};
        if (i >= 100)
            t.predecessors.push_back("T" + std::to_string(i - 100));
        if (profile == "effort") {
            if (i % 3 == 0) {
                t.taskType = jobs::TaskType::FixedWork;
                t.workDriven = true;
                t.workUnitMinutes = 6000;
            }
            if (i % 25 == 0)
                t.breaks = {{"2026-11-03 10:00", "2026-11-04 10:00"}};
            p.assignments.push_back({t.id, "R" + std::to_string(i % 8), 100});
        }
        p.tasks.push_back(t);
    }
    return p;
}
std::uint64_t datesHash(const jobs::ScheduleResult &r, bool allPlanFields) {
    std::uint64_t hash = 14695981039346656037ULL;
    auto add = [&](const std::string &s) {
        for (const unsigned char c : s) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        hash ^= 255;
        hash *= 1099511628211ULL;
    };
    for (const auto &row : r.tasks) {
        add(row.taskId);
        add(row.start);
        add(row.finish);
        add(row.remainingStart.value_or(""));
        if (allPlanFields) {
            for (const auto value : {row.earlyStartMinutes, row.earlyFinishMinutes, row.lateStartMinutes,
                                     row.lateFinishMinutes, row.totalFloatMinutes, row.freeFloatMinutes})
                add(std::to_string(value));
            add(row.critical ? "1" : "0");
            for (const auto &id : row.drivingPredecessors)
                add(id);
        }
    }
    return hash;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: forecast benchmark <tasks> <legacy|pdm|effort> <repeats>");
        const int count = std::stoi(argv[1]), repeats = std::stoi(argv[3]);
        const std::string profile = argv[2];
        if (count < 1 || count > 100000 || repeats < 1 || repeats > 100 ||
            (profile != "legacy" && profile != "pdm" && profile != "effort"))
            throw std::runtime_error("invalid benchmark arguments");
        const auto p = fixture(count, profile);
        const auto plan = jobs::calculatePlan(p);
        if (plan.status != jobs::CalculationStatus::Valid)
            throw std::runtime_error("benchmark plan failed");
        std::uint64_t hash = 0;
#ifdef _MSC_VER
        const auto compiler = "MSVC " + std::to_string(_MSC_VER);
#else
        const std::string compiler = __VERSION__;
#endif
        std::cout << "{\"tasks\":" << count << ",\"profile\":\"" << profile << "\",\"repeats\":" << repeats
                  << ",\"plan_hash\":\"" << datesHash(plan, true) << "\",\"compiler\":\"" << compiler
                  << "\",\"milliseconds\":[";
        for (int i = 0; i < repeats; ++i) {
            const auto begin = std::chrono::steady_clock::now();
            const auto r = jobs::calculate(p);
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            if (r.status != jobs::CalculationStatus::Valid || (i && datesHash(r, false) != hash))
                throw std::runtime_error("benchmark forecast failed or changed dates");
            hash = datesHash(r, false);
            std::cout << (i ? "," : "") << elapsed;
        }
        std::cout << "],\"forecast_dates_hash\":\"" << hash << "\"}\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
