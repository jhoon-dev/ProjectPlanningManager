#include "calculation/ResourceAvailabilityQuery.h"
#include "domain/ResourceAvailability.h"
#include <algorithm>
#include <chrono>
#include <set>
namespace jobs {
ResourceAvailabilityQuery::ResourceAvailabilityQuery(const Project &p, const ScheduleResult &schedule,
                                                     std::size_t limit,
                                                     const std::set<std::string> *includedResources) {
    if (schedule.status != CalculationStatus::Valid || schedule.documentId != p.documentId ||
        schedule.inputRevision != p.revision || schedule.profile != p.calculationProfile ||
        (schedule.isForecast && schedule.statusAt != p.statusAt))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    std::set<std::string> tasks;
    for (const auto &task : p.tasks)
        tasks.insert(task.id);
    std::set<std::string> seen;
    for (const auto &row : schedule.tasks)
        if (!tasks.contains(row.taskId) || !seen.insert(row.taskId).second || !validStamp(row.start) ||
            !validStamp(row.finish) || row.start > row.finish)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    if (tasks != seen || (schedule.isForecast && (!p.statusAt || !validStamp(*p.statusAt))))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    if (schedule.isForecast)
        forecastStatus_ = p.statusAt;
    std::map<std::string, std::shared_ptr<const TaskCalendar>> calendars;
    TaskCalendar base;
    base.holidays = p.holidays;
    const auto addCalendar = [&](const std::string &id, TaskCalendar calendar) {
        std::sort(calendar.holidays.begin(), calendar.holidays.end());
        calendars[id] = std::make_shared<const TaskCalendar>(std::move(calendar));
    };
    addCalendar("", std::move(base));
    for (const auto &calendar : p.calendars)
        addCalendar(calendar.id, calendar);
    for (const auto &r : p.resources) {
        calculationCheckpoint();
        if (includedResources && !includedResources->contains(r.id))
            continue;
        if (!calendars.contains(r.calendarId))
            throw AppError(ErrorCode::RESOURCE_CALENDAR_INVALID);
        Resource capacity;
        capacity.id = r.id;
        capacity.maxUnits = r.maxUnits;
        capacity.exceptions = r.exceptions;
        capacity.availableFrom = r.availableFrom;
        capacity.availableTo = r.availableTo;
        capacity.availability = r.availability;
        resources_.emplace(r.id, Entry{std::move(capacity), calendars.at(r.calendarId), {}});
    }
    for (const auto &span : resourceOccupancies(p, schedule, limit, includedResources)) {
        calculationCheckpoint();
        ++occupancyCount_;
        if (!resources_.contains(span.resourceId) || span.units < 0 || span.units > 100000 || !validStamp(span.start) ||
            !validStamp(span.finish) || span.start >= span.finish || (forecastStatus_ && span.start < *forecastStatus_))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        auto &events = resources_.at(span.resourceId).demand;
        events[span.start] += span.units;
        events[span.finish] -= span.units;
    }
    for (auto &[id, resource] : resources_) {
        (void)id;
        std::int64_t sum = 0;
        for (auto &[stamp, amount] : resource.demand) {
            (void)stamp;
            calculationCheckpoint();
            sum += amount;
            if (sum < 0)
                throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
            amount = sum;
        }
        if (sum)
            throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    }
}
ResourceAvailabilityPoint ResourceAvailabilityQuery::at(const std::string &id, const std::string &stamp) const {
    calculationCheckpoint();
    if (!validStamp(stamp) || !resources_.contains(id))
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    const auto &r = resources_.at(id);
    if (forecastStatus_ && stamp < *forecastStatus_)
        return resourceAvailabilityPoint(r.resource, *r.calendar, stamp, {},
                                         "상태일 이전의 실제 자원 수요는 추정하지 않습니다.");
    auto it = r.demand.upper_bound(stamp);
    const auto demand = it == r.demand.begin() ? 0 : std::prev(it)->second;
    return resourceAvailabilityPoint(r.resource, *r.calendar, stamp, demand);
}
ResourceAvailabilityPoint resourceAvailabilityPoint(const Resource &r, const TaskCalendar &calendar,
                                                    const std::string &stamp, std::optional<std::int64_t> demand,
                                                    const std::string &issue) {
    calculationCheckpoint();
    if (!validStamp(stamp))
        throw AppError(ErrorCode::CIVIL_TIMESTAMP_INVALID);
    const auto date = stamp.substr(0, 10);
    using namespace std::chrono;
    const auto day = sys_days{year{std::stoi(date.substr(0, 4))} / unsigned(std::stoi(date.substr(5, 2))) /
                              unsigned(std::stoi(date.substr(8, 2)))};
    const auto weekday = int(std::chrono::weekday{day}.iso_encoding());
    const auto minute = std::stoi(stamp.substr(11, 2)) * 60 + std::stoi(stamp.substr(14, 2));
    ResourceAvailabilityPoint out;
    out.resourceId = r.id;
    out.at = stamp;
    out.working = std::find(calendar.weekdays.begin(), calendar.weekdays.end(), weekday) != calendar.weekdays.end() &&
                  std::find(calendar.holidays.begin(), calendar.holidays.end(), date) == calendar.holidays.end() &&
                  std::any_of(calendar.periods.begin(), calendar.periods.end(),
                              [&](const auto &period) { return minute >= period.begin && minute < period.end; });
    out.capacity = out.working ? resourceUnitsAt(r, stamp) : std::optional<int>{0};
    out.demand = demand;
    if (!demand) {
        out.issue = issue;
        return out;
    }
    if (out.capacity) {
        out.remaining = std::int64_t(*out.capacity) - *out.demand;
        out.freeUnits = std::max<std::int64_t>(0, *out.remaining);
        out.shortage = std::max<std::int64_t>(0, -*out.remaining);
    } else
        out.issue = "지정 시각의 자원 가용량이 미설정입니다.";
    return out;
}
} // namespace jobs
