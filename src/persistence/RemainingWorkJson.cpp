#include "persistence/RemainingWorkJson.h"
#include "domain/Project.h"
#include <QJsonObject>
#include <cmath>
namespace ppm::json {
namespace {
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"파일 객체·문자열·정수·필드·한도를 확인하세요."});
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        invalid();
    auto o = v.toObject();
    if (o.size() != keys.size())
        invalid();
    for (const auto &k : keys)
        if (!o.contains(k))
            invalid();
    return o;
}
std::string str(const QJsonValue &v) {
    if (!v.isString() || v.toString().size() > 1024)
        invalid();
    return v.toString().toStdString();
}
QJsonArray array(const QJsonValue &v, int limit) {
    if (!v.isArray() || v.toArray().size() > limit)
        invalid();
    return v.toArray();
}
} // namespace
QJsonArray encodeRemainingWork(const std::vector<RemainingWorkPlan> &plans) {
    QJsonArray out;
    for (const auto &v : plans) {
        QJsonArray days;
        for (const auto &day : v.days)
            days.append(QJsonObject{{"date", QString::fromStdString(day.date)},
                                    {"workUnitMinutes", double(day.workUnitMinutes)}});
        out.append(QJsonObject{{"taskId", QString::fromStdString(v.taskId)},
                               {"resourceId", QString::fromStdString(v.resourceId)},
                               {"statusAt", QString::fromStdString(v.statusAt)},
                               {"days", days}});
    }
    return out;
}
std::vector<RemainingWorkPlan> decodeRemainingWork(const QJsonValue &value) {
    std::vector<RemainingWorkPlan> plans;
    std::size_t count = 0;
    for (const auto &entry : array(value, 10000)) {
        const auto o = object(entry, {"taskId", "resourceId", "statusAt", "days"});
        RemainingWorkPlan plan{str(o["taskId"]), str(o["resourceId"]), str(o["statusAt"]), {}};
        for (const auto &v : array(o["days"], 1024)) {
            if (++count > 100000)
                invalid();
            const auto d = object(v, {"date", "workUnitMinutes"});
            auto n = d["workUnitMinutes"];
            const auto work = n.toDouble(-1);
            if (!n.isDouble() || !std::isfinite(work) || work < 0 || work > double(maxWork) || std::floor(work) != work)
                invalid();
            plan.days.push_back({str(d["date"]), std::int64_t(work)});
        }
        plans.push_back(std::move(plan));
    }
    return plans;
}
} // namespace ppm::json
