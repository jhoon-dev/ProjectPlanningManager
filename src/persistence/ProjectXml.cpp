#include "domain/ResourceAvailability.h"
#include "domain/XmlCalendar.h"
#include "domain/XmlBaselines.h"
#include "domain/XmlUnitFields.h"
#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlFormula.h"
#include "domain/ResourcePlan.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "persistence/ProjectXml.h"
#include "persistence/XmlBaselineExchange.h"
#include "persistence/XmlActualExport.h"
#include "persistence/ResourceCurveExchange.h"
#include "calculation/ResourcePlanning.h"
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
#include "domain/XmlRollDown.h"
#include "domain/XmlRollUp.h"
#include "domain/CalculationControl.h"
#include "persistence/UdfMapping.h"
#include "persistence/ExchangePackage.h"
#include "persistence/JsonProjectStore.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

namespace jobs {
namespace {
constexpr auto ns = "http://schemas.microsoft.com/project";
constexpr qsizetype byteLimit = 16 * 1024 * 1024;
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}

QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
struct Node {
    QString name, text, path;
    std::vector<Node> children;
    mutable bool used = false;
};
Node readNode(QXmlStreamReader &r, const QString &path, int depth, int &count, qsizetype &pathCharacters) {
    if (depth > 32 || ++count > 300000)
        throw AppError(ErrorCode::FILE_PROJECTXML_001, {});
    pathCharacters += path.size();
    if (r.name().size() > 128 || path.size() > 4096 || pathCharacters > 16 * 1024 * 1024)
        throw AppError(ErrorCode::FILE_PROJECTXML_002, {});
    if (r.namespaceUri() != QLatin1StringView(ns))
        throw AppError(ErrorCode::FILE_PROJECTXML_003, {diagnosticArgument(r.namespaceUri().toString())});
    if (!r.attributes().isEmpty())
        throw AppError(ErrorCode::FILE_PROJECTXML_004, {diagnosticArgument(path)});
    Node n{r.name().toString(), {}, path, {}, false};
    std::map<QString, int> indices;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::DTD || t == QXmlStreamReader::EntityReference)
            throw AppError(ErrorCode::FILE_PROJECTXML_005, {});
        if (t == QXmlStreamReader::StartElement) {
            auto name = r.name().toString();
            int i = ++indices[name];
            n.children.push_back(
                readNode(r, path + "/" + name + "[" + QString::number(i) + "]", depth + 1, count, pathCharacters));
        } else if (t == QXmlStreamReader::Characters)
            n.text += r.text();
        else if (t == QXmlStreamReader::EndElement)
            break;
    }
    if (!n.children.empty() && !n.text.trimmed().isEmpty())
        throw AppError(ErrorCode::FILE_PROJECTXML_006, {diagnosticArgument(path)});
    return n;
}
const Node *child(const Node &n, const QString &name) {
    const Node *found = nullptr;
    for (const auto &c : n.children)
        if (c.name == name) {
            if (found)
                throw AppError(ErrorCode::FILE_PROJECTXML_007, {diagnosticArgument(c.path)});
            found = &c;
        }
    if (found)
        found->used = true;
    return found;
}
QString field(const Node &n, const QString &name, const QString &fallback = {}) {
    auto *c = child(n, name);
    if (!c)
        return fallback;
    if (!c->children.empty())
        throw AppError(ErrorCode::FILE_PROJECTXML_008, {diagnosticArgument(c->path)});
    return c->text;
}
qint64 number(const QString &s, const QString &path, qint64 lo, qint64 hi) {
    static const QRegularExpression re("^[+-]?[0-9]+$");
    bool ok = false;
    auto n = s.trimmed().toLongLong(&ok);
    if (!ok || !re.match(s.trimmed()).hasMatch() || n < lo || n > hi)
        throw AppError(ErrorCode::FILE_PROJECTXML_009, {diagnosticArgument(path)});
    return n;
}
qint64 num(const Node &n, const QString &name, qint64 lo, qint64 hi, const QString &fallback = {}) {
    return number(field(n, name, fallback), n.path + "/" + name, lo, hi);
}
QString uid(const Node &n, const QString &name = "UID") {
    return QString::number(num(n, name, 0, 2147483647));
}
bool flag(const Node &n, const QString &name, bool fallback = false) {
    auto s = field(n, name, fallback ? "1" : "0").trimmed();
    if (s == "1" || s == "true")
        return true;
    if (s == "0" || s == "false")
        return false;
    throw AppError(ErrorCode::FILE_PROJECTXML_010, {diagnosticArgument(n.path), diagnosticArgument(name)});
}
std::vector<const Node *> list(const Node &n, const QString &wrapper, const QString &item) {
    std::vector<const Node *> out;
    auto *w = child(n, wrapper);
    if (!w)
        return out;
    for (const auto &c : w->children)
        if (c.name == item) {
            c.used = true;
            out.push_back(&c);
        }
    // The writer emits one UID=0 project summary in addition to the 10,000 native rows.
    const bool projectSummary =
        wrapper == "Tasks" && item == "Task" && out.size() == 10001 &&
        std::count_if(out.begin(), out.end(), [](const auto *row) { return field(*row, "UID").trimmed() == "0"; }) == 1;
    if (out.size() > 10000 && !projectSummary)
        throw AppError(ErrorCode::FILE_PROJECTXML_011, {diagnosticArgument(w->path)});
    return out;
}
void diagnostic(QJsonArray &d, const QString &path, const QString &text) {
    d.append(QJsonObject{{"path", path}, {"message", text}});
}
void unconverted(const Node &n, QJsonArray &d) {
    for (const auto &c : n.children) {
        bool baselineTime = false;
        if (c.name == "TimephasedData" && (n.name == "Task" || n.name == "Resource" || n.name == "Assignment"))
            for (const auto &type : c.children)
                if (type.name == "Type") {
                    bool ok = false;
                    const auto number = type.text.trimmed().toInt(&ok);
                    baselineTime =
                        ok && (xmlBaselineTimeNumber(number, "task") || xmlBaselineTimeNumber(number, "resource") ||
                               xmlBaselineTimeNumber(number, "assignment"));
                }
        if (baselineTime)
            diagnostic(d, c.path, "외부 번호별 기준선 시간분포 후보 · 기본 미채택 · 번호별 검토에서 확인");
        else if (!c.used)
            diagnostic(d, c.path,
                       c.name == "Baseline" && (n.name == "Task" || n.name == "Resource" || n.name == "Assignment")
                           ? "외부 기준선 후보 · 기본 미채택 · 번호별 검토에서 값과 지원 여부 확인"
                           : "매핑하지 않음 · 원본 XML에 보존");
        else
            unconverted(c, d);
    }
}
QString stamp(const QString &s, const QString &path) {
    static const QRegularExpression re("^([0-9]{4}-[0-9]{2}-[0-9]{2})T([0-9]{2}:[0-9]{2}):00$");
    auto m = re.match(s.trimmed());
    auto out = m.captured(1) + " " + m.captured(2);
    if (!m.hasMatch() || !validStamp(str(out)))
        throw AppError(ErrorCode::FILE_PROJECTXML_012, {diagnosticArgument(path)});
    return out;
}
qint64 duration(const QString &s, const QString &path) {
    static const QRegularExpression re("^PT(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?$");
    auto m = re.match(s.trimmed());
    if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty() && m.captured(3).isEmpty()))
        throw AppError(ErrorCode::FILE_PROJECTXML_013, {diagnosticArgument(path)});
    auto part = [&](int i) { return m.captured(i).isEmpty() ? qint64(0) : number(m.captured(i), path, 0, 300000000); };
    auto seconds = part(1) * 3600 + part(2) * 60 + part(3);
    if (seconds % 60 || seconds / 60 > 5000000)
        throw AppError(ErrorCode::FILE_PROJECTXML_014, {diagnosticArgument(path)});
    return seconds / 60;
}
bool workingFormat(qint64 n) {
    return n == 3 || n == 5 || n == 7 || n == 9 || n == 11 || n == 35 || n == 37 || n == 39 || n == 41 || n == 43;
}
int units(const QString &s, const QString &path, bool zero) {
    static const QRegularExpression re("^(\\d+)(?:\\.(\\d{1,2})0*)?$");
    auto m = re.match(s.trimmed());
    if (!m.hasMatch())
        throw AppError(ErrorCode::FILE_PROJECTXML_015, {diagnosticArgument(path)});
    auto whole = number(m.captured(1), path, 0, 1000);
    auto fraction = m.captured(2).leftJustified(2, '0').toInt();
    auto value = whole * 100 + fraction;
    if (value > 100000 || (!zero && value == 0))
        throw AppError(ErrorCode::FILE_PROJECTXML_016, {diagnosticArgument(path)});
    return int(value);
}
void graph(const Project &p) {
    std::map<std::string, int> incoming;
    std::map<std::string, std::vector<std::string>> outgoing;
    for (const auto &t : p.tasks)
        incoming[t.id] = 0;
    for (const auto &t : p.tasks)
        for (const auto &pred : t.predecessors) {
            if (pred == t.id || !incoming.contains(pred))
                throw AppError(ErrorCode::FILE_PROJECTXML_017, {diagnosticArgument(qs(t.id))});
            ++incoming[t.id];
            outgoing[pred].push_back(t.id);
        }
    std::vector<std::string> ready;
    for (const auto &[id, n] : incoming)
        if (!n)
            ready.push_back(id);
    std::size_t visited = 0;
    while (!ready.empty()) {
        auto id = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto &next : outgoing[id])
            if (!--incoming[next])
                ready.push_back(next);
    }
    if (visited != p.tasks.size())
        throw AppError(ErrorCode::FILE_PROJECTXML_018, {});
}
int minute(const QString &s, const QString &path) {
    static const QRegularExpression re("^([0-9]{2}):([0-9]{2}):00$");
    auto m = re.match(s.trimmed());
    int h = m.captured(1).toInt(), v = m.captured(2).toInt();
    if (!m.hasMatch() || h > 24 || v > 59 || (h == 24 && v))
        throw AppError(ErrorCode::FILE_PROJECTXML_019, {diagnosticArgument(path)});
    return h * 60 + v;
}
using Week = std::array<std::vector<WorkPeriod>, 7>;
struct CalendarInput {
    const Node *node;
    QString base;
};
struct CalendarValue {
    Week week;
    std::set<std::string> holidays;
};
void holiday(const Node &n, CalendarValue &c) {
    if (!child(n, "DayWorking"))
        throw AppError(ErrorCode::FILE_PROJECTXML_020, {diagnosticArgument(n.path)});
    if (flag(n, "DayWorking"))
        throw AppError(ErrorCode::FILE_PROJECTXML_021, {diagnosticArgument(n.path)});
    if (auto *times = child(n, "WorkingTimes"); times && !times->children.empty())
        throw AppError(ErrorCode::FILE_PROJECTXML_022, {diagnosticArgument(n.path)});
    auto *range = child(n, "TimePeriod");
    if (!range)
        throw AppError(ErrorCode::FILE_PROJECTXML_023, {diagnosticArgument(n.path)});
    auto from = stamp(field(*range, "FromDate"), range->path);
    auto last = field(*range, "ToDate");
    // MSPDI uses 23:59:59 for the inclusive end of a whole-day calendar exception.
    if (last.endsWith("T23:59:59")) {
        last.chop(2);
        last += "00";
    }
    auto to = stamp(last, range->path);
    if (from.mid(11) != "00:00" || (to.mid(11) != "00:00" && to.mid(11) != "23:59"))
        throw AppError(ErrorCode::FILE_PROJECTXML_024, {diagnosticArgument(n.path)});
    auto a = QDate::fromString(from.left(10), Qt::ISODate), b = QDate::fromString(to.left(10), Qt::ISODate);
    if (a > b || a.daysTo(b) > 10000)
        throw AppError(ErrorCode::FILE_PROJECTXML_025, {diagnosticArgument(n.path)});
    for (auto day = a; day <= b; day = day.addDays(1)) {
        c.holidays.insert(str(day.toString(Qt::ISODate)));
        if (c.holidays.size() > 10000)
            throw AppError(ErrorCode::FILE_PROJECTXML_026, {});
    }
}
QJsonObject baseReport(const Project &p, bool importing) {
    return {
        {"contract", "jobs-mspdi-plan-v1"},
        {"direction", importing ? "import" : "export"},
        {"format", "Microsoft Project XML"},
        {"namespace", ns},
        {"documentId", qs(p.documentId)},
        {"revision", QString::number(p.revision)},
        {"nativeSnapshotProfile", qs(p.calculationProfile)},
        {"targetPolicy", QJsonObject{{"calculationProfile", effortProfile},
                                     {"lagCalendar", "successor-working-calendar"},
                                     {"projectStartAnchor", "09:00"}}},
        {"compatibility",
         QJsonObject{{"import", importing ? "mapped-subset" : "not-applicable"},
                     {"originalPreservation", importing ? "exact-xml-bytes" : "exact-native-input-snapshot"},
                     {"jobsRecomputation", "unverified"},
                     {"externalRoundTrip", "unverified"}}},
        {"companionPolicy", "원본과 진단은 이 폴더의 동반 파일입니다. .ppm 사본 저장/이동 시 자동 복제되지 않습니다."}};
}
std::int64_t actualMoney(QString text, const std::string &currency) {
    PlanInput money;
    money.currency = currency;
    const auto amount = parseFieldCost(importXmlUdfScalar(money, CustomFieldType::Cost, str(text)), currency);
    if (amount < 0)
        throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
    return amount;
}
std::vector<XmlPeriodCandidate> readPeriodCandidates(const Node &root, const Project &p, QJsonArray &diagnostics) {
    std::map<std::string, const Node *> tasks;
    for (const auto *n : list(root, "Tasks", "Task"))
        tasks.emplace(str(uid(*n)), n);
    std::map<std::string, std::string> names;
    for (const auto &t : p.tasks)
        names.emplace(t.id, t.name);
    const auto currency = str(field(root, "CurrencyCode").trimmed());
    std::set<std::string> describedCosts;
    std::vector<XmlPeriodCandidate> out;
    for (const auto *assignment : list(root, "Assignments", "Assignment")) {
        calculationCheckpoint();
        XmlPeriodCandidate base;
        base.assignmentUid = str(uid(*assignment));
        base.taskUid = str(uid(*assignment, "TaskUID"));
        base.resourceUid = str(uid(*assignment, "ResourceUID"));
        base.name = names.at("xml-task-" + base.taskUid);
        try {
            const auto amount = field(*tasks.at(base.taskUid), "ActualCost");
            if (!amount.isEmpty() && amount != "NA") {
                if (describedCosts.insert(base.taskUid).second)
                    diagnostic(
                        diagnostics, tasks.at(base.taskUid)->path + "/ActualCost",
                        "명시 누계 원가 후보 · 일별 원가 검토에서만 사용하며 계획/기간 합계로 추정하지 않습니다.");
                if (currency.empty())
                    throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
                base.taskActualMinor = actualMoney(amount, currency);
            } else
                base.totalIssue = "작업 누계 ActualCost가 없습니다.";
        } catch (const AppError &) {
            base.totalIssue = "작업 누계 ActualCost의 통화·정밀도·중복을 확인하세요.";
        }
        std::map<std::string, XmlPeriodCandidate> days;
        // Separate measures may cover the same time; fragments of one measure may not.
        std::map<std::pair<std::string, int>, std::map<std::string, std::string>> intervals;
        std::size_t index = 0;
        for (const auto &data : assignment->children) {
            calculationCheckpoint();
            if (data.name != "TimephasedData")
                continue;
            ++index;
            const auto type = num(data, "Type", 1, 100);
            if (type != 2 && type != 6)
                continue;
            data.used = true;
            auto row = base;
            row.key = base.assignmentUid + "/invalid-" + std::to_string(index);
            try {
                const auto rawStart = field(data, "Start");
                // Keep a known date attached to malformed fragments so they cannot
                // be silently omitted from an otherwise valid daily aggregate.
                const auto date = str(rawStart.left(10));
                if (rawStart.size() > 10 && rawStart.at(10) == QLatin1Char('T') && validDate(date)) {
                    row.date = date;
                    row.key = base.assignmentUid + "/" + date;
                }
                row.start = str(stamp(rawStart, data.path));
                row.finish = str(stamp(field(data, "Finish"), data.path));
                row.date = row.start.substr(0, 10);
                row.key = base.assignmentUid + "/" + row.date;
                const auto midnight =
                    QDate::fromString(qs(row.date), Qt::ISODate).addDays(1).toString(Qt::ISODate) + " 00:00";
                if (uid(data) != qs(base.assignmentUid) || num(data, "Unit", 0, 8) != 2 || row.start >= row.finish ||
                    (row.finish.substr(0, 10) != row.date && qs(row.finish) != midnight))
                    throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
                row.dateBucket = row.start == row.date + " 00:00" && qs(row.finish) == midnight;
                if (!row.dateBucket)
                    row.timedFinish = row.finish;
                if (type == 2) {
                    row.workUnitMinutes = curveWorkDuration(field(data, "Value"));
                    row.workParts = 1;
                } else {
                    if (currency.empty())
                        throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
                    row.actualMinor = actualMoney(field(data, "Value"), currency);
                    row.costParts = 1;
                }
            } catch (const AppError &) {
                row.issue = "일별(Unit2) 단일 날짜·분 정밀도 범위·부모 UID·값의 정확한 단위/통화를 확인하세요.";
            }
            if (row.issue.empty()) {
                auto &ranges = intervals[{row.key, int(type)}];
                const auto next = ranges.lower_bound(row.start);
                if ((next != ranges.end() && next->first < row.finish) ||
                    (next != ranges.begin() && std::prev(next)->second > row.start))
                    row.issue = "같은 배정·날짜·실적 종류의 구간이 겹치거나 중복됩니다. 합산하지 않습니다.";
                else
                    ranges.emplace_hint(next, row.start, row.finish);
            }
            auto [it, inserted] = days.emplace(row.key, row);
            if (!inserted) {
                auto &current = it->second;
                if (!row.issue.empty())
                    current.issue = row.issue;
                const auto add = [&](auto &total, const auto &value, auto limit) {
                    if (!value)
                        return;
                    if (total.value_or(0) > limit - *value)
                        current.issue = "같은 배정·날짜의 실제 공수 또는 원가 합계가 한도를 초과합니다.";
                    else
                        total = total.value_or(0) + *value;
                };
                if (current.issue.empty()) {
                    add(current.workUnitMinutes, row.workUnitMinutes, maxWork);
                    add(current.actualMinor, row.actualMinor, maxMoney);
                }
                current.workParts += row.workParts;
                current.costParts += row.costParts;
                if (!current.issue.empty()) {
                    current.workUnitMinutes.reset();
                    current.actualMinor.reset();
                }
                current.start = std::min(current.start, row.start);
                current.finish = std::max(current.finish, row.finish);
                current.timedFinish = std::max(current.timedFinish, row.timedFinish);
                current.dateBucket = current.dateBucket || row.dateBucket;
            }
        }
        if (days.empty() &&
            (!field(*assignment, "ActualWork").isEmpty() || !field(*assignment, "ActualCost").isEmpty())) {
            base.key = base.assignmentUid + "/undated";
            base.issue = "배정 누계만 있고 일별 실제 값이 없습니다. 날짜별 실적으로 배분하지 않습니다.";
            out.push_back(base);
        }
        for (auto &[key, row] : days)
            out.push_back(std::move(row));
        if (out.size() > 10000)
            throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
    }
    if (!out.empty())
        diagnostic(diagnostics, "/Project/Assignments/TimephasedData",
                   "일별 실제 공수(Type2)·원가(Type6) 후보를 읽었습니다. 별도 병합 검토 전에는 채택하지 않습니다. "
                   "같은 날짜의 겹치지 않는 구간은 종류별로 합산하고 누계·초과 공수·계획 값은 중복 합산하지 않습니다.");
    return out;
}
std::vector<XmlActualInterval> readActualIntervals(const Node &root, const Project &p) {
    std::map<std::string, const Node *> tasks;
    for (const auto *n : list(root, "Tasks", "Task"))
        tasks.emplace(str(uid(*n)), n);
    std::map<std::string, std::string> names;
    for (const auto &t : p.tasks)
        names[t.id] = t.name;
    const auto currency = str(field(root, "CurrencyCode").trimmed());
    std::vector<XmlActualInterval> out;
    for (const auto *a : list(root, "Assignments", "Assignment")) {
        XmlActualInterval base;
        base.assignmentUid = str(uid(*a));
        base.taskUid = str(uid(*a, "TaskUID"));
        base.resourceUid = str(uid(*a, "ResourceUID"));
        base.name = names.at("xml-task-" + base.taskUid);
        try {
            const auto raw = field(*tasks.at(base.taskUid), "ActualCost");
            if (raw.isEmpty() || raw == "NA")
                base.totalIssue = "작업 누계 ActualCost가 없습니다.";
            else if (currency.empty())
                base.totalIssue = "원본 통화가 없습니다.";
            else
                base.taskActualMinor = actualMoney(raw, currency);
        } catch (const AppError &) {
            base.totalIssue = "작업 누계 ActualCost의 통화/정밀도/중복을 확인하세요.";
        }
        std::size_t index = 0;
        for (const auto &n : a->children) {
            if (n.name != "TimephasedData")
                continue;
            ++index;
            const auto type = int(num(n, "Type", 1, 100));
            if (type != 2 && type != 6)
                continue;
            calculationCheckpoint();
            auto row = base;
            row.type = type;
            row.sourceIndex = index;
            row.key = base.assignmentUid + "/record-" + std::to_string(index);
            try {
                row.start = str(stamp(field(n, "Start"), n.path));
                row.finish = str(stamp(field(n, "Finish"), n.path));
                const auto first = QDate::fromString(qs(row.start.substr(0, 10)), Qt::ISODate);
                auto last = QDate::fromString(qs(row.finish.substr(0, 10)), Qt::ISODate);
                if (row.finish.substr(11) == "00:00")
                    last = last.addDays(-1);
                row.multipleDays = last > first;
                row.wholeDays = row.start.substr(11) == "00:00" && row.finish.substr(11) == "00:00";
                row.unit = int(num(n, "Unit", 0, 8));
                if (uid(n) != qs(base.assignmentUid) || row.start >= row.finish ||
                    (row.unit != 0 && row.unit != 1 && row.unit != 2 && row.unit != 3 && row.unit != 5 &&
                     row.unit != 8))
                    throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
                if (type == 2)
                    row.workUnitMinutes = curveWorkDuration(field(n, "Value"));
                else {
                    if (currency.empty())
                        throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
                    row.actualMinor = actualMoney(field(n, "Value"), currency);
                }
            } catch (const AppError &) {
                row.issue = "구간의 부모 UID·시각/분 정밀도·단위·정확한 총량을 확인하세요.";
            }
            out.push_back(std::move(row));
            if (out.size() > 10000)
                throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
        }
    }
    return out;
}
} // namespace

QByteArray readProjectXml(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw AppError(ErrorCode::FILE_PROJECTXML_027, {diagnosticArgument(f.errorString())});
    if (f.size() > byteLimit)
        throw AppError(ErrorCode::FILE_PROJECTXML_028, {});
    auto bytes = f.read(byteLimit + 1);
    if (f.error() != QFileDevice::NoError || bytes.size() > byteLimit || !f.atEnd())
        throw AppError(ErrorCode::FILE_PROJECTXML_029, {});
    return bytes;
}
ProjectXmlExchange importProjectXml(const QByteArray &bytes, const QString &sourceName) {
    if (bytes.size() > byteLimit)
        throw AppError(ErrorCode::FILE_PROJECTXML_030, {});
    QXmlStreamReader reader(bytes);
    reader.setEntityExpansionLimit(0);
    int count = 0;
    qsizetype pathCharacters = 0;
    std::optional<Node> root;
    while (!reader.atEnd()) {
        auto token = reader.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            throw AppError(ErrorCode::FILE_PROJECTXML_031, {});
        if (token == QXmlStreamReader::StartElement) {
            if (root || reader.name() != QLatin1StringView("Project"))
                throw AppError(ErrorCode::FILE_PROJECTXML_032, {});
            root = readNode(reader, "/Project", 1, count, pathCharacters);
        }
    }
    if (reader.hasError() || !root)
        throw AppError(ErrorCode::FILE_PROJECTXML_033, {diagnosticArgument(reader.errorString())});
    auto &r = *root;
    Project p;
    p.documentId = str(QUuid::createUuid().toString(QUuid::WithoutBraces));
    p.calculationProfile = effortProfile;
    p.name = str(field(r, "Name"));
    auto start = stamp(field(r, "StartDate"), "/Project/StartDate");
    p.startDate = str(start.left(10));
    p.currency = str(field(r, "CurrencyCode", "KRW"));
    currencyDecimals(p.currency);
    if (!flag(r, "ScheduleFromStart", true))
        throw AppError(ErrorCode::FILE_PROJECTXML_034, {});
    QJsonArray diagnostics, mapping;
    diagnostic(diagnostics, "/Project",
               "기간 기반 계획 변환입니다. 원본 날짜·공수·실적·비용의 계산 동등성은 미검증입니다.");
    diagnostic(diagnostics, "/Project/Calendars",
               "프로젝트 달력을 수입 작업/자원에 명시 배정합니다. 이후 새 작업은 PPM 기본 달력을 사용합니다.");
    if (start.mid(11) != "09:00")
        diagnostic(diagnostics, "/Project/StartDate", "시작 날짜는 보존하며 PPM의 날짜 시작 기준은 09:00입니다.");
    auto saveVersion = field(r, "SaveVersion"), build = field(r, "BuildNumber");
    if (saveVersion.isEmpty())
        throw AppError(ErrorCode::FILE_PROJECTXML_035, {});
    number(saveVersion, "/Project/SaveVersion", 1, 999);
    auto map = [&](const QString &kind, const QString &id, const std::string &target) {
        mapping.append(QJsonObject{{"kind", kind}, {"xmlUid", id}, {"jobsId", qs(target)}});
    };

    std::map<QString, CalendarInput> inputs;
    std::vector<QString> calendarOrder;
    for (auto *c : list(r, "Calendars", "Calendar")) {
        auto id = uid(*c);
        if (inputs.contains(id))
            throw AppError(ErrorCode::FILE_PROJECTXML_036, {diagnosticArgument(id)});
        bool base = flag(*c, "IsBaseCalendar", true);
        auto parent = field(*c, "BaseCalendarUID", "-1").trimmed();
        if (base && parent != "-1")
            throw AppError(ErrorCode::FILE_PROJECTXML_037, {diagnosticArgument(c->path)});
        inputs[id] = {c, base ? QString{} : QString::number(number(parent, c->path, 0, 2147483647))};
        calendarOrder.push_back(id);
    }
    std::map<QString, CalendarValue> resolved;
    std::set<QString> resolving;
    std::size_t expandedHolidays = 0;
    std::function<CalendarValue(const QString &, int)> resolve = [&](const QString &id, int depth) -> CalendarValue {
        if (resolved.contains(id))
            return resolved.at(id);
        if (depth > 32 || resolving.contains(id))
            throw AppError(ErrorCode::FILE_PROJECTXML_038, {diagnosticArgument(id)});
        if (!inputs.contains(id))
            throw AppError(ErrorCode::FILE_PROJECTXML_039, {diagnosticArgument(id)});
        resolving.insert(id);
        const auto &input = inputs.at(id);
        const auto &c = *input.node;
        CalendarValue value;
        if (!input.base.isEmpty())
            value = resolve(input.base, depth + 1);
        std::set<int> seen;
        for (auto *day : list(c, "WeekDays", "WeekDay")) {
            int type = int(num(*day, "DayType", 0, 7));
            if (type == 0) {
                holiday(*day, value);
                continue;
            }
            if (!child(*day, "DayWorking"))
                throw AppError(ErrorCode::FILE_PROJECTXML_040, {diagnosticArgument(day->path)});
            int index = type == 1 ? 6 : type - 2;
            if (!seen.insert(index).second)
                throw AppError(ErrorCode::FILE_PROJECTXML_041, {diagnosticArgument(day->path)});
            auto &periods = value.week[std::size_t(index)];
            periods.clear();
            auto times = list(*day, "WorkingTimes", "WorkingTime");
            if (flag(*day, "DayWorking")) {
                if (times.empty() || times.size() > 5)
                    throw AppError(ErrorCode::FILE_PROJECTXML_042, {diagnosticArgument(day->path)});
                for (auto *time : times)
                    periods.push_back(
                        {minute(field(*time, "FromTime"), time->path), minute(field(*time, "ToTime"), time->path)});
            } else if (!times.empty())
                throw AppError(ErrorCode::FILE_PROJECTXML_043, {diagnosticArgument(day->path)});
        }
        if (input.base.isEmpty() && seen.size() != 7)
            throw AppError(ErrorCode::FILE_PROJECTXML_044, {diagnosticArgument(c.path)});
        for (auto *exception : list(c, "Exceptions", "Exception")) {
            auto type = num(*exception, "Type", 1, 9, "1");
            if ((type != 1 && type != 9) || num(*exception, "Period", 1, 10000, "1") != 1 ||
                flag(*exception, "EnteredByOccurrences"))
                throw AppError(ErrorCode::FILE_PROJECTXML_045, {diagnosticArgument(exception->path)});
            for (const auto &name : {"DaysOfWeek", "MonthItem", "MonthPosition", "Month", "MonthDay"})
                if (child(*exception, name))
                    throw AppError(ErrorCode::FILE_PROJECTXML_046, {diagnosticArgument(exception->path)});
            holiday(*exception, value);
        }
        if (child(c, "WorkWeeks"))
            throw AppError(ErrorCode::FILE_PROJECTXML_047, {diagnosticArgument(c.path)});
        TaskCalendar out;
        out.id = "xml-cal-" + str(id);
        out.name = str(field(c, "Name", "Calendar " + id));
        out.weekdays.clear();
        out.periods.clear();
        for (std::size_t i = 0; i < 7; ++i)
            if (!value.week[i].empty()) {
                out.weekdays.push_back(int(i) + 1);
                if (out.periods.empty())
                    out.periods = value.week[i];
                else if (out.periods != value.week[i])
                    throw AppError(ErrorCode::FILE_PROJECTXML_048, {diagnosticArgument(c.path)});
            }
        out.holidays.assign(value.holidays.begin(), value.holidays.end());
        expandedHolidays += out.holidays.size();
        if (expandedHolidays > 100000)
            throw AppError(ErrorCode::FILE_PROJECTXML_049, {});
        p.calendars.push_back(out);
        map("calendar", id, out.id);
        resolving.erase(id);
        resolved[id] = value;
        return value;
    };
    for (const auto &id : calendarOrder)
        resolve(id, 0);
    auto projectCalendar = QString::number(num(r, "CalendarUID", 0, 2147483647));
    if (!inputs.contains(projectCalendar))
        throw AppError(ErrorCode::FILE_PROJECTXML_050, {diagnosticArgument(projectCalendar)});
    auto calendar = [&](const Node &n) {
        auto id = field(n, "CalendarUID", "-1").trimmed();
        if (id == "-1")
            id = projectCalendar;
        else
            id = QString::number(number(id, n.path, 0, 2147483647));
        if (!inputs.contains(id))
            throw AppError(ErrorCode::FILE_PROJECTXML_051, {diagnosticArgument(n.path)});
        return "xml-cal-" + str(id);
    };

    std::set<QString> taskUids;
    std::map<QString, std::string> taskIds;
    std::vector<std::pair<const Node *, std::size_t>> taskNodes;
    std::vector<std::pair<int, std::string>> stack;
    bool previousLeaf = false;
    int previousLevel = 0;
    for (auto *n : list(r, "Tasks", "Task")) {
        auto id = uid(*n);
        if (!taskUids.insert(id).second)
            throw AppError(ErrorCode::FILE_PROJECTXML_052, {diagnosticArgument(id)});
        if (flag(*n, "IsSubproject") || child(*n, "Project"))
            throw AppError(ErrorCode::FILE_PROJECTXML_053, {diagnosticArgument(n->path)});
        if (id == "0") {
            diagnostic(diagnostics, n->path, "프로젝트 요약 · 실행 작업으로 변환하지 않음");
            continue;
        }
        if (flag(*n, "IsNull"))
            throw AppError(ErrorCode::FILE_PROJECTXML_054, {diagnosticArgument(n->path)});
        if (flag(*n, "Manual") || flag(*n, "ExternalTask") || !field(*n, "SubprojectName").isEmpty())
            throw AppError(ErrorCode::FILE_PROJECTXML_055, {diagnosticArgument(n->path)});
        auto level = int(num(*n, "OutlineLevel", 1, 32, "1"));
        if (level > previousLevel + 1 || (previousLeaf && level > previousLevel))
            throw AppError(ErrorCode::FILE_PROJECTXML_056, {diagnosticArgument(n->path)});
        while (!stack.empty() && stack.back().first >= level)
            stack.pop_back();
        auto parent = stack.empty() ? std::string{} : stack.back().second;
        if (level > 1 && (stack.empty() || stack.back().first != level - 1))
            throw AppError(ErrorCode::FILE_PROJECTXML_057, {diagnosticArgument(n->path)});
        auto name = str(field(*n, "Name"));
        bool summary = flag(*n, "Summary");
        if (summary) {
            if (child(*n, "PredecessorLink"))
                throw AppError(ErrorCode::FILE_PROJECTXML_058, {diagnosticArgument(n->path)});
            WbsNode group{"xml-wbs-" + str(id), name, parent};
            p.wbsNodes.push_back(group);
            stack.emplace_back(level, group.id);
            map("wbs", id, group.id);
        } else {
            Task t;
            t.id = "xml-task-" + str(id);
            t.name = name;
            t.wbsId = parent;
            t.calendarId = calendar(*n);
            auto d = field(*n, "Duration");
            if (!d.isEmpty())
                t.durationMinutes = duration(d, n->path + "/Duration");
            const auto format = num(*n, "DurationFormat", 0, 100, "7");
            if (!workingFormat(format))
                throw AppError(ErrorCode::FILE_PROJECTXML_059, {diagnosticArgument(n->path)});
            if (format >= 35)
                diagnostic(diagnostics, n->path + "/DurationFormat",
                           "근무분 값만 매핑 · 추정 기간 표시를 변환하지 않음");
            if (flag(*n, "Milestone") && t.durationMinutes != 0)
                throw AppError(ErrorCode::FILE_PROJECTXML_060, {diagnosticArgument(n->path)});
            if (num(*n, "Type", 0, 2, "0") != 1)
                diagnostic(diagnostics, n->path + "/Type",
                           "기간 기반 고정 기간으로 투영 · 공수/작업 유형 계산 규칙은 보존 원본 참조");
            static const std::array<ConstraintType, 8> constraints{
                ConstraintType::ASAP, ConstraintType::ALAP, ConstraintType::MSO,  ConstraintType::MFO,
                ConstraintType::SNET, ConstraintType::SNLT, ConstraintType::FNET, ConstraintType::FNLT};
            auto type = num(*n, "ConstraintType", 0, 7, "0");
            t.constraintType = constraints[std::size_t(type)];
            if (type > 1)
                t.constraintAt = str(stamp(field(*n, "ConstraintDate"), n->path + "/ConstraintDate"));
            auto deadline = field(*n, "Deadline");
            if (!deadline.isEmpty())
                t.deadlineAt = str(stamp(deadline, n->path + "/Deadline"));
            if (flag(*n, "IgnoreResourceCalendar"))
                throw AppError(ErrorCode::FILE_PROJECTXML_061, {diagnosticArgument(n->path)});
            taskIds[id] = t.id;
            map("task", id, t.id);
            taskNodes.emplace_back(n, p.tasks.size());
            p.tasks.push_back(std::move(t));
        }
        previousLeaf = !summary;
        previousLevel = level;
    }
    for (const auto &[n, index] : taskNodes) {
        auto &task = p.tasks[index];
        for (const auto &link : n->children)
            if (link.name == "PredecessorLink") {
                link.used = true;
                auto pred = uid(link, "PredecessorUID");
                if (!taskIds.contains(pred) || flag(link, "CrossProject") || !field(link, "CrossProjectName").isEmpty())
                    throw AppError(ErrorCode::FILE_PROJECTXML_062, {diagnosticArgument(link.path)});
                static const std::array<DependencyType, 4> types{DependencyType::FF, DependencyType::FS,
                                                                 DependencyType::SF, DependencyType::SS};
                auto type = types[std::size_t(num(link, "Type", 0, 3, "1"))];
                if (child(link, "LinkLag") && !child(link, "LagFormat"))
                    throw AppError(ErrorCode::FILE_PROJECTXML_063, {diagnosticArgument(link.path)});
                auto lag = num(link, "LinkLag", -50000000, 50000000, "0");
                if (lag % 10)
                    throw AppError(ErrorCode::FILE_PROJECTXML_064, {diagnosticArgument(link.path)});
                const auto format = num(link, "LagFormat", 0, 100, "7");
                if (!workingFormat(format))
                    throw AppError(ErrorCode::FILE_PROJECTXML_065, {diagnosticArgument(link.path)});
                if (format >= 35)
                    diagnostic(diagnostics, link.path + "/LagFormat",
                               "근무분 값만 매핑 · 추정 지연 표시를 변환하지 않음");
                task.predecessors.push_back(taskIds.at(pred));
                if (type != DependencyType::FS || lag)
                    task.dependencyRules.push_back({taskIds.at(pred), type, lag / 10});
            }
    }
    std::map<QString, std::string> resources;
    std::set<QString> resourceUids;
    bool availabilityIncomplete = false;
    std::size_t availabilityDays = 0;
    for (auto *n : list(r, "Resources", "Resource")) {
        auto id = uid(*n);
        if (!resourceUids.insert(id).second)
            throw AppError(ErrorCode::FILE_PROJECTXML_066, {diagnosticArgument(id)});
        auto name = field(*n, "Name");
        if (id == "0" && name.trimmed().isEmpty()) {
            diagnostic(diagnostics, n->path, "미배정 placeholder · 자원으로 변환하지 않음");
            continue;
        }
        if (num(*n, "Type", 0, 2, "1") != 1)
            throw AppError(ErrorCode::FILE_PROJECTXML_067, {diagnosticArgument(n->path)});
        Resource resource;
        resource.id = "xml-resource-" + str(id);
        resource.name = str(name);
        resource.calendarId = calendar(*n);
        auto capacity = field(*n, "MaxUnits");
        resource.maxUnits = capacity.isEmpty() ? std::nullopt : std::optional<int>{units(capacity, n->path, true)};
        try {
            const auto endpoint = [&](const Node &node, const QString &tag, bool finish) -> std::optional<std::string> {
                const auto raw = field(node, tag);
                if (raw.isEmpty())
                    return {};
                auto at = normalizeFieldDateTime(str(raw));
                if (at.size() != 19 || (at.substr(17) != "00" && (!finish || at.substr(17) != "59")))
                    throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                at.resize(16);
                at[10] = ' ';
                return at;
            };
            resource.availableFrom = endpoint(*n, "AvailableFrom", false);
            resource.availableTo = endpoint(*n, "AvailableTo", true);
            for (const auto &table : n->children)
                if (table.name == "AvailabilityPeriods")
                    for (const auto &entry : table.children)
                        if (entry.name != "AvailabilityPeriod")
                            throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
            bool daily = !resource.availableFrom && !resource.availableTo;
            std::size_t days = 0;
            for (const auto *period : list(*n, "AvailabilityPeriods", "AvailabilityPeriod")) {
                for (const auto &element : period->children)
                    if (element.name != "AvailableFrom" && element.name != "AvailableTo" &&
                        element.name != "AvailableUnits")
                        throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                const auto raw = field(*period, "AvailableUnits");
                AvailabilityPeriod row{
                    endpoint(*period, "AvailableFrom", false), endpoint(*period, "AvailableTo", true),
                    raw.isEmpty() ? std::optional<int>{} : std::optional<int>{units(raw, period->path, true)}};
                if (row.from && row.to && row.from->ends_with(" 00:00") && row.to->ends_with(" 23:59")) {
                    const auto begin = QDate::fromString(qs(row.from->substr(0, 10)), Qt::ISODate);
                    const auto end = QDate::fromString(qs(row.to->substr(0, 10)), Qt::ISODate);
                    const auto length = begin.daysTo(end) + 1;
                    if (length < 1 || length > 10000 || days + std::size_t(length) > 100000)
                        daily = false;
                    else
                        days += std::size_t(length);
                } else
                    daily = false;
                resource.availability.push_back(std::move(row));
            }
            validateResourceAvailability(resource);
            if (daily && availabilityDays + days <= 100000) {
                for (const auto &row : resource.availability) {
                    const auto begin = QDate::fromString(qs(row.from->substr(0, 10)), Qt::ISODate);
                    const auto end = QDate::fromString(qs(row.to->substr(0, 10)), Qt::ISODate);
                    for (auto day = begin; day <= end; day = day.addDays(1)) {
                        calculationCheckpoint();
                        resource.exceptions.push_back({str(day.toString(Qt::ISODate)), row.units});
                    }
                }
                std::sort(resource.exceptions.begin(), resource.exceptions.end(),
                          [](const auto &a, const auto &b) { return a.date < b.date; });
                availabilityDays += days;
                resource.availability.clear();
            }
        } catch (const AppError &) {
            availabilityIncomplete = true;
            resource.exceptions.clear();
            resource.availability.clear();
            resource.availableFrom.reset();
            resource.availableTo.reset();
            diagnostic(diagnostics, n->path + "/AvailabilityPeriods",
                       "중복"
                       "·잘못된 가용기간이나 분 미만 경계는 적용하지 않습니다. 원본을 보존하며 날짜별 가용률 조회식은 "
                       "비활성으로 가져옵니다.");
        }
        resources[id] = resource.id;
        map("resource", id, resource.id);
        p.resources.push_back(std::move(resource));
    }
    std::set<QString> assignments;
    struct XmlCurveInput {
        std::string task, resource, externalId;
        int code;
        std::optional<std::int64_t> work;
        std::string origin;
        std::vector<CurveTimephase> rows;
    };
    std::vector<XmlCurveInput> curveInputs;
    for (auto *n : list(r, "Assignments", "Assignment")) {
        auto id = uid(*n);
        if (!assignments.insert(id).second)
            throw AppError(ErrorCode::FILE_PROJECTXML_068, {diagnosticArgument(id)});
        auto t = uid(*n, "TaskUID"), resource = uid(*n, "ResourceUID");
        if (!taskIds.contains(t) || !resources.contains(resource))
            throw AppError(ErrorCode::FILE_PROJECTXML_069, {diagnosticArgument(n->path)});
        p.assignments.push_back({taskIds.at(t), resources.at(resource), units(field(*n, "Units"), n->path, false)});
        map("assignment", id, taskIds.at(t) + "/" + resources.at(resource));
        auto contour = field(*n, "WorkContour");
        const auto code = contour.isEmpty() ? 0 : int(num(*n, "WorkContour", 0, 8));
        XmlCurveInput curve{taskIds.at(t), resources.at(resource), str(id), code, {}, {}, {}};
        for (const auto &data : n->children)
            if (data.name == "TimephasedData" && num(data, "Type", 1, 100) == 1) {
                data.used = true;
                if (curve.rows.size() >= 1024)
                    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"XML 시간대별 계획 공수 한도1,024구간 초과"});
                if (field(data, "UID", id) != id)
                    throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID);
                (void)num(data, "Unit", 0, 8, "0");
                curve.rows.push_back({str(stamp(field(data, "Start"), data.path)),
                                      str(stamp(field(data, "Finish"), data.path)),
                                      curveWorkDuration(field(data, "Value")), 0});
            }
        if (code || !curve.rows.empty()) {
            const auto actual = field(*n, "ActualWork");
            if (!curve.rows.empty() && !actual.isEmpty() && curveWorkDuration(actual) > 0) {
                diagnostic(diagnostics, n->path + "/TimephasedData",
                           "실제공수가 있는 Type1 잔여 분포를 전체 계획 곡선으로 추정하지 않음 · 원본 XML에 보존");
            } else {
                const auto work = field(*n, "Work");
                if (!work.isEmpty())
                    curve.work = curveWorkDuration(work);
                auto start = field(*n, "Start");
                if (start.isEmpty())
                    for (const auto &[node, index] : taskNodes)
                        if (p.tasks[index].id == curve.task)
                            start = field(*node, "Start");
                if (!start.isEmpty())
                    curve.origin = str(stamp(start, n->path));
                curveInputs.push_back(std::move(curve));
            }
        }
    }
    auto guidText = [&](const QString &text) {
        const auto guid = QUuid(text.trimmed());
        if (guid.isNull())
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"잘못된 목록 GUID"});
        return str(guid.toString(QUuid::WithoutBraces));
    };
    auto readMasks = [&](const std::vector<const Node *> &nodes) {
        if (nodes.size() > 16)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"코드 마스크 수준 한도 초과"});
        std::vector<XmlCodeMask> masks;
        for (const auto *n : nodes) {
            auto length = field(*n, "Length").trimmed();
            masks.push_back(
                {int(num(*n, "Level", 1, 16, QString::number(masks.size() + 1))), int(num(*n, "Type", 0, 3, "3")),
                 length.isEmpty() ? 0 : int(number(length, n->path, 0, 255)), str(field(*n, "Separator", "."))});
        }
        std::sort(masks.begin(), masks.end(), [](const auto &a, const auto &b) { return a.level < b.level; });
        validateXmlMasks(masks);
        return masks;
    };
    if (const auto *n = child(r, "WBSMasks")) {
        XmlWbsCodes codes;
        codes.prefix = str(field(*n, "Prefix"));
        codes.generate = flag(*n, "GenerateCodes");
        codes.verifyUnique = flag(*n, "VerifyUniqueCodes");
        std::vector<const Node *> rows;
        for (const auto &m : n->children)
            if (m.name == "WBSMask") {
                m.used = true;
                rows.push_back(&m);
            }
        codes.masks = readMasks(rows);
        p.xmlWbs = std::move(codes);
    }
    for (const auto *n : list(r, "Tasks", "Task")) {
        const auto raw = field(*n, "WBS");
        const auto level = field(*n, "WBSLevel");
        if (uid(*n) == "0")
            continue;
        if (!raw.isEmpty()) {
            if (!p.xmlWbs)
                p.xmlWbs = XmlWbsCodes{};
            const bool group = flag(*n, "Summary");
            p.xmlWbs->values.push_back(
                {group ? "wbs" : "task", (group ? "xml-wbs-" : "xml-task-") + str(uid(*n)), str(raw)});
            if (!level.isEmpty() && !p.xmlWbs->masks.empty() && str(level) != xmlWbsLevel(*p.xmlWbs, str(raw)))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"WBS/WBSLevel 값이 서로 다릅니다."});
        } else if (!level.isEmpty())
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"WBS 값 없이 WBSLevel이 있습니다."});
    }
    validateXmlWbsCodes(p);
    auto lookupScalar = [&](CustomFieldType type, const QString &raw, bool tableValue = false, int format = 3,
                            bool packedDate = false) {
        return importXmlUdfScalar(p, type, str(raw), format, tableValue, packedDate);
    };
    auto optionLabels = [&](CustomField &f) {
        if (!f.xmlLookup->masks.empty()) {
            validateXmlLookupMask(*f.xmlLookup);
            rebuildXmlLookupOptions(f);
            return;
        }
        f.options.clear();
        std::set<std::string> labels;
        for (const auto &v : f.xmlLookup->values) {
            std::vector<std::string> path;
            auto current = &v;
            std::set<std::string> seen;
            while (current) {
                if (!seen.insert(current->optionId).second || seen.size() > 16)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 계층 순환/깊이 초과"});
                auto text = f.xmlLookup->valueType == CustomFieldType::Flag
                                ? QString(current->value == "1" ? "예" : "아니요")
                                : qs(current->value).simplified();
                path.push_back(str(text.isEmpty() ? QString("(빈 텍스트)") : text.left(180)));
                if (current->parentId.empty())
                    break;
                const auto parent = std::find_if(f.xmlLookup->values.begin(), f.xmlLookup->values.end(),
                                                 [&](const auto &o) { return o.optionId == current->parentId; });
                if (parent == f.xmlLookup->values.end())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 부모 참조 누락"});
                current = &*parent;
            }
            std::string name;
            for (auto it = path.rbegin(); it != path.rend(); ++it)
                name += (name.empty() ? "" : " › ") + *it;
            name = str(qs(name).left(240));
            if (!labels.insert(name).second) {
                name += " [" + v.optionId + "]";
                if (!labels.insert(name).second)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 표시 이름 중복"});
            }
            f.options.push_back({v.optionId, name});
        }
    };
    struct LookupInput {
        XmlLookup lookup;
        std::string fieldId;
        bool supported = true;
    };
    std::map<std::string, LookupInput> lookupTables;
    std::map<std::string, std::string> tableByField, globalValueIds, globalValueGuids;
    std::size_t lookupItems = 0;
    auto readLookupValues = [&](const std::vector<const Node *> &nodes, XmlLookup &l, bool outline) {
        if (nodes.size() > 1000 || (lookupItems += nodes.size()) > 100000)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 항목 개수 한도 초과"});
        std::map<std::string, std::string> byId;
        bool supported = true;
        for (const auto *n : nodes) {
            calculationCheckpoint();
            XmlLookupValue v;
            if (outline) {
                if (child(*n, "ValueID"))
                    v.valueId = str(QString::number(num(*n, "ValueID", 0, 2147483647)));
                v.guid = guidText(field(*n, "FieldGUID"));
                const auto type = num(*n, "Type", 0, 100, "21");
                const auto parsedType = xmlLookupType(int(type));
                const auto scalar = parsedType.value_or(CustomFieldType::Text);
                if (!parsedType)
                    supported = false;
                if (!l.values.empty() && scalar != l.valueType)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"한 목록의 값 형식이 서로 다릅니다."});
                l.valueType = scalar;
                auto parent = field(*n, "ParentValueID").trimmed();
                if (!parent.isEmpty() && parent != "0" && parent != "-1")
                    v.parentId = str(QString::number(number(parent, n->path, 0, 2147483647)));
            } else
                v.valueId = str(QString::number(num(*n, "ID", 0, 2147483647)));
            v.optionId = "xml-choice-" + (v.guid.empty() ? v.valueId : v.guid);
            if (!v.valueId.empty() &&
                (!byId.emplace(v.valueId, v.optionId).second || !globalValueIds.emplace(v.valueId, l.tableGuid).second))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 목록 값 ID"});
            if (!v.guid.empty() && !globalValueGuids.emplace(v.guid, l.tableGuid).second)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 목록 값 GUID"});
            v.value =
                supported ? lookupScalar(l.valueType, field(*n, "Value"), true, 3, outline) : str(field(*n, "Value"));
            v.description = str(field(*n, "Description"));
            l.values.push_back(std::move(v));
        }
        for (auto &v : l.values)
            if (!v.parentId.empty()) {
                if (!byId.contains(v.parentId))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"다른 목록/누락 부모 ID"});
                v.parentId = byId.at(v.parentId);
            }
        return supported;
    };
    for (const auto *node : list(r, "OutlineCodes", "OutlineCode")) {
        LookupInput input;
        input.lookup.tableGuid = guidText(field(*node, "Guid"));
        input.lookup.tableName = str(field(*node, "FieldName", field(*node, "Alias")));
        if (child(*node, "FieldID"))
            input.fieldId = str(QString::number(num(*node, "FieldID", 1, 2147483647)));
        input.lookup.leafOnly = flag(*node, "LeafOnly");
        input.lookup.allLevelsRequired = flag(*node, "AllLevelsRequired");
        input.lookup.masks = readMasks(list(*node, "Masks", "Mask"));
        input.lookup.restrictValues = flag(*node, "OnlyTableValuesAllowed");
        input.supported = readLookupValues(list(*node, "Values", "Value"), input.lookup, true);
        if (!input.supported)
            diagnostic(diagnostics, node->path, "미지원 목록 값 형식 · 원본에 보존");
        if (!input.fieldId.empty() && !tableByField.emplace(input.fieldId, input.lookup.tableGuid).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 목록 필드 ID"});
        if (!lookupTables.emplace(input.lookup.tableGuid, std::move(input)).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 목록 GUID"});
    }
    std::map<std::string, ExternalField> udf;
    std::vector<XmlFormulaAlias> formulaAliases;
    std::set<std::string> declaredUdf, declaredGuids;
    struct SecondaryField {
        std::string id, name;
        CustomFieldType type;
        std::optional<XmlLookup> lookup;
    };
    std::vector<SecondaryField> secondaryFields;
    struct RollDownInput {
        FieldScope scope;
        std::string sourceId, secondary;
        bool automatic;
    };
    std::vector<RollDownInput> rollDownInputs;
    for (const auto *definition : list(r, "ExtendedAttributes", "ExtendedAttribute")) {
        calculationCheckpoint();
        const auto id = str(QString::number(num(*definition, "FieldID", 1, 2147483647)));
        if (!declaredUdf.insert(id).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 XML FieldID: " + id});
        auto type = xmlUdfType(id);
        const auto dynamic = xmlDynamicUdfScope(id);
        const auto cf = field(*definition, "CFType").trimmed();
        if (dynamic) {
            const auto parsedType = xmlCfType(str(cf));
            if (!parsedType) {
                diagnostic(diagnostics, definition->path, "미지원 기업/배정 필드 형식 · 원본 XML에 보존");
                continue;
            }
            type = std::pair{*dynamic, *parsedType};
        }
        if (!type) {
            diagnostic(diagnostics, definition->path, "미지원 사용자 필드 형식 · 원본 XML에 보존");
            continue;
        }
        const auto elem = field(*definition, "ElemType").trimmed();
        const auto expected = qs(xmlCfType(type->second));
        const auto expectedElem = type->first == FieldScope::Assignment ? "23"
                                  : type->first == FieldScope::Resource ? "21"
                                                                        : "20";
        if ((!cf.isEmpty() && cf != expected) || (!elem.isEmpty() && elem != expectedElem))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML UDF FieldID와 형식/범위 불일치"});
        auto name = field(*definition, "Alias");
        const auto fieldName = field(*definition, "FieldName");
        if (name.isEmpty())
            name = fieldName;
        std::optional<XmlLookup> lookup;
        const auto tableRef = field(*definition, "Ltuid").trimmed();
        if (!tableRef.isEmpty() || tableByField.contains(id)) {
            if (child(*definition, "ValueList"))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"두 종류의 목록 정의가 동시에 있습니다."});
            const auto tableId = tableRef.isEmpty() ? tableByField.at(id) : guidText(tableRef);
            if (!lookupTables.contains(tableId))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"정의 없는 목록 GUID 참조"});
            if (!lookupTables.at(tableId).supported) {
                diagnostic(diagnostics, definition->path, "미지원 목록 · 원본에 보존");
                continue;
            }
            lookup = lookupTables.at(tableId).lookup;
            if (dateTimeCustomField(lookup->valueType) && dateTimeCustomField(type->second)) {
                if (lookup->valueType != type->second)
                    diagnostic(diagnostics, definition->path, "날짜 목록 형식을 필드의 시작/종료 역할에 맞춰 정규화");
                for (auto &v : lookup->values)
                    v.value = normalizeLookupScalar(
                        type->second,
                        type->second != CustomFieldType::Date && v.value.size() == 10 ? v.value + "T00:00:00" : v.value,
                        p.currency);
            } else if (!lookup->values.empty() && lookup->valueType != type->second)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"필드와 목록의 값 형식 불일치"});
            lookup->valueType = type->second;
        } else if (child(*definition, "ValueList")) {
            lookup.emplace();
            lookup->valueType = type->second;
            const auto values = list(*definition, "ValueList", "Value");
            if (values.empty())
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"일반 값 목록이 비어 있습니다."});
            readLookupValues(values, *lookup, false);
            std::set<std::string> raw;
            for (const auto &v : lookup->values)
                if (!raw.insert(v.value).second)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"일반 목록의 직접 값이 중복됩니다."});
        }
        const auto maximum = int(num(*definition, "MaxMultiValues", 0, 1000, "1"));
        if (!lookup && (child(*definition, "DefaultGuid") || maximum != 1 || xmlOutlineUdf(id))) {
            diagnostic(diagnostics, definition->path, "선택 목록 정의가 없는 필드 · 원본에 보존");
            continue;
        }
        auto imported = importUdfField(p, true, type->first, id, str(name),
                                       lookup ? CustomFieldType::Choice : type->second, mapping);
        if (lookup) {
            lookup->maxValues = maximum;
            lookup->restrictValues = flag(*definition, "RestrictValues", lookup->restrictValues);
            lookup->fieldName = str(fieldName);
            if (child(*definition, "Guid"))
                lookup->fieldGuid = guidText(field(*definition, "Guid"));
            std::optional<std::string> selectedDefault;
            if (child(*definition, "Default")) {
                const auto ref = str(QString::number(num(*definition, "Default", 0, 2147483647)));
                const auto found = std::find_if(lookup->values.begin(), lookup->values.end(),
                                                [&](const auto &v) { return v.valueId == ref; });
                if (found == lookup->values.end())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기본값 ID가 목록에 없습니다."});
                selectedDefault = found->optionId;
            }
            if (child(*definition, "DefaultGuid")) {
                const auto ref = guidText(field(*definition, "DefaultGuid"));
                const auto found = std::find_if(lookup->values.begin(), lookup->values.end(),
                                                [&](const auto &v) { return v.guid == ref; });
                if (found == lookup->values.end() || (selectedDefault && *selectedDefault != found->optionId))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기본값 GUID/ID 참조 불일치"});
                selectedDefault = found->optionId;
            }
            if (selectedDefault)
                lookup->defaultOptionId = *selectedDefault;
            auto &native = customFields(p, type->first).back();
            native.xmlLookup = *lookup;
            optionLabels(native);
            imported.lookup = *lookup;
            auto mapped = mapping.last().toObject();
            mapped["lookupEntries"] = int(lookup->values.size());
            mapped["maxValues"] = maximum;
            mapping[mapping.size() - 1] = mapped;
        }
        if (dynamic) {
            auto guid = field(*definition, "Guid").trimmed();
            if (!guid.isEmpty()) {
                const auto parsed = QUuid(guid);
                if (parsed.isNull())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"잘못된 XML 기업 필드 GUID"});
                guid = parsed.toString(QUuid::WithoutBraces);
                if (!declaredGuids.insert(str(guid)).second)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 XML 기업 필드 GUID"});
            }
            imported.xmlIdentity = XmlFieldIdentity{id, str(guid), str(fieldName)};
            customFields(p, type->first).back().xmlIdentity = imported.xmlIdentity;
            auto entry = mapping.last().toObject();
            entry["xmlGuid"] = guid;
            entry["xmlFieldName"] = fieldName;
            entry["representation"] = "hex-element";
            mapping[mapping.size() - 1] = entry;
        }
        if (child(*definition, "CalculationType") || child(*definition, "RollupType")) {
            auto &native = customFields(p, type->first).back();
            native.xmlRollUp = XmlRollUp{int(num(*definition, "CalculationType", 0, 2, "0")),
                                         int(num(*definition, "RollupType", 0, 7, "0"))};
            if (native.xmlRollUp->calculationType == 1 && type->first != FieldScope::Task &&
                type->first != FieldScope::Resource)
                diagnostic(diagnostics, definition->path, "작업·자원 외 범위의 요약 규칙은 실행하지 않고 보존합니다.");
            if (native.xmlRollUp->calculationType == 2 && !child(*definition, "Formula"))
                diagnostic(diagnostics, definition->path,
                           "요약 외부 계산식은 실행하지 않고 명시 요약 값을 보존합니다.");
        }
        auto alias = [&](const std::string &name) {
            if (!name.empty())
                formulaAliases.push_back({name, "field:" + imported.nativeId, type->first});
        };
        alias(str(fieldName));
        alias(xmlSlotName(id));
        if (child(*definition, "Formula") && !field(*definition, "Formula").trimmed().isEmpty())
            customFields(p, type->first).back().xmlFormula = XmlFormula{str(field(*definition, "Formula")), {}, false};
        const bool hasAuto = child(*definition, "AutoRollDown") != nullptr;
        const bool automatic = hasAuto && flag(*definition, "AutoRollDown");
        std::string secondaryId;
        if (child(*definition, "SecondaryPID")) {
            const auto secondary = str(QString::number(num(*definition, "SecondaryPID", 1, 2147483647)));
            if (xmlDynamicUdfScope(secondary) == FieldScope::Assignment) {
                secondaryFields.push_back({secondary, imported.name, imported.type, imported.lookup});
                secondaryId = secondary;
            } else {
                if (automatic)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                   {"자동 roll-down의 보조 배정 필드 ID를 지원하지 않습니다."});
                diagnostic(diagnostics, definition->path + "/SecondaryPID", "미지원 보조 필드 ID · 원본 XML에 보존");
            }
        }
        if (hasAuto || !secondaryId.empty())
            rollDownInputs.push_back({type->first, imported.nativeId, secondaryId, automatic});
        udf.emplace(id, std::move(imported));
    }
    for (const auto &[guid, input] : lookupTables) {
        if (input.fieldId.empty() || declaredUdf.contains(input.fieldId))
            continue;
        const auto type = xmlUdfType(input.fieldId);
        if (!input.supported || !type || (!xmlOutlineUdf(input.fieldId) && !dateTimeCustomField(type->second))) {
            declaredUdf.insert(input.fieldId);
            diagnostic(diagnostics, "/Project/OutlineCodes", "미지원 목록 필드 ID/형식 · 원본 XML에 보존");
            continue;
        }
        auto lookup = input.lookup;
        if (xmlOutlineUdf(input.fieldId) && lookup.valueType != CustomFieldType::Text)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Outline Code는 텍스트 목록이어야 합니다."});
        if (dateTimeCustomField(type->second)) {
            if (!dateTimeCustomField(lookup.valueType))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"필드와 날짜 목록 형식이 다릅니다."});
            lookup.valueType = type->second;
            for (auto &v : lookup.values)
                v.value = normalizeLookupScalar(
                    type->second,
                    type->second != CustomFieldType::Date && v.value.size() == 10 ? v.value + "T00:00:00" : v.value,
                    p.currency);
        }
        auto imported = importUdfField(p, true, type->first, input.fieldId, input.lookup.tableName,
                                       CustomFieldType::Choice, mapping);
        auto &native = customFields(p, type->first).back();
        native.xmlLookup = lookup;
        optionLabels(native);
        imported.lookup = lookup;
        declaredUdf.insert(input.fieldId);
        udf.emplace(input.fieldId, std::move(imported));
    }
    std::set<std::string> seenSecondary;
    for (const auto &secondary : secondaryFields) {
        if (!seenSecondary.insert(secondary.id).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 XML SecondaryPID"});
        if (declaredUdf.contains(secondary.id)) {
            if (udf.contains(secondary.id) &&
                (udf.at(secondary.id).scope != FieldScope::Assignment || udf.at(secondary.id).type != secondary.type))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML SecondaryPID 형식/범위 불일치"});
            continue;
        }
        declaredUdf.insert(secondary.id);
        auto imported =
            importUdfField(p, true, FieldScope::Assignment, secondary.id, secondary.name, secondary.type, mapping);
        imported.xmlIdentity = XmlFieldIdentity{secondary.id, {}, secondary.name};
        p.assignmentFields.back().xmlIdentity = imported.xmlIdentity;
        if (secondary.lookup) {
            auto lookup = *secondary.lookup;
            lookup.fieldGuid.clear();
            p.assignmentFields.back().xmlLookup = lookup;
            optionLabels(p.assignmentFields.back());
            imported.lookup = lookup;
        }
        udf.emplace(secondary.id, std::move(imported));
        diagnostic(diagnostics, "/Project/ExtendedAttributes/SecondaryPID",
                   "보조 배정 필드는 독립 배정 정의로 보존·재내보내기합니다.");
    }
    for (const auto &input : rollDownInputs) {
        auto &fields = customFields(p, input.scope);
        auto source = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.id == input.sourceId; });
        const auto target = input.secondary.empty()         ? std::string{}
                            : udf.contains(input.secondary) ? udf.at(input.secondary).nativeId
                                                            : std::string{};
        if (!input.secondary.empty() && target.empty())
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Roll-down 배정 필드 정의가 없습니다."});
        source->xmlRollDown = XmlRollDown{target, input.automatic};
    }
    std::map<std::string, std::optional<std::string>> observedProjectDates;
    auto projectDate = [&](const ExternalField &f, std::optional<std::string> v) {
        if (f.scope != FieldScope::Project || !dateTimeCustomField(f.lookup ? f.lookup->valueType : f.type))
            return;
        auto [it, inserted] = observedProjectDates.emplace(f.externalId, v);
        if (!inserted && it->second != v)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"프로젝트 날짜의 미설정/값 표현이 서로 다릅니다."});
    };
    auto missingDate = [](const QString &s) { return s.trimmed().isEmpty() || s.trimmed() == "NA"; };
    std::size_t udfCells = 0;
    auto readUdf = [&](const Node &owner, FieldScope scope, std::vector<TaskAttribute> &values,
                       std::vector<std::string> *explicitMissing = nullptr) {
        std::set<std::string> seen, missingLookups;
        std::size_t enterpriseEntries = 0;
        std::map<std::string, std::map<std::string, std::vector<std::string>>> selections;
        auto resolve = [&](const ExternalField &f, const std::string &refId, const std::string &refGuid,
                           const std::optional<QString> &raw) {
            auto &dictionary = customFields(p, f.scope);
            auto &native =
                *std::find_if(dictionary.begin(), dictionary.end(), [&](const auto &v) { return v.id == f.nativeId; });
            auto &l = *native.xmlLookup;
            std::optional<std::string> chosen;
            auto accept = [&](const XmlLookupValue &v) {
                if (chosen && *chosen != v.optionId)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 값의 ID/GUID가 서로 다릅니다."});
                chosen = v.optionId;
            };
            if (!refId.empty()) {
                const auto v = std::find_if(l.values.begin(), l.values.end(),
                                            [&](const auto &v) { return v.listed && v.valueId == refId; });
                if (v == l.values.end())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"다른 목록/미정의 값 ID 참조"});
                accept(*v);
            }
            if (!refGuid.empty()) {
                const auto v = std::find_if(l.values.begin(), l.values.end(),
                                            [&](const auto &v) { return v.listed && v.guid == refGuid; });
                if (v == l.values.end())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"다른 목록/미정의 값 GUID 참조"});
                accept(*v);
            }
            std::optional<std::string> scalar;
            if (raw)
                scalar = lookupScalar(l.valueType, *raw);
            if (chosen) {
                const auto &v = *std::find_if(l.values.begin(), l.values.end(),
                                              [&](const auto &v) { return v.optionId == *chosen; });
                if (scalar && *scalar != v.value)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 참조와 직접 값이 다릅니다."});
                return *chosen;
            }
            if (!scalar)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 값/참조가 없습니다."});
            for (const auto &v : l.values)
                if (v.value == *scalar) {
                    if (chosen)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                       {"직접 값에 대응하는 목록 항목이 모호합니다."});
                    chosen = v.optionId;
                }
            if (chosen)
                return *chosen;
            if (l.restrictValues || l.values.size() >= 1000)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 밖 직접 값 또는 목록 한도 초과"});
            const auto guid = QUuid::createUuidV5(QUuid("{0b63e8b5-9b9c-4d4c-af0a-06e430b5387a}"),
                                                  qs(f.nativeId + ":" + *scalar).toUtf8());
            const auto option = "xml-free-" + guid.toString(QUuid::WithoutBraces).toStdString();
            l.values.push_back({option, {}, {}, *scalar, {}, {}, false});
            optionLabels(native);
            return option;
        };
        // Every scalar XML child reaches this probe; reuse compilation per thread.
        thread_local const QRegularExpression hexFieldId("^[bcf][0-9a-f]{6}$");
        for (const auto &entry : owner.children) {
            std::string id;
            const bool extended = entry.name == "ExtendedAttribute", outline = entry.name == "OutlineCode",
                       enterprise = entry.name == "EnterpriseExtendedAttribute";
            const bool hex = !extended && !outline && !enterprise && hexFieldId.match(entry.name).hasMatch();
            if (enterprise) {
                if (++enterpriseEntries > 450)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"객체별 기업 참조 요소는 450개까지입니다."});
                const auto fieldHex = field(entry, "FieldIDInHex").trimmed();
                if (!fieldHex.isEmpty()) {
                    bool ok = false;
                    const auto number = fieldHex.toUInt(&ok, 16);
                    if (!ok || number > 2147483647 || !number)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"잘못된 기업 필드 16진 ID"});
                    id = std::to_string(number);
                } else
                    id = str(QString::number(num(entry, "FieldID", 1, 2147483647)));
            } else if (extended || outline)
                id = str(QString::number(num(entry, "FieldID", 1, 2147483647)));
            else if (hex) {
                bool ok = false;
                id = std::to_string(entry.name.toUInt(&ok, 16));
                if (!ok || !xmlDynamicUdfScope(id))
                    continue;
            } else
                continue;
            calculationCheckpoint();
            entry.used = true;
            if (++udfCells > 100000)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML UDF 참조 한도 초과"});
            if (!declaredUdf.contains(id))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"정의 없는 XML FieldID: " + id});
            if (!udf.contains(id))
                continue;
            const auto &f = udf.at(id);
            if (f.scope != scope && !(scope == FieldScope::Task && f.scope == FieldScope::Project))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML UDF 범위 불일치"});
            const auto *definition = customField(p, "field:" + f.nativeId, f.scope);
            if (explicitMissing) {
                const bool ref = child(entry, "ValueGUID") || child(entry, "FieldValueGUID") ||
                                 child(entry, "ValueID") ||
                                 (enterprise && child(entry, "FieldID") &&
                                  str(QString::number(num(entry, "FieldID", 0, 2147483647))) != id);
                const auto raw = hex ? entry.text : field(entry, "Value");
                const auto kind = definition->xmlLookup ? definition->xmlLookup->valueType : definition->type;
                const bool absent = !hex && !child(entry, "Value");
                if (!ref && (absent || (dateTimeCustomField(kind) && missingDate(raw)) ||
                             (kind != CustomFieldType::Text && raw.trimmed().isEmpty()) ||
                             (hex && definition->xmlLookup && !definition->xmlLookup->tableGuid.empty() &&
                              raw.trimmed().isEmpty()))) {
                    if (!seen.insert(id).second || selections.contains(id) || !missingLookups.insert(id).second)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"배정의 명시 미설정/값 표현 충돌"});
                    explicitMissing->push_back("field:" + f.nativeId);
                    diagnostic(diagnostics, entry.path, "배정 명시 미설정 · 자동 roll-down보다 우선");
                    continue;
                }
            }
            if (definition->xmlLookup) {
                const bool hasRef = child(entry, "ValueGUID") || child(entry, "FieldValueGUID") ||
                                    child(entry, "ValueID") ||
                                    (enterprise && child(entry, "FieldID") &&
                                     str(QString::number(num(entry, "FieldID", 0, 2147483647))) != id);
                const auto raw = hex ? entry.text : field(entry, "Value");
                if (dateTimeCustomField(definition->xmlLookup->valueType) && !hasRef && missingDate(raw)) {
                    if (selections.contains(id) || !missingLookups.insert(id).second)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                       {"날짜 목록의 미설정/선택 표현이 충돌합니다."});
                    projectDate(f, std::nullopt);
                    diagnostic(diagnostics, entry.path, "NA/빈 날짜 선택은 미설정으로 보존");
                    continue;
                }
                if (missingLookups.contains(id))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"날짜 목록의 미설정/선택 표현이 충돌합니다."});
                if (child(entry, "DurationFormat")) {
                    const auto fmt = num(entry, "DurationFormat", 3, 53);
                    if (definition->xmlLookup->valueType != CustomFieldType::Duration || fmt >= 32 || fmt % 2 == 0 ||
                        fmt > 11)
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 기간은 일반 시간 단위만 지원합니다."});
                }
                auto &channel = selections[id][str(entry.name)];
                auto add = [&](const std::string &refId, const std::string &refGuid,
                               const std::optional<QString> &raw) {
                    const auto option = resolve(f, refId, refGuid, raw);
                    if (std::find(channel.begin(), channel.end(), option) != channel.end())
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"같은 목록 항목의 중복 참조"});
                    channel.push_back(option);
                };
                if (hex) {
                    if (!entry.children.empty())
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 GUID 값에 하위 요소가 있습니다."});
                    if (!definition->xmlLookup->tableGuid.empty()) {
                        const auto refs = entry.text.split(QRegularExpression("[;,\\s]+"), Qt::SkipEmptyParts);
                        if (refs.empty())
                            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"빈 목록 GUID 참조"});
                        for (const auto &ref : refs) {
                            if (++udfCells > 100000)
                                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 참조 한도 초과"});
                            add({}, guidText(ref), {});
                        }
                    } else
                        add({}, {}, entry.text);
                } else {
                    std::string refId, refGuid;
                    if (child(entry, "ValueID"))
                        refId = str(QString::number(num(entry, "ValueID", 0, 2147483647)));
                    if (child(entry, "ValueGUID"))
                        refGuid = guidText(field(entry, "ValueGUID"));
                    if (child(entry, "FieldValueGUID")) {
                        const auto other = guidText(field(entry, "FieldValueGUID"));
                        if (!refGuid.empty() && refGuid != other)
                            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"값 GUID 참조 불일치"});
                        refGuid = other;
                    }
                    if (enterprise && child(entry, "FieldID")) {
                        const auto other = str(QString::number(num(entry, "FieldID", 0, 2147483647)));
                        if (other != id) {
                            if (!refId.empty() && refId != other)
                                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기업 목록 값 ID 불일치"});
                            refId = other;
                        }
                    }
                    add(refId, refGuid,
                        child(entry, "Value") ? std::optional<QString>{field(entry, "Value")} : std::nullopt);
                }
                continue;
            }
            if (!seen.insert(id).second)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 XML UDF 값"});
            if (outline || child(entry, "FieldValueGUID") ||
                ((extended || enterprise) && (child(entry, "ValueGUID") || child(entry, "ValueID"))))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"목록 정의 없는 값 참조"});
            if ((extended || enterprise) && !child(entry, "Value")) {
                diagnostic(diagnostics, entry.path, "직접 값 없는 필드 · 값 생략");
                continue;
            }
            auto value = (extended || enterprise) ? field(entry, "Value") : entry.text;
            if (!extended && !enterprise && !entry.children.empty())
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML UDF 스칼라 하위 요소"});
            if (enterprise && child(entry, "FieldID") &&
                str(QString::number(num(entry, "FieldID", 1, 2147483647))) != id)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기업 스칼라 필드 ID가 다릅니다."});
            const auto durationFormat =
                int(num(entry, "DurationFormat", 3, 53,
                        f.type == CustomFieldType::Duration ? field(r, "DurationFormat", "3") : QString("3")));
            if (f.type == CustomFieldType::Duration && durationFormat != 3 && durationFormat != 4 &&
                durationFormat != 35 && durationFormat != 36)
                diagnostic(diagnostics, entry.path + "/DurationFormat",
                           "기간 표시 단위를 분으로 정규화 · 경과 시간/추정 여부는 보존");
            if (f.type != CustomFieldType::Duration && child(entry, "DurationFormat"))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기간 외 필드에 DurationFormat이 있습니다."});
            if (dateTimeCustomField(f.type) && missingDate(value)) {
                projectDate(f, std::nullopt);
                diagnostic(diagnostics, entry.path, "NA/빈 날짜는 미설정으로 보존");
                continue;
            }
            value = qs(lookupScalar(f.type, value, false, durationFormat));
            projectDate(f, str(value));
            if (f.scope == FieldScope::Project) {
                const auto normalized =
                    normalizeAttributeValue(p, "field:" + f.nativeId, str(value), FieldScope::Project);
                const auto existing = attributeValue(p.projectAttributes, "field:" + f.nativeId);
                if (existing && *existing != normalized)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"작업별 XML 프로젝트 필드 값 충돌"});
                if (!existing)
                    importUdfValue(p, f, p.projectAttributes, normalized);
            } else
                importUdfValue(p, f, values, str(value));
        }
        for (auto &[id, channels] : selections) {
            const auto &f = udf.at(id);
            const auto &definition = *customField(p, "field:" + f.nativeId, f.scope);
            std::optional<std::string> value;
            for (const auto &[kind, selected] : channels) {
                const auto current = choiceValue(definition, selected);
                if (value && *value != current)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"동일 필드의 XML 참조 표현이 서로 다릅니다."});
                value = current;
            }
            projectDate(f, value);
            auto &target = f.scope == FieldScope::Project ? p.projectAttributes : values;
            const auto existing = attributeValue(target, "field:" + f.nativeId);
            if (existing && existing != value)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"프로젝트 목록 선택값 충돌"});
            if (!existing)
                importUdfValue(p, f, target, *value);
        }
    };
    readUdf(r, FieldScope::Project, p.projectAttributes);
    for (const auto *node : list(r, "Tasks", "Task")) {
        const auto id = uid(*node);
        if (taskIds.contains(id)) {
            auto task =
                std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == taskIds.at(id); });
            readUdf(*node, FieldScope::Task, task->attributes);
        } else {
            std::vector<TaskAttribute> summaryValues;
            readUdf(*node, FieldScope::Task, summaryValues);
            if (id == "0")
                p.taskSummaryAttributes = std::move(summaryValues);
            else {
                auto group = std::find_if(p.wbsNodes.begin(), p.wbsNodes.end(),
                                          [&](const auto &g) { return g.id == "xml-wbs-" + str(id); });
                if (group != p.wbsNodes.end())
                    group->attributes = std::move(summaryValues);
                else if (!summaryValues.empty())
                    diagnostic(diagnostics, node->path, "소유자 없는 요약 값 · 원본 XML에 보존");
            }
        }
    }
    for (const auto *node : list(r, "Resources", "Resource")) {
        const auto id = uid(*node);
        if (!resources.contains(id))
            continue;
        auto resource = std::find_if(p.resources.begin(), p.resources.end(),
                                     [&](const auto &x) { return x.id == resources.at(id); });
        readUdf(*node, FieldScope::Resource, resource->attributes);
    }
    std::size_t assignmentIndex = 0;
    for (const auto *node : list(r, "Assignments", "Assignment")) {
        auto &a = p.assignments.at(assignmentIndex++);
        readUdf(*node, FieldScope::Assignment, a.attributes, &a.rollDownMissing);
    }
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(p, scope))
            if (f.xmlFormula) {
                const auto source = f.xmlFormula->source;
                f.xmlFormula = bindXmlFormula(p, scope, source, formulaAliases);
                const auto issue = xmlFormulaProblem(p, scope, "field:" + f.id);
                if (issue.empty() && f.type != CustomFieldType::Choice)
                    f.xmlFormula->enabled = true;
                else
                    diagnostic(
                        diagnostics, "/ExtendedAttribute/" + qs(f.id) + "/Formula",
                        qs("실행하지 않은 XML 계산식 · 스냅샷 보존: " + (issue.empty() ? "목록 결과 미지원" : issue)));
            }
    try {
        validateXmlFormulas(p);
    } catch (const std::exception &e) {
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            for (auto &f : customFields(p, scope))
                if (f.xmlFormula)
                    f.xmlFormula->enabled = false;
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   qs(std::string("계산식 의존성 검증 실패 · 모든 XML 식을 비활성 보존: ") + e.what()));
    }
    if (availabilityIncomplete && hasXmlAvailabilityFunctions(p)) {
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            for (auto &f : customFields(p, scope))
                if (f.xmlFormula && xmlAvailabilityFormula(*f.xmlFormula))
                    f.xmlFormula->enabled = false;
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "가용기간 전체를 정확히 읽지 못해 PPMUnitAvailability 식을 비활성 보존했습니다. 정적 값과 원본 "
                   "XML을 확인하세요.");
    }
    if (hasXmlUnitReferences(p) || hasXmlAvailabilityFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Units/Max Units는 기본 배정률/가용률의 1=100% 수치입니다. PPMUnitAvailability는 명시 날짜의 예외 "
                   "또는 기본 가용률을 조회하며 달력 근무시간·현재 부하는 포함하지 않습니다.");
    if (hasXmlProjectConversionFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "ProjDateValue/ProjDateConv/ProjDurConv는명시 ISO·고정 en-US 날짜 표시와 기간 단위를 사용합니다. "
                   "NA는미설정이며 PC 현재 시각/로캘/근무시간 옵션은추측하지 않습니다.");
    if (hasXmlConversionFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "CDec/CSng/CVar는native 소수6자리 범위를 사용합니다. CSng는단정밀도 짝수 반올림 후 "
                   "소수6자리로 변환하며 CVar는원래 형식/기간 의미를 보존합니다. 전체 Decimal/Variant 형식은아닙니다.");
    if (hasXmlStringFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Replace/InStrRev/StrReverse는BMP 문자 위치와 비교0 또는ASCII 비교1을 사용합니다. "
                   "Replace는시작 이후 문자열을 반환하며 Replace/StrReverse의Null은오류입니다.");
    if (hasXmlMathFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Atn/Cos/Exp/Log/Sin/Tan은라디안·자연로그와 부동소수 근사 계산을 사용합니다. "
                   "결과는소수6자리이며 Log는양수만 허용하고 범위를 넘는 결과는오류입니다.");
    if (hasXmlNumberTextFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Str/Val/Hex/Oct는 고정 점 소수·숫자 접두부와32비트 진수 규칙을 사용합니다. "
                   "Val은 소수6자리 정수 계산으로 반올림하며 현재 PC 로캘/형식 전체를 추측하지 않습니다.");
    if (hasXmlTimeFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "TimeSerial/TimeValue는1899-12-30 기준 Date 값을 반환합니다. 정수 넘침·음수 OLE 규칙과 "
            "고정 24시간/영문 AM·PM/ISO 입력을 지원하며 소수 초는6자리까지 보존합니다. 현재 날짜를 붙이지 않습니다.");
    if (hasXmlExtendedDateFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "DatePart/Weekday·확장 DateAdd/DateDiff는 Gregorian 민간 시각과 고정 주 규칙을 사용합니다. "
                   "월말 보정·달력 경계/온전한 주를 계산하며 근무일이나 현재 PC 설정을 추측하지 않습니다.");
    if (hasXmlFormattingFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "Format은 고정 en-US, PPMFormat/StrConv는 명시 LCID1033/1041/1042·Unicode16.0 지원 부분을 사용합니다. "
            "결과는 문자열이며 PC 로캘을 사용하지 않습니다. 외부 제품의 전체 표시 형식·NLS 계산 동등성은 "
            "미검증입니다.");
    if (hasXmlTimeZoneFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "PPM 시간대 확장 식을 보존합니다. 명시 오프셋/고정 IANA2026f·중복 시각 정책을 사용하며 PC 기본 시간대는 "
            "사용하지 않습니다. 날짜 결과는 시간대 표지가 없는 민간 시각이고 오프셋은 PPMTimeZoneText로 보존합니다.");
    if (hasXmlAdditionalFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "추가 함수의 지원 부분을 실행합니다. 문자열 비교/문자 코드는 명시 ASCII/BMP 범위, DateValue는 ISO, "
                   "CByte/CCur와 Sqr는 native 정밀도 제한을 따릅니다. 외부 제품/로캘의 계산 동등성은 미검증입니다.");
    if (hasXmlCalendarFunctions(p)) {
        p.xmlFormulaCalendarId = "xml-cal-" + str(projectCalendar);
        diagnostic(diagnostics, "/Project/CalendarUID",
                   "XML 프로젝트 달력을 프로젝트·요약 계산식의 기본 달력으로 보존합니다. 작업/자원은 명시 배정 달력을 "
                   "사용합니다.");
    }
    if (hasXmlActualReferences(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "실적 참조 식은 보존합니다. XML 실적/진척을 실적 원장에 자동 채택하지 않으므로 상태일 기준 실적 "
                   "보고가 필요합니다.");
    if (hasXmlRemainingReferences(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "잔여 참조 식은 보존합니다. XML 상태일·진척 원장을 자동 채택하지 않아 native 원본의 잔여 예측과 "
                   "달라질 수 있습니다.");
    extendRollDownLookups(p);
    for (auto &input : curveInputs) {
        calculationCheckpoint();
        if (input.rows.empty()) {
            if (input.code == 8) {
                diagnostic(diagnostics, "/Assignment/" + qs(input.externalId),
                           "수동 곡선에 계획 시간대별 공수가 없어 원본 XML에만 보존");
                continue;
            }
            p = applyNativeCurve(p, input.task, input.resource, mspWorkContour(input.code), input.work);
        } else {
            const auto plan = calculatePlan(p);
            if (plan.status != CalculationStatus::Valid)
                throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
            const auto &row = *std::find_if(plan.tasks.begin(), plan.tasks.end(),
                                            [&](const auto &v) { return v.taskId == input.task; });
            const auto &task =
                *std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == input.task; });
            WorkCalendar cal(p, task);
            const auto origin = cal.offset(input.origin.empty() ? row.start : input.origin);
            const auto duration = TaskExecution(p, task).duration(row);
            std::vector<PlannedWorkAmount> amounts;
            std::int64_t last = 0, total = 0;
            for (const auto &v : input.rows) {
                const auto from = cal.offset(v.start) - origin, to = cal.offset(v.finish) - origin;
                if (from < last || to <= from || to > duration || v.work > maxWork - total)
                    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                                   {"XML 계획 시간대의 순서·기간·공수 한도를 확인하세요."});
                if (from > last)
                    amounts.push_back({last, from, 0});
                amounts.push_back({from, to, v.work});
                last = to;
                total += v.work;
            }
            if (last < duration)
                amounts.push_back({last, duration, 0});
            if (input.work && total != *input.work)
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"XML Work와 계획 시간대별 합계가 다릅니다."});
            p = materializeResourceWork(p, input.task, input.resource, amounts);
            auto a = std::find_if(p.assignments.begin(), p.assignments.end(), [&](const auto &v) {
                return v.taskId == input.task && v.resourceId == input.resource;
            });
            NativeCurve c;
            if (input.code > 0 && input.code < 8)
                c = mspWorkContour(input.code);
            else {
                c.source = "mspdi";
                c.mspCode = 8;
                c.name = "MS Project timephased work";
            }
            c.externalId = input.externalId;
            c.durationMinutes = duration;
            c.workUnitMinutes = total;
            c.baseUnits = a->units;
            c.projectedPeriods = a->periods;
            a->nativeCurve = std::move(c);
        }
        diagnostic(diagnostics, "/Assignment/" + qs(input.externalId) + "/WorkContour",
                   "외부 계획 곡선과 정수 배정 투영 보존 · 실제공수/원가로 채택하지 않음");
    }
    std::vector<XmlProgressCandidate> progressCandidates;
    std::optional<std::string> progressStatus;
    std::string progressStatusIssue;
    auto status = field(r, "StatusDate");
    if (!status.isEmpty() && status != "NA") {
        try {
            progressStatus = str(stamp(status, "/Project/StatusDate"));
        } catch (const AppError &) {
            progressStatusIssue = "XML StatusDate가 분 정밀도 civil 일시가 아닙니다. 채택 상태일을 명시하세요.";
        }
    } else
        progressStatusIssue = "XML StatusDate가 없습니다. 채택 상태일을 명시하세요.";
    for (const auto &[node, index] : taskNodes) {
        calculationCheckpoint();
        XmlProgressCandidate c;
        c.xmlUid = str(uid(*node));
        c.taskId = p.tasks[index].id;
        c.name = p.tasks[index].name;
        try {
            auto actualStart = field(*node, "ActualStart"), actualFinish = field(*node, "ActualFinish"),
                 actualDuration = field(*node, "ActualDuration"), remaining = field(*node, "RemainingDuration");
            c.present =
                !actualStart.isEmpty() || !actualFinish.isEmpty() || !actualDuration.isEmpty() || !remaining.isEmpty();
            if (!actualStart.isEmpty() && actualStart != "NA")
                c.actualStart = str(stamp(actualStart, node->path + "/ActualStart"));
            if (!actualFinish.isEmpty() && actualFinish != "NA")
                c.actualFinish = str(stamp(actualFinish, node->path + "/ActualFinish"));
            if (!actualDuration.isEmpty())
                c.actualMinutes = duration(actualDuration, node->path + "/ActualDuration");
            if (!remaining.isEmpty())
                c.remainingMinutes = duration(remaining, node->path + "/RemainingDuration");
            if (c.present && (!c.actualMinutes || !c.remainingMinutes))
                c.issue =
                    "ActualDuration과 RemainingDuration의 명시 값이 모두 필요합니다. 날짜/진척률로 추정하지 않습니다.";
            if (!c.present)
                c.issue = "작업 진척 값 없음";
        } catch (const AppError &) {
            c.present = true;
            c.issue = "XML 실제 날짜/기간 형식·중복·분 정밀도를 확인하세요.";
        }
        // This quantity is an explicit forecast input for a mapped native work-driven task.
        // Keep its error separate so duration-only adoption retains its existing contract.
        try {
            const auto remainingWork = field(*node, "RemainingWork");
            if (!remainingWork.isEmpty())
                c.remainingWorkUnitMinutes = curveWorkDuration(remainingWork);
        } catch (const AppError &) {
            c.remainingWorkIssue = "RemainingWork의 정확한 비음수 공수 단위(100u=1자원·분)를 확인하세요.";
        }
        progressCandidates.push_back(std::move(c));
    }
    if (std::any_of(progressCandidates.begin(), progressCandidates.end(), [](const auto &c) { return c.present; }))
        diagnostic(diagnostics, "/Project/Tasks",
                   "작업 진척 후보를 읽었습니다. 기본 계획 변환은 미채택이며 별도 검토로 새 사본에만 명시 채택합니다. "
                   "배정/기간 실제 공수·원가·진척률은 채택하지 않습니다.");
    auto periodCandidates = readPeriodCandidates(r, p, diagnostics);
    auto actualIntervals = readActualIntervals(r, p);
    // A supported daily record must not conceal an overlapping multi-day record of the same measure.
    using IntervalKey = std::pair<std::string, int>;
    std::map<IntervalKey, std::vector<const XmlActualInterval *>> multi;
    for (const auto &v : actualIntervals)
        if (v.multipleDays && validStamp(v.start) && validStamp(v.finish) && v.start < v.finish)
            multi[{v.assignmentUid, v.type}].push_back(&v);
    std::map<IntervalKey, std::vector<std::string>> ends;
    for (auto &[key, values] : multi) {
        std::sort(values.begin(), values.end(), [](auto a, auto b) { return a->start < b->start; });
        std::string end;
        for (const auto *v : values) {
            end = std::max(end, v->finish);
            ends[key].push_back(end);
        }
    }
    std::map<std::pair<std::string, std::string>, XmlPeriodCandidate *> daily;
    for (auto &v : periodCandidates)
        daily[{v.assignmentUid, v.date}] = &v;
    for (const auto &v : actualIntervals)
        if (!v.multipleDays && validStamp(v.start) && validStamp(v.finish)) {
            const IntervalKey key{v.assignmentUid, v.type};
            if (!multi.contains(key))
                continue;
            const auto &values = multi.at(key);
            const auto end = std::lower_bound(values.begin(), values.end(), v.finish,
                                              [](auto r, const auto &s) { return r->start < s; });
            if (end != values.begin() && ends.at(key)[std::size_t(end - values.begin() - 1)] > v.start &&
                daily.contains({v.assignmentUid, v.start.substr(0, 10)})) {
                auto &row = *daily.at({v.assignmentUid, v.start.substr(0, 10)});
                row.issue = "다일 구간과 같은 종류의 일별 구간이 겹칩니다. 부분 채택하지 않습니다.";
                row.workUnitMinutes.reset();
                row.actualMinor.reset();
            }
        }
    unconverted(r, diagnostics);
    validate(p);
    graph(p);
    auto report = baseReport(p, true);
    report["source"] = QJsonObject{{"name", QFileInfo(sourceName).fileName()},
                                   {"sha256", hash(bytes)},
                                   {"producer", "unverified"},
                                   {"saveVersion", saveVersion},
                                   {"buildNumber", build}};
    report["mapping"] = mapping;
    report["diagnostics"] = diagnostics;
    const auto multiCount =
        std::count_if(actualIntervals.begin(), actualIntervals.end(), [](const auto &v) { return v.multipleDays; });
    if (multiCount)
        report["actualIntervalReview"] = QJsonObject{{"multiDayIntervals", int(multiCount)},
                                                     {"defaultAllocated", false},
                                                     {"policy", "explicit-range-total-confirmation-required"}};
    if (std::any_of(progressCandidates.begin(), progressCandidates.end(),
                    [](const auto &row) { return row.present; })) {
        QJsonArray rows;
        for (const auto &row : progressCandidates)
            rows.append(QJsonObject{
                {"xmlUid", qs(row.xmlUid)},
                {"taskId", qs(row.taskId)},
                {"name", qs(row.name)},
                {"present", row.present},
                {"issue", qs(row.issue)},
                {"actualStart", row.actualStart ? QJsonValue(qs(*row.actualStart)) : QJsonValue(QJsonValue::Null)},
                {"actualFinish", row.actualFinish ? QJsonValue(qs(*row.actualFinish)) : QJsonValue(QJsonValue::Null)},
                {"actualMinutes",
                 row.actualMinutes ? QJsonValue(double(*row.actualMinutes)) : QJsonValue(QJsonValue::Null)},
                {"remainingMinutes",
                 row.remainingMinutes ? QJsonValue(double(*row.remainingMinutes)) : QJsonValue(QJsonValue::Null)},
                {"remainingWorkUnitMinutes", row.remainingWorkUnitMinutes
                                                 ? QJsonValue(double(*row.remainingWorkUnitMinutes))
                                                 : QJsonValue(QJsonValue::Null)},
                {"remainingWorkIssue", qs(row.remainingWorkIssue)}});
        report["taskProgressReview"] = QJsonObject{
            {"rows", rows},
            {"xmlStatusAt", progressStatus ? QJsonValue(qs(*progressStatus)) : QJsonValue(QJsonValue::Null)},
            {"statusIssue", qs(progressStatusIssue)},
            {"defaultAdopted", false}};
    }
    ProjectXmlExchange exchange{std::move(p), bytes, report, true};
    exchange.progressCandidates = std::move(progressCandidates);
    exchange.progressStatusAt = std::move(progressStatus);
    exchange.progressStatusIssue = std::move(progressStatusIssue);
    exchange.periodCandidates = std::move(periodCandidates);
    exchange.actualIntervals = std::move(actualIntervals);
    exchange.actualCurrency = str(field(r, "CurrencyCode").trimmed());
    exchange.baselineCandidates = readXmlBaselines(bytes, exchange.project, mapping);
    if (!exchange.baselineCandidates.empty()) {
        QJsonArray rows;
        for (const auto &c : exchange.baselineCandidates)
            rows.append(QJsonObject{{"number", c.row.number},
                                    {"kind", qs(c.row.kind)},
                                    {"xmlUid", qs(c.row.xmlUid)},
                                    {"id", qs(c.row.id)},
                                    {"issue", qs(c.issue)}});
        exchange.report["baselineReview"] = QJsonObject{{"defaultAdopted", false}, {"rows", rows}};
    }
    return exchange;
}

namespace {
QString xmlStamp(const std::string &s) {
    auto v = qs(s);
    v.replace(' ', 'T');
    return v + ":00";
}
QString timeText(int m) {
    return QString("%1:%2:00").arg(m / 60, 2, 10, QChar('0')).arg(m % 60, 2, 10, QChar('0'));
}
QString unitText(int u) {
    return QString::number(u / 100) + "." + QString::number(u % 100).rightJustified(2, '0');
}
QString durationText(std::int64_t n) {
    return "PT" + QString::number(n / 60) + "H" + QString::number(n % 60) + "M0S";
}
} // namespace
static ProjectXmlExchange exportProjectXmlImpl(const Project &p, bool includeActuals) {
    EvaluationScope evaluation;
    validate(p);
    std::optional<XmlActualProjection> actuals;
    if (includeActuals)
        actuals = projectXmlActuals(p);
    ScheduleFormulaScope formulaSchedule(p, nullptr, ScheduleFormulaMode::Plan);
    graph(p);
    if (qs(p.name).size() > 255)
        throw AppError(ErrorCode::FILE_PROJECTXML_070, {});
    for (const auto &t : p.tasks)
        if (qs(t.name).size() > 512)
            throw AppError(ErrorCode::FILE_PROJECTXML_071, {diagnosticArgument(qs(t.id))});
    for (const auto &g : p.wbsNodes)
        if (qs(g.name).size() > 512)
            throw AppError(ErrorCode::FILE_PROJECTXML_072, {diagnosticArgument(qs(g.id))});
    for (const auto &r : p.resources)
        if (qs(r.name).size() > 512)
            throw AppError(ErrorCode::FILE_PROJECTXML_073, {diagnosticArgument(qs(r.id))});
    for (const auto &c : p.calendars)
        if (qs(c.name).size() > 512)
            throw AppError(ErrorCode::FILE_PROJECTXML_074, {diagnosticArgument(qs(c.id))});
    if (p.tasks.size() + p.wbsNodes.size() > 10000 || p.resources.size() > 10000 || p.assignments.size() > 10000 ||
        p.calendars.size() >= 10000)
        throw AppError(ErrorCode::FILE_PROJECTXML_075, {});
    QByteArray bytes;
    QXmlStreamWriter w(&bytes);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("Project");
    w.writeDefaultNamespace(ns);
    auto value = [&](const QString &name, const QString &text) {
        w.writeTextElement(name, text);
        if (bytes.size() > byteLimit)
            throw AppError(ErrorCode::FILE_PROJECTXML_076, {});
    };
    value("SaveVersion", "12");
    value("Name", qs(p.name));
    value("ScheduleFromStart", "1");
    value("StartDate", qs(p.startDate) + "T09:00:00");
    value("CurrencyDigits", QString::number(currencyDecimals(p.currency)));
    value("CurrencyCode", qs(p.currency));
    const auto formulaCalendar = std::find_if(p.calendars.begin(), p.calendars.end(),
                                              [&](const auto &c) { return c.id == p.xmlFormulaCalendarId; });
    value("CalendarUID", p.xmlFormulaCalendarId.empty()
                             ? "1"
                             : QString::number(std::distance(p.calendars.begin(), formulaCalendar) + 2));
    value("DefaultStartTime", "09:00:00");
    value("DefaultFinishTime", "18:00:00");
    value("MinutesPerDay", "480");
    value("MinutesPerWeek", "2400");
    if (actuals) {
        value("EditableActualCosts", "1");
        value("SpreadActualCost", "0");
        value("StatusDate", xmlStamp(*p.statusAt));
    }
    QJsonArray diagnostics, mapping;
    if (actuals)
        diagnostics = actuals->diagnostics;
    std::set<std::string> curveTasks;
    for (const auto &a : p.assignments)
        if (!a.periods.empty() || a.nativeCurve || a.productivityEstimate)
            curveTasks.insert(a.taskId);
    std::map<std::pair<std::string, std::string>, CurveExport> curveExports;
    std::map<std::string, std::int64_t> curveDurations;
    for (const auto &a : p.assignments)
        if (curveTasks.contains(a.taskId)) {
            auto curve = exportResourceCurve(p, a);
            curveDurations[a.taskId] = curve.definition.durationMinutes;
            auto task = *std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == a.taskId; });
            if (!task.breaks.empty()) {
                // XML's duration projection omits native breaks. Keep all emitted
                // timephases inside that projected task's working-minute window.
                task.breaks.clear();
                WorkCalendar projected(p, task);
                curveDurations[a.taskId] = projected.offset(curve.dates.finish) - projected.offset(curve.dates.start);
                diagnostic(diagnostics, "/Task/" + qs(a.taskId) + "/Duration",
                           "수동 중단은 XML 작업 분할로 교환하지 않습니다. 계획 시간대 공수의 전체 날짜 창을 기간에 "
                           "포함하고 원본 중단은 source.ppm에 보존합니다.");
            }
            curveExports.emplace(std::pair{a.taskId, a.resourceId}, std::move(curve));
        }
    diagnostic(diagnostics, "/Project",
               actuals ? "계획과 상태일의 명시 관측 실적 투영입니다. 외부 제품의 계산/재저장 호환은 미검증입니다. 전체 "
                         "입력/이력은 source.ppm 참조."
                       : "기간 기반 계획 투영입니다. 외부 제품의 계산/재저장 호환은 미검증입니다. 원본 전체 "
                         "입력/이력은 source.ppm 참조.");
    if (hasResourcePlan(p))
        diagnostic(diagnostics, "/Project/ResourcePlan",
                   "자재·비용계정·역할은 source.ppm에만 보존 · 연결된 역할의 구체 자원 배정은 기간 기반 배정으로 투영");
    if (!p.activities.empty() || !p.stepReports.empty())
        diagnostic(diagnostics, "/Project/Activities",
                   "Steps·LOE·WBS Summary 정의/보고는 XML에 매핑하지 않습니다. source.ppm에 보존합니다.");
    if (p.printPreferences || !p.networkPositions.empty())
        diagnostic(diagnostics, "/Project/Presentation",
                   "인쇄 설정·수동 네트워크 배치는 교환하지 않습니다. source.ppm에 보존합니다.");
    if (!p.remainingWork.empty())
        diagnostic(diagnostics, "/Project/RemainingTimephasedWork",
                   "날짜별 잔여 공수는 교환하지 않습니다. source.ppm에 보존합니다.");
    if (hasAssignmentPeriods(p) && !actuals)
        diagnostic(diagnostics, "/Project/Assignment/TimephasedData",
                   "기간별 계획 배정은 계획 TimephasedData로 교환합니다. 고유 곡선 정의·산정 근거·전체 이력은 "
                   "source.ppm에 보존합니다.");
    if (hasClassification(p) || !p.savedTaskViews.empty() || !p.savedResourceViews.empty())
        diagnostic(diagnostics, "/Project/ExtendedAttribute",
                   "지원 사용자 필드는 UDF 매핑표를 따릅니다. 작업 코드·저장된 보기는 source.ppm에 전체 "
                   "데이터를 보존합니다.");
    if (p.progressPolicy)
        diagnostic(diagnostics, "/Project/ProgressPolicy",
                   "채택 진척 정책은 교환하지 않습니다. native source.ppm에 설정과 근거를 보존하며 외부 파일은 논리 "
                   "계획입니다.");
    if (p.planLeveling || p.remainingLeveling)
        diagnostic(diagnostics, "/Project/Leveling",
                   "평준화 채택 정책/날짜는 XML에 매핑하지 않습니다. source.ppm에서 보존하며 XML은 논리 계획입니다.");
    if (!p.levelingDecisions.empty())
        diagnostic(diagnostics, "/Project/LevelingDecisions",
                   "평준화 결정 이력은 XML에 매핑하지 않습니다. source.ppm에 원본 이력을 보존합니다.");
    if (!p.periodActuals.empty() && !actuals)
        diagnostic(diagnostics, "/Project/ActualWork",
                   "기간별 실제공수·실제원가 원장은 XML에 매핑하지 않습니다. source.ppm에 전체 이력을 보존합니다.");
    if (p.xerFinancials)
        diagnostic(diagnostics, "/Project/XerFinancials",
                   "보존된 XER 재무기간·누계 스냅샷은 XML에 매핑하지 않습니다. source.ppm에 보존합니다.");
    if (!p.financialPeriodEvents.empty())
        diagnostic(diagnostics, "/Project/FinancialPeriods",
                   "사용자 기간·마감 이력은 XML에 매핑하지 않습니다. source.ppm에 보존합니다.");
    if (!p.externalEvmAdoptions.empty())
        diagnostic(
            diagnostics, "/Project/ExternalEvmAdoptions",
            "외부 XER EV/PV의 native 채택/철회 정책은 XML에 매핑하지 않습니다. source.ppm에 전체 이력을 보존합니다.");
    auto map = [&](const QString &kind, int id, const std::string &target) {
        mapping.append(QJsonObject{{"kind", kind}, {"xmlUid", QString::number(id)}, {"jobsId", qs(target)}});
    };
    const auto udf = exportUdfFields(p, true, diagnostics, mapping);
    const bool externalFormulas = hasXmlFormulas(p);
    if (hasXmlProjectConversionFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "ProjDateValue/ProjDateConv/ProjDurConv는명시 ISO·고정 en-US 날짜 표시와 기간 단위를 사용합니다. "
                   "NA는미설정이며 PC 현재 시각/로캘/근무시간 옵션은추측하지 않습니다.");
    if (hasXmlConversionFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "CDec/CSng/CVar는native 소수6자리 범위를 사용합니다. CSng는단정밀도 짝수 반올림 후 "
                   "소수6자리로 변환하며 CVar는원래 형식/기간 의미를 보존합니다. 전체 Decimal/Variant 형식은아닙니다.");
    if (hasXmlStringFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Replace/InStrRev/StrReverse는BMP 문자 위치와 비교0 또는ASCII 비교1을 사용합니다. "
                   "Replace는시작 이후 문자열을 반환하며 Replace/StrReverse의Null은오류입니다.");
    if (hasXmlMathFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Atn/Cos/Exp/Log/Sin/Tan은라디안·자연로그와 부동소수 근사 계산을 사용합니다. "
                   "결과는소수6자리이며 Log는양수만 허용하고 범위를 넘는 결과는오류입니다.");
    if (hasXmlNumberTextFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Str/Val/Hex/Oct는 고정 점 소수·숫자 접두부와32비트 진수 규칙을 사용합니다. "
                   "Val은 소수6자리 정수 계산으로 반올림하며 현재 PC 로캘/형식 전체를 추측하지 않습니다.");
    if (hasXmlTimeFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "TimeSerial/TimeValue는1899-12-30 기준 Date 값을 반환합니다. 정수 넘침·음수 OLE 규칙과 "
            "고정 24시간/영문 AM·PM/ISO 입력을 지원하며 소수 초는6자리까지 보존합니다. 현재 날짜를 붙이지 않습니다.");
    if (hasXmlExtendedDateFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "DatePart/Weekday·확장 DateAdd/DateDiff는 Gregorian 민간 시각과 고정 주 규칙을 사용합니다. "
                   "월말 보정·달력 경계/온전한 주를 계산하며 근무일이나 현재 PC 설정을 추측하지 않습니다.");
    if (hasXmlFormattingFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "Format은 고정 en-US, PPMFormat/StrConv는 명시 LCID1033/1041/1042·Unicode16.0 지원 부분을 사용합니다. "
            "결과는 문자열이며 PC 로캘을 사용하지 않습니다. 외부 제품의 전체 표시 형식·NLS 계산 동등성은 "
            "미검증입니다.");
    if (hasXmlTimeZoneFunctions(p))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "PPM 시간대 함수는 Project 표준 함수가 아닌 확장입니다. 외부 제품에서 식을 재계산하지 못할 수 있습니다. "
            "일반 날짜 요소에는 시간대가 없으며 오프셋은 Text 결과/식·source.ppm에 보존하세요. 자료는 "
            "IANA2026f입니다.");
    if (hasXmlAdditionalFunctions(p))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "추가 함수의 식과 현재 결과를 출력합니다. Choose의 전체 인수 평가와 ASCII/BMP/ISO·고정점 지원 "
                   "범위는 native 계약을 따르며 전체 VBA/로캘 동등성은 미검증입니다. source.ppm을 함께 보관하세요.");
    if (hasXmlComputedReferences(p, true))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "Start/Finish 일정 참조는 현재 계획을 사용합니다. 상태일 예측은 source.ppm과 별도 보고서에서 확인하세요.");
    if (hasXmlActualReferences(p, true))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Actual Start/Finish는 상태일 기준 실적 원장의 값을 내보냅니다. 실적 원장은 XML로 교환하지 않으며 "
                   "재가져오기 후 실적 참조는 미설정입니다. source.ppm에 원본을 보존합니다.");
    if (hasXmlRemainingReferences(p, true))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "Remaining Duration/Work는 현재 상태일의 잔여 예측을 사용하며 계획 Start/Finish와 구별합니다. XML "
                   "상태일·진척 원장을 자동 채택하지 않으므로 source.ppm에 원본을 보존합니다.");
    if (hasXmlBaselineReferences(p, true))
        diagnostic(diagnostics, "/ExtendedAttributes/Formula",
                   "채택한 외부 번호별 기준선이 있으면 해당 명시 값을, 그 외 번호는 native 승인본을 사용합니다. 표준 "
                   "Baseline 요소로 내보내며 재가져오기 시 별도 검토·채택해야 식이 그 값을 사용합니다. source.ppm에 "
                   "전체 이력을 보존합니다.");
    if (hasXmlEarlyLateReferences(p, true))
        diagnostic(
            diagnostics, "/ExtendedAttributes/Formula",
            "Early/Late는 현재 계획의 배치/검증된 후진 날짜를 내보냅니다. 상태일 예측의 미완료 잔여 구간과 "
            "구별하며 미산정 Late는 미설정입니다. 실제 제품의 논리/자원 순서 의미와 동등성은 검증하지 않았습니다.");
    std::map<std::string, std::string> formulaFieldNames;
    std::set<std::string> occupiedFormulaNames;
    const auto nameKey = [](FieldScope scope, const std::string &key) {
        return std::to_string(int(scope)) + ":" + key;
    };
    const auto fold = [](std::string name) {
        for (char &c : name)
            if (c >= 'A' && c <= 'Z')
                c = char(c + 32);
        return name;
    };
    for (const auto &f : udf)
        if (auto name = xmlSlotName(f.externalId); !name.empty())
            occupiedFormulaNames.insert(nameKey(f.scope, fold(name)));
    if (externalFormulas)
        for (const auto &f : udf) {
            auto name = xmlSlotName(f.externalId);
            if (name.empty())
                name = f.xmlIdentity && !f.xmlIdentity->fieldName.empty() ? f.xmlIdentity->fieldName
                       : f.lookup && !f.lookup->fieldName.empty()         ? f.lookup->fieldName
                                                                          : f.name;
            auto folded = fold(name);
            const bool reserved = !xmlBaselineNamedKey(folded).empty() || folded == "name" || folded == "summary" ||
                                  folded == "duration" || folded == "work" || folded == "project start" ||
                                  std::set<std::string>{"earlystart",
                                                        "earlyfinish",
                                                        "latestart",
                                                        "latefinish",
                                                        "early start",
                                                        "early finish",
                                                        "late start",
                                                        "late finish",
                                                        "baselinestart",
                                                        "baselinefinish",
                                                        "baselineduration",
                                                        "baselinework",
                                                        "baselinecost",
                                                        "baseline cost",
                                                        "task.baselinecost",
                                                        "resource.baselinecost",
                                                        "project.baselinecost",
                                                        "task.baseline cost",
                                                        "resource.baseline cost",
                                                        "project.baseline cost",
                                                        "baseline duration",
                                                        "baseline work",
                                                        "task.baselinestart",
                                                        "task.baselinefinish",
                                                        "task.baselineduration",
                                                        "task.baselinework",
                                                        "task.baseline start",
                                                        "task.baseline finish",
                                                        "task.baseline duration",
                                                        "task.baseline work",
                                                        "resource.baselinework",
                                                        "resource.baseline work",
                                                        "project.baselinestart",
                                                        "project.baselinefinish",
                                                        "project.baselinework",
                                                        "project.baseline start",
                                                        "project.baseline finish",
                                                        "project.baseline work",
                                                        "project.start",
                                                        "project.finish",
                                                        "task.start",
                                                        "task.finish",
                                                        "start",
                                                        "finish",
                                                        "actualstart",
                                                        "actualfinish",
                                                        "actualduration",
                                                        "remainingduration",
                                                        "remainingwork",
                                                        "remaining duration",
                                                        "remaining work",
                                                        "task.remainingduration",
                                                        "task.remainingwork",
                                                        "task.remaining duration",
                                                        "task.remaining work",
                                                        "resource.remainingduration",
                                                        "resource.remainingwork",
                                                        "resource.remaining duration",
                                                        "resource.remaining work",
                                                        "project.remainingduration",
                                                        "project.remainingwork",
                                                        "project.remaining duration",
                                                        "project.remaining work",

                                                        "actualwork",
                                                        "task.actualduration",
                                                        "task.actualwork",
                                                        "task.actual duration",
                                                        "task.actual work",
                                                        "resource.actualduration",
                                                        "resource.actualwork",
                                                        "resource.actual duration",
                                                        "resource.actual work",
                                                        "project.actualduration",
                                                        "project.actualwork",
                                                        "project.actual duration",
                                                        "project.actual work",
                                                        "project.actualstart",
                                                        "project.actualfinish",
                                                        "project.actual start",
                                                        "project.actual finish",
                                                        "task.actualstart",
                                                        "task.actualfinish",
                                                        "task.actual start",
                                                        "task.actual finish",
                                                        "resource.actualstart",
                                                        "resource.actualfinish",
                                                        "resource.actual start",
                                                        "resource.actual finish",
                                                        "actual start",
                                                        "actual finish",
                                                        "baseline start",
                                                        "baseline finish",
                                                        "project finish",
                                                        "cost",
                                                        "actual cost",
                                                        "remaining cost",
                                                        "baseline cost",
                                                        "remaining duration",
                                                        "actual duration",
                                                        "remaining work",
                                                        "actual work",
                                                        "id",
                                                        "unique id",
                                                        "% complete",
                                                        "percent complete",
                                                        "calendar",
                                                        "project calendar",
                                                        "resource names",
                                                        "max units",
                                                        "units",
                                                        "milestone"}
                                      .contains(folded) ||
                                  folded.starts_with("project:") || folded.starts_with("task:") ||
                                  folded.starts_with("resource:") || folded.starts_with("field:") ||
                                  folded.starts_with("project.") || folded.starts_with("task.") ||
                                  folded.starts_with("resource.");
            if (xmlSlotName(f.externalId).empty() &&
                (reserved || occupiedFormulaNames.contains(nameKey(f.scope, folded)))) {
                name = "XML_Field_" + f.externalId;
                while (occupiedFormulaNames.contains(nameKey(f.scope, fold(name))))
                    name += "_";
                diagnostic(diagnostics, "/ExtendedAttribute/" + qs(f.nativeId),
                           "계산식의 모호한 필드 이름을 외부 ID 기반 이름으로 구별합니다. 원본 이름은 source.ppm에 "
                           "보존합니다.");
            }
            occupiedFormulaNames.insert(nameKey(f.scope, fold(name)));
            formulaFieldNames[nameKey(f.scope, "field:" + f.nativeId)] = name;
        }

    if (externalFormulas)
        for (qsizetype i = 0; i < mapping.size(); ++i) {
            auto item = mapping[i].toObject();
            if (item["kind"] != "udf")
                continue;
            auto scope = item["scope"] == "TASK"         ? FieldScope::Task
                         : item["scope"] == "RSRC"       ? FieldScope::Resource
                         : item["scope"] == "ASSIGNMENT" ? FieldScope::Assignment
                                                         : FieldScope::Project;
            const auto key = "field:" + str(item["jobsId"].toString());
            if (formulaFieldNames.contains(nameKey(scope, key)))
                item["xmlFieldName"] = qs(formulaFieldNames.at(nameKey(scope, key)));
            mapping[i] = item;
        }
    auto scalarText = [&](const XmlLookup &l, const XmlLookupValue &v, bool tableValue = false) {
        return qs(exportXmlUdfScalar(p, l.valueType, v.value, tableValue, tableValue && !l.tableGuid.empty()));
    };
    std::set<std::string> writtenTables;
    if (std::any_of(udf.begin(), udf.end(), [](const auto &f) { return f.lookup && !f.lookup->tableGuid.empty(); })) {
        w.writeStartElement("OutlineCodes");
        for (const auto &f : udf)
            if (f.lookup && !f.lookup->tableGuid.empty() && writtenTables.insert(f.lookup->tableGuid).second) {
                const auto &l = *f.lookup;
                w.writeStartElement("OutlineCode");
                value("Guid", qs(l.tableGuid));
                if (xmlOutlineUdf(f.externalId))
                    value("FieldID", qs(f.externalId));
                if (!l.tableName.empty())
                    value("FieldName", qs(l.tableName));
                w.writeStartElement("Values");
                for (const auto &v : l.values)
                    if (v.listed) {
                        w.writeStartElement("Value");
                        if (!v.valueId.empty())
                            value("ValueID", qs(v.valueId));
                        value("FieldGUID", qs(v.guid));
                        value("Type", QString::number(xmlLookupType(l.valueType)));
                        if (!v.parentId.empty()) {
                            const auto &parent = *std::find_if(l.values.begin(), l.values.end(),
                                                               [&](const auto &o) { return o.optionId == v.parentId; });
                            if (parent.valueId.empty())
                                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                               {"XML 목록 부모의 숫자 ID가 없습니다."});
                            value("ParentValueID", qs(parent.valueId));
                        }
                        value("Value", scalarText(l, v, true));
                        if (!v.description.empty())
                            value("Description", qs(v.description));
                        w.writeEndElement();
                    }
                w.writeEndElement();
                value("Enterprise", xmlDynamicUdfScope(f.externalId) ? "1" : "0");
                value("LeafOnly", l.leafOnly ? "1" : "0");
                value("AllLevelsRequired", l.allLevelsRequired ? "1" : "0");
                value("OnlyTableValuesAllowed", l.restrictValues ? "1" : "0");
                if (!l.masks.empty()) {
                    w.writeStartElement("Masks");
                    for (const auto &m : l.masks) {
                        w.writeStartElement("Mask");
                        value("Level", QString::number(m.level));
                        value("Type", QString::number(m.type));
                        value("Length", QString::number(m.length));
                        value("Separator", qs(m.separator));
                        w.writeEndElement();
                    }
                    w.writeEndElement();
                }
                w.writeEndElement();
            }
        w.writeEndElement();
    }
    if (p.xmlWbs) {
        const auto &c = *p.xmlWbs;
        w.writeStartElement("WBSMasks");
        value("VerifyUniqueCodes", c.verifyUnique ? "1" : "0");
        value("GenerateCodes", c.generate ? "1" : "0");
        value("Prefix", qs(c.prefix));
        for (const auto &m : c.masks) {
            w.writeStartElement("WBSMask");
            value("Level", QString::number(m.level));
            value("Type", QString::number(m.type));
            value("Length", QString::number(m.length));
            value("Separator", qs(m.separator));
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!udf.empty()) {
        w.writeStartElement("ExtendedAttributes");
        for (const auto &f : udf) {
            if (qs(f.name).size() > 50)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"XML 사용자 필드 별칭은 50자까지입니다: " + f.name});
            w.writeStartElement("ExtendedAttribute");
            value("FieldID", qs(f.externalId));
            const auto name = externalFormulas ? formulaFieldNames.at(nameKey(f.scope, "field:" + f.nativeId))
                              : f.xmlIdentity  ? f.xmlIdentity->fieldName
                              : f.lookup       ? f.lookup->fieldName
                                               : "";
            const auto guid = f.xmlIdentity ? f.xmlIdentity->guid : f.lookup ? f.lookup->fieldGuid : "";
            if (!name.empty())
                value("FieldName", qs(name));
            value("CFType", qs(xmlCfType(f.type)));
            if (!guid.empty())
                value("Guid", qs(guid));
            value("ElemType", f.scope == FieldScope::Assignment ? "23" : f.scope == FieldScope::Resource ? "21" : "20");
            if (f.lookup && f.lookup->maxValues != 1)
                value("MaxMultiValues", QString::number(f.lookup->maxValues));
            value("Alias", qs(f.name));
            const auto *nativeDefinition = customField(p, "field:" + f.nativeId, f.scope);
            if (nativeDefinition->xmlFormula) {
                auto source = rewriteXmlFormula(*nativeDefinition->xmlFormula, [&](const XmlFormulaReference &ref) {
                    if (ref.key.starts_with("builtin:")) {
                        auto key = ref.key.substr(8);
                        if (xmlUnitReference(ref.key)) {
                            auto name = ref.key == "builtin:units" ? std::string("Units") : std::string("Max Units");
                            if (ref.scope != f.scope)
                                name = (ref.scope == FieldScope::Resource  ? "Resource."
                                        : ref.scope == FieldScope::Task    ? "Task."
                                        : ref.scope == FieldScope::Project ? "Project."
                                                                           : "Assignment.") +
                                       name;
                            return name;
                        }
                        if (xmlComputedReference(ref.key) || xmlActualReference(ref.key) ||
                            xmlBaselineReference(ref.key)) {
                            auto name = xmlBaselineReference(ref.key) ? xmlBaselineDisplayName(ref.key)
                                        : key == "earlystart"         ? std::string("Early Start")
                                        : key == "earlyfinish"        ? std::string("Early Finish")
                                        : key == "latestart"          ? std::string("Late Start")
                                        : key == "latefinish"         ? std::string("Late Finish")
                                        : key == "baselinestart"      ? std::string("Baseline Start")
                                        : key == "baselinefinish"     ? std::string("Baseline Finish")
                                        : key == "baselineduration"   ? std::string("Baseline Duration")
                                        : key == "baselinework"       ? std::string("Baseline Work")
                                        : key == "baselinecost"       ? std::string("Baseline Cost")
                                        : key == "remainingduration"  ? std::string("Remaining Duration")
                                        : key == "remainingwork"      ? std::string("Remaining Work")
                                        : key == "actualduration"     ? std::string("Actual Duration")
                                        : key == "actualwork"         ? std::string("Actual Work")
                                        : key == "actualstart"        ? std::string("Actual Start")
                                        : key == "actualfinish"       ? std::string("Actual Finish")
                                        : key == "start"              ? std::string("Start")
                                                                      : std::string("Finish");
                            if (ref.scope != f.scope)
                                name = (ref.scope == FieldScope::Project    ? "Project."
                                        : ref.scope == FieldScope::Resource ? "Resource."
                                                                            : "Task.") +
                                       name;
                            return name;
                        }
                        return key == "name"       ? std::string("Name")
                               : key == "summary"  ? std::string("Summary")
                               : key == "duration" ? std::string("Duration")
                               : key == "work"     ? std::string("Work")
                                                   : std::string("Project Start");
                    }
                    auto target = std::find_if(udf.begin(), udf.end(), [&](const auto &entry) {
                        return entry.scope == ref.scope && "field:" + entry.nativeId == ref.key;
                    });
                    if (target == udf.end())
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"계산식의 XML 내보내기 참조가 없습니다."});
                    auto name = formulaFieldNames.at(nameKey(ref.scope, ref.key));
                    if (ref.scope != f.scope)
                        name = (ref.scope == FieldScope::Project ? "Project."
                                : ref.scope == FieldScope::Task  ? "Task."
                                                                 : "Resource.") +
                               name;
                    return name;
                });
                value("Formula", qs(source));
                if (!nativeDefinition->xmlFormula->enabled)
                    diagnostic(diagnostics, "/ExtendedAttribute/" + qs(f.nativeId) + "/Formula",
                               "native에서 비활성인 식도 XML 정의에 보존합니다. XML 재가져오기는 지원식을 다시 "
                               "활성화하며 source.ppm은 실행 설정을 보존합니다.");
            }
            if (nativeDefinition->xmlRollUp) {
                value("CalculationType", QString::number(nativeDefinition->xmlRollUp->calculationType));
                value("RollupType", QString::number(nativeDefinition->xmlRollUp->rollupType));
            }
            if (nativeDefinition->xmlRollDown) {
                const auto &rule = *nativeDefinition->xmlRollDown;
                if (!rule.assignmentFieldId.empty()) {
                    auto target = std::find_if(udf.begin(), udf.end(), [&](const auto &entry) {
                        return entry.scope == FieldScope::Assignment && entry.nativeId == rule.assignmentFieldId;
                    });
                    if (target == udf.end())
                        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"내보낼 roll-down 배정 필드가 없습니다."});
                    value("SecondaryPID", qs(target->externalId));
                }
                value("AutoRollDown", rule.automatic ? "1" : "0");
            }
            const XmlLookupValue *defaultValue = nullptr;
            if (f.lookup && !f.lookup->defaultOptionId.empty())
                defaultValue = &*std::find_if(f.lookup->values.begin(), f.lookup->values.end(),
                                              [&](const auto &v) { return v.optionId == f.lookup->defaultOptionId; });
            if (defaultValue && !defaultValue->guid.empty())
                value("DefaultGuid", qs(defaultValue->guid));
            if (f.lookup && !f.lookup->tableGuid.empty())
                value("Ltuid", qs(f.lookup->tableGuid));
            if (f.lookup)
                value("RestrictValues", f.lookup->restrictValues ? "1" : "0");
            if (defaultValue && !defaultValue->valueId.empty())
                value("Default", qs(defaultValue->valueId));
            if (f.lookup && f.lookup->tableGuid.empty() &&
                std::any_of(f.lookup->values.begin(), f.lookup->values.end(), [](const auto &v) { return v.listed; })) {
                w.writeStartElement("ValueList");
                for (const auto &v : f.lookup->values)
                    if (v.listed) {
                        w.writeStartElement("Value");
                        value("ID", qs(v.valueId));
                        value("Value", scalarText(*f.lookup, v, true));
                        if (!v.description.empty())
                            value("Description", qs(v.description));
                        w.writeEndElement();
                    }
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    std::size_t udfCells = 0;
    auto writeUdf = [&](FieldScope scope, const Task *task, const Resource *resource,
                        const Assignment *assignment = nullptr, const std::vector<TaskAttribute> *summary = nullptr) {
        std::size_t enterpriseEntries = 0;
        for (const auto &f : udf)
            if (f.scope == scope) {
                calculationCheckpoint();
                if (++udfCells > 100000)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 조회 셀 한도 100,000개 초과"});
                const auto *native = customField(p, "field:" + f.nativeId, f.scope);
                if (assignment) {
                    const auto key = "field:" + f.nativeId;
                    if (std::find(assignment->rollDownMissing.begin(), assignment->rollDownMissing.end(), key) !=
                        assignment->rollDownMissing.end()) {
                        w.writeStartElement("ExtendedAttribute");
                        value("FieldID", qs(f.externalId));
                        w.writeEndElement();
                        diagnostic(diagnostics, "/Assignment/" + qs(assignmentAttributeId(*assignment)),
                                   "직접 값 없는 ExtendedAttribute는 명시 미설정 · 외부 제품의 manual provenance "
                                   "왕복은 별도 검증");
                        continue;
                    }
                    if (!attributeValue(assignment->attributes, key) && !calculatedField(*native))
                        continue; // Inheritance stays live across XML roundtrips.
                }
                if (f.lookup) {
                    const auto key = "field:" + f.nativeId;
                    const auto selected = summary      ? attributeValue(*summary, key)
                                          : assignment ? attributeValue(assignment->attributes, key)
                                          : task       ? attributeValue(task->attributes, key)
                                          : resource   ? attributeValue(resource->attributes, key)
                                                       : attributeValue(p.projectAttributes, key);
                    if (!selected)
                        continue;
                    const auto ids = choiceSelection(*native, *selected);
                    const bool allListed = std::all_of(ids.begin(), ids.end(), [&](const auto &id) {
                        return std::find_if(f.lookup->values.begin(), f.lookup->values.end(),
                                            [&](const auto &v) { return v.optionId == id; })
                            ->listed;
                    });
                    for (const auto &id : ids) {
                        const auto &v = *std::find_if(f.lookup->values.begin(), f.lookup->values.end(),
                                                      [&](const auto &v) { return v.optionId == id; });
                        const auto scalar = scalarText(*f.lookup, v);
                        if (f.type != CustomFieldType::Duration && f.xmlIdentity && v.listed && !v.guid.empty() &&
                            ids.size() == 1) {
                            value(QString::number(qs(f.externalId).toUInt(), 16), qs(v.guid));
                        } else if (f.type != CustomFieldType::Duration && f.xmlIdentity && ids.size() > 1 &&
                                   !f.lookup->tableGuid.empty()) {
                            if (++enterpriseEntries > 450)
                                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                               {"XML 객체별 기업 참조 요소는 450개까지입니다."});
                            w.writeStartElement("EnterpriseExtendedAttribute");
                            value("FieldIDInHex", QString::number(qs(f.externalId).toUInt(), 16));
                            value("FieldID", qs(f.externalId));
                            value("Value", scalar);
                            if (f.type == CustomFieldType::Duration)
                                value("DurationFormat", QString::number(xmlFieldDurationFormat(v.value)));
                            if (!v.guid.empty())
                                value("FieldValueGUID", qs(v.guid));
                            w.writeEndElement();
                        } else if (xmlOutlineUdf(f.externalId) && allListed) {
                            w.writeStartElement("OutlineCode");
                            value("FieldID", qs(f.externalId));
                            if (!v.valueId.empty())
                                value("ValueID", qs(v.valueId));
                            if (!v.guid.empty())
                                value("ValueGUID", qs(v.guid));
                            w.writeEndElement();
                        } else {
                            w.writeStartElement("ExtendedAttribute");
                            value("FieldID", qs(f.externalId));
                            value("Value", scalar);
                            if (f.type == CustomFieldType::Duration)
                                value("DurationFormat", QString::number(xmlFieldDurationFormat(v.value)));
                            if (!v.guid.empty())
                                value("ValueGUID", qs(v.guid));
                            w.writeEndElement();
                        }
                    }
                    continue;
                }
                auto v = summary ? attributeValue(*summary, "field:" + f.nativeId)
                                 : exportUdfValue(p, f, task, resource, diagnostics, assignment);
                if (!v)
                    continue;
                const auto scalar = qs(exportXmlUdfScalar(p, f.type, *v));
                if (f.xmlIdentity && f.type != CustomFieldType::Duration) {
                    value(QString::number(QString::fromStdString(f.externalId).toUInt(), 16), scalar);
                } else {
                    w.writeStartElement("ExtendedAttribute");
                    value("FieldID", qs(f.externalId));
                    value("Value", scalar);
                    if (f.type == CustomFieldType::Duration)
                        value("DurationFormat", QString::number(xmlFieldDurationFormat(*v)));
                    w.writeEndElement();
                }
            }
    };
    std::map<std::string, int> calendars;
    calendars[""] = 1;
    w.writeStartElement("Calendars");
    auto calendar = [&](const TaskCalendar &c, int id) {
        if (c.periods.size() > 5 || c.holidays.size() > 10000)
            throw AppError(ErrorCode::FILE_PROJECTXML_077, {});
        w.writeStartElement("Calendar");
        value("UID", QString::number(id));
        value("Name", qs(c.name));
        value("IsBaseCalendar", "1");
        value("BaseCalendarUID", "-1");
        w.writeStartElement("WeekDays");
        for (int day = 1; day <= 7; ++day) {
            int nativeDay = day == 1 ? 7 : day - 1;
            bool working = std::find(c.weekdays.begin(), c.weekdays.end(), nativeDay) != c.weekdays.end();
            w.writeStartElement("WeekDay");
            value("DayType", QString::number(day));
            value("DayWorking", working ? "1" : "0");
            if (working) {
                w.writeStartElement("WorkingTimes");
                for (const auto &period : c.periods) {
                    w.writeStartElement("WorkingTime");
                    value("FromTime", timeText(period.begin));
                    value("ToTime", timeText(period.end));
                    w.writeEndElement();
                }
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        for (const auto &date : c.holidays) {
            w.writeStartElement("WeekDay");
            value("DayType", "0");
            value("DayWorking", "0");
            w.writeStartElement("TimePeriod");
            value("FromDate", qs(date) + "T00:00:00");
            value("ToDate", qs(date) + "T23:59:00");
            w.writeEndElement();
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
    };
    TaskCalendar standard;
    standard.name = "PPM standard";
    for (int suffix = 2;
         std::any_of(p.calendars.begin(), p.calendars.end(), [&](const auto &c) { return c.name == standard.name; });
         ++suffix)
        standard.name = "PPM standard_" + std::to_string(suffix);
    standard.holidays = p.holidays;
    calendar(standard, 1);
    map("calendar", 1, "");
    int next = 2;
    for (const auto &c : p.calendars) {
        calendars[c.id] = next;
        calendar(c, next);
        map("calendar", next++, c.id);
    }
    w.writeEndElement();

    struct Row {
        const WbsNode *group = nullptr;
        const Task *task = nullptr;
        int level = 1;
    };
    std::map<std::string, std::vector<const WbsNode *>> groups;
    std::map<std::string, std::vector<const Task *>> tasks;
    for (const auto &g : p.wbsNodes)
        groups[g.parentId].push_back(&g);
    for (const auto &t : p.tasks)
        tasks[t.wbsId].push_back(&t);
    std::vector<Row> rows;
    std::function<void(const std::string &, int)> visit = [&](const std::string &parent, int level) {
        if (level > 32)
            throw AppError(ErrorCode::FILE_PROJECTXML_078, {});
        for (auto *g : groups[parent]) {
            rows.push_back({g, nullptr, level});
            visit(g->id, level + 1);
        }
        for (auto *t : tasks[parent])
            rows.push_back({nullptr, t, level});
    };
    visit("", 1);
    std::map<std::string, int> taskIds;
    next = 1;
    for (const auto &row : rows) {
        if (row.task)
            taskIds[row.task->id] = next;
        map(row.task ? "task" : "wbs", next, row.task ? row.task->id : row.group->id);
        ++next;
    }
    const RollUpValues summaries(p);
    auto summaryValues = [&](const std::string &id) {
        std::vector<TaskAttribute> values;
        if (id.empty())
            values = p.taskSummaryAttributes;
        else {
            auto g = std::find_if(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == id; });
            if (g != p.wbsNodes.end())
                values = g->attributes;
        }
        for (const auto *f : summaries.fields())
            if (f->xmlRollUp && (f->xmlRollUp->calculationType == 1 ||
                                 (f->xmlRollUp->calculationType == 2 && f->xmlFormula && f->xmlFormula->enabled))) {
                const auto key = "field:" + f->id;
                const auto &cell = summaries.cell(id, key);
                const auto old = attributeValue(values, key);
                std::erase_if(values, [&](const auto &v) { return v.key == key; });
                if (cell.value)
                    values.push_back({key, *cell.value});
                if (!cell.issue.empty())
                    diagnostic(diagnostics, "/TaskSummary/" + qs(id), qs(f->name + ": " + cell.issue));
                if (old && old != cell.value)
                    diagnostic(diagnostics, "/TaskSummary/" + qs(id),
                               "저장된 명시 요약 값 대신 현재 자동 roll-up 결과를 내보냅니다.");
            }
        return values;
    };
    const auto baselineRows = projectXmlBaselines(p, diagnostics);
    const auto baselineIndex = indexXmlBaselines(baselineRows);
    w.writeStartElement("Tasks");
    if (!p.projectFields.empty() || !summaries.fields().empty() ||
        std::any_of(baselineRows.begin(), baselineRows.end(), [](const auto &r) { return r.kind == "project"; })) {
        w.writeStartElement("Task");
        value("UID", "0");
        value("ID", "0");
        value("Name", qs(p.name));
        value("Summary", "1");
        writeUdf(FieldScope::Project, nullptr, nullptr);
        const auto summary = summaryValues("");
        writeUdf(FieldScope::Task, nullptr, nullptr, nullptr, &summary);
        writeXmlBaselines(w, baselineIndex, "project", "", "", 0, p.currency);
        writeXmlBaselineTimes(w, baselineIndex, "project", "", "", 0);
        w.writeEndElement();
    }
    next = 1;
    for (const auto &row : rows) {
        w.writeStartElement("Task");
        value("UID", QString::number(next));
        value("ID", QString::number(next++));
        value("Name", qs(row.task ? row.task->name : row.group->name));
        value("Type", "1");
        if (auto code = xmlWbsCode(p, row.task ? "task" : "wbs", row.task ? row.task->id : row.group->id)) {
            value("WBS", qs(*code));
            auto level = xmlWbsLevel(*p.xmlWbs, *code);
            if (!level.empty())
                value("WBSLevel", qs(level));
        }
        value("OutlineLevel", QString::number(row.level));
        const auto *observed = actuals && row.task && actuals->progress.contains(row.task->id)
                                   ? actuals->progress.at(row.task->id)
                                   : nullptr;
        std::optional<std::int64_t> exportedDuration;
        if (row.task)
            exportedDuration = curveDurations.contains(row.task->id)
                                   ? std::optional<std::int64_t>{curveDurations.at(row.task->id)}
                                   : row.task->durationMinutes;
        if (observed)
            exportedDuration = observed->actualDurationMinutes + observed->remainingMinutes;
        if (exportedDuration)
            value("Duration", durationText(*exportedDuration));
        value("DurationFormat", "3");
        value("Milestone", row.task && (actuals ? exportedDuration == 0 : row.task->durationMinutes == 0) ? "1" : "0");
        value("Summary", row.group ? "1" : "0");
        if (actuals && row.task) {
            if (observed && observed->actualStart)
                value("ActualStart", xmlStamp(*observed->actualStart));
            if (observed && observed->actualFinish)
                value("ActualFinish", xmlStamp(*observed->actualFinish));
            if (observed)
                value("ActualDuration", durationText(observed->actualDurationMinutes));
            const auto cost = actuals->cost.find(row.task->id);
            if (cost != actuals->cost.end() && cost->second->actualMinor)
                value("ActualCost", qs(exportXmlUdfScalar(p, CustomFieldType::Cost,
                                                          formatFieldCost(*cost->second->actualMinor, p.currency))));
            if (const auto work = actuals->taskWork.at(row.task->id))
                value("ActualWork", curveWorkText(*work));
            if (observed)
                value("RemainingDuration", durationText(observed->remainingMinutes));
            if (observed && observed->remainingWorkUnitMinutes)
                value("RemainingWork", curveWorkText(*observed->remainingWorkUnitMinutes));
        }
        if (row.task) {
            const auto &t = *row.task;
            static const std::array<ConstraintType, 8> constraints{
                ConstraintType::ASAP, ConstraintType::ALAP, ConstraintType::MSO,  ConstraintType::MFO,
                ConstraintType::SNET, ConstraintType::SNLT, ConstraintType::FNET, ConstraintType::FNLT};
            auto it = std::find(constraints.begin(), constraints.end(), t.constraintType);
            value("ConstraintType", QString::number(it - constraints.begin()));
            value("CalendarUID", QString::number(calendars.at(t.calendarId)));
            if (t.constraintAt)
                value("ConstraintDate", xmlStamp(*t.constraintAt));
            if (t.deadlineAt)
                value("Deadline", xmlStamp(*t.deadlineAt));
            for (const auto &d : dependencies(t)) {
                w.writeStartElement("PredecessorLink");
                value("PredecessorUID", QString::number(taskIds.at(d.predecessorId)));
                int type = d.type == DependencyType::FF   ? 0
                           : d.type == DependencyType::FS ? 1
                           : d.type == DependencyType::SF ? 2
                                                          : 3;
                value("Type", QString::number(type));
                value("CrossProject", "0");
                value("LinkLag", QString::number(d.lagMinutes * 10));
                value("LagFormat", "3");
                w.writeEndElement();
            }
            if (t.taskType != TaskType::FixedDuration || t.workUnitMinutes || !t.breaks.empty())
                diagnostic(diagnostics, "/Task/" + qs(t.id),
                           "공수·작업 유형·분할을 내보내지 않음 · 명목 기간 기반 투영");
            if (actuals ? !exportedDuration : !t.durationMinutes)
                diagnostic(diagnostics, "/Task/" + qs(t.id) + "/Duration",
                           "미정 기간 생략 · 외부 앱에서 기본 기간으로 해석할 수 있음");
        }
        writeXmlBaselines(w, baselineIndex, row.task ? "task" : "wbs", row.task ? row.task->id : row.group->id, "",
                          next - 1, p.currency);
        if (row.task)
            writeUdf(FieldScope::Task, row.task, nullptr);
        else {
            const auto summary = summaryValues(row.group->id);
            writeUdf(FieldScope::Task, nullptr, nullptr, nullptr, &summary);
        }
        writeXmlBaselineTimes(w, baselineIndex, row.task ? "task" : "wbs", row.task ? row.task->id : row.group->id, "",
                              next - 1);
        w.writeEndElement();
    }
    w.writeEndElement();
    std::map<std::string, int> resources;
    next = 1;
    w.writeStartElement("Resources");
    for (const auto &r : p.resources) {
        resources[r.id] = next;
        map("resource", next, r.id);
        w.writeStartElement("Resource");
        value("UID", QString::number(next++));
        value("Name", qs(r.name));
        value("Type", "1");
        if (r.maxUnits)
            value("MaxUnits", unitText(*r.maxUnits));
        else
            diagnostic(diagnostics, "/Resource/" + qs(r.id) + "/MaxUnits",
                       "미정 가용량 생략 · 외부 앱에서 기본 가용량으로 해석할 수 있음");
        const auto availabilityStamp = [](std::string stamp, bool finish = false) {
            stamp[10] = 'T';
            return qs(stamp) + (finish ? ":59" : ":00");
        };
        if (r.availableFrom)
            value("AvailableFrom", availabilityStamp(*r.availableFrom));
        if (r.availableTo)
            value("AvailableTo", availabilityStamp(*r.availableTo, true));
        if (actuals)
            if (const auto work = actuals->resourceWork.at(r.id))
                value("ActualWork", curveWorkText(*work));
        value("CalendarUID", QString::number(calendars.at(r.calendarId)));
        writeUdf(FieldScope::Resource, nullptr, &r);
        writeXmlBaselines(w, baselineIndex, "resource", r.id, "", resources.at(r.id), p.currency);
        if (hasResourceAvailability(r)) {
            const auto periods = resourceAvailabilityIntervals(r);
            if (periods.size() > 10000)
                throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
            w.writeStartElement("AvailabilityPeriods");
            for (const auto &period : periods) {
                calculationCheckpoint();
                w.writeStartElement("AvailabilityPeriod");
                if (period.from)
                    value("AvailableFrom", availabilityStamp(*period.from));
                if (period.to)
                    value("AvailableTo", availabilityStamp(*period.to, true));
                if (period.units)
                    value("AvailableUnits", unitText(*period.units));
                w.writeEndElement();
            }
            w.writeEndElement();
            diagnostic(diagnostics, "/Resource/" + qs(r.id) + "/AvailabilityPeriods",
                       "가용 경계·날짜 예외·기간별 가용률을 비중첩 전체 기간으로 투영합니다. 편집 정의/우선순위는 "
                       "source.ppm에 보존합니다.");
        } else if (!r.exceptions.empty()) {
            diagnostic(diagnostics, "/Project/Resource/AvailabilityPeriods",
                       "native 날짜별 가용률 예외를 출력합니다. 기간 밖 MaxUnits 기본값 적용은 PPM 계산식 계약이며 "
                       "외부 제품의 가용량 해석과 다를 수 있습니다.");
            w.writeStartElement("AvailabilityPeriods");
            for (const auto &day : r.exceptions) {
                calculationCheckpoint();
                w.writeStartElement("AvailabilityPeriod");
                value("AvailableFrom", qs(day.date) + "T00:00:00");
                value("AvailableTo", qs(day.date) + "T23:59:00");
                if (day.units)
                    value("AvailableUnits", unitText(*day.units));
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        writeXmlBaselineTimes(w, baselineIndex, "resource", r.id, "", resources.at(r.id));
        w.writeEndElement();
        if (r.type != "labor")
            diagnostic(diagnostics, "/Resource/" + qs(r.id), "장비를 work 자원으로 투영");
        if (!r.rates.empty())
            diagnostic(diagnostics, "/Resource/" + qs(r.id), "단가는 source.ppm에 보존합니다.");
        if (r.productivity)
            diagnostic(diagnostics, "/Resource/" + qs(r.id) + "/Productivity",
                       "물량/자원시간 생산성 정의는 source.ppm에 보존 · 단가/가용량으로 추정하지 않음");
    }
    w.writeEndElement();
    next = 1;
    w.writeStartElement("Assignments");
    for (const auto &a : p.assignments) {
        w.writeStartElement("Assignment");
        const auto assignmentUid = next;
        value("UID", QString::number(next));
        map("assignment", next++, a.taskId + "/" + a.resourceId);
        value("TaskUID", QString::number(taskIds.at(a.taskId)));
        value("ResourceUID", QString::number(resources.at(a.resourceId)));
        const auto key = std::pair{a.taskId, a.resourceId};
        if (actuals) {
            if (const auto cost = actuals->assignmentCost.at(key))
                value("ActualCost",
                      qs(exportXmlUdfScalar(p, CustomFieldType::Cost, formatFieldCost(*cost, p.currency))));
            if (const auto work = actuals->work.at(key))
                value("ActualWork", curveWorkText(*work));
        }
        value("Units", unitText(a.units));
        if (curveExports.contains(key) && !actuals) {
            const auto &curve = curveExports.at(key);
            const auto code = curve.definition.durationMinutes == 0 ? 0
                              : curve.exact && curve.definition.source == "mspdi" && curve.definition.mspCode >= 0
                                  ? curve.definition.mspCode
                                  : 8;
            value("WorkContour", QString::number(code));
            value("Start", xmlStamp(curve.dates.start));
            value("Finish", xmlStamp(curve.dates.finish));
            value("Work", curveWorkText(curve.definition.workUnitMinutes));
            value("RemainingWork", curveWorkText(curve.definition.workUnitMinutes));
            value("ActualWork", "PT0H0M0S");
            if (curve.definition.durationMinutes > 0)
                for (const auto &data : curveTimephases(p, a, curve)) {
                    w.writeStartElement("TimephasedData");
                    value("Type", "1");
                    value("UID", QString::number(assignmentUid));
                    value("Start", xmlStamp(data.start));
                    value("Finish", xmlStamp(data.finish));
                    value("Unit", "0");
                    value("Value", curveWorkText(data.work));
                    w.writeEndElement();
                }
        }
        writeUdf(FieldScope::Assignment, nullptr, nullptr, &a);
        writeXmlBaselines(w, baselineIndex, "assignment", a.taskId, a.resourceId, assignmentUid, p.currency);
        writeXmlBaselineTimes(w, baselineIndex, "assignment", a.taskId, a.resourceId, assignmentUid);
        if (actuals && actuals->days.contains(key))
            for (const auto *entry : actuals->days.at(key))
                for (const auto type : {2, 6}) {
                    const auto amount = type == 2 ? entry->workUnitMinutes : entry->actualMinor;
                    if (!amount)
                        continue;
                    calculationCheckpoint();
                    w.writeStartElement("TimephasedData");
                    value("Type", QString::number(type));
                    value("UID", QString::number(assignmentUid));
                    value("Start", qs(entry->date) + "T00:00:00");
                    value("Finish", QDate::fromString(qs(entry->date), Qt::ISODate).addDays(1).toString(Qt::ISODate) +
                                        "T00:00:00");
                    value("Unit", "2");
                    value("Value", type == 2 ? curveWorkText(*amount)
                                             : qs(exportXmlUdfScalar(p, CustomFieldType::Cost,
                                                                     formatFieldCost(*amount, p.currency))));
                    w.writeEndElement();
                }
        if (a.productivityEstimate)
            diagnostic(diagnostics, "/Assignment/" + qs(a.taskId) + "/Productivity",
                       actuals ? "물량·생산성 근거와 계획 공수 분포는 source.ppm에 보존 · 표준 실적은 명시 관측만 교환"
                               : "물량·생산성 근거는 source.ppm에 보존 · 환산된 계획 공수만 교환");
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    if (w.hasError() || bytes.size() > byteLimit)
        throw AppError(ErrorCode::FILE_PROJECTXML_079, {});
    // Parsing our own output is a support-boundary check, never a Microsoft Project compatibility claim.
    importProjectXml(bytes, "project.xml");
    if (!actuals && (!p.baselines.empty() || !p.progress.empty() || !p.costEntries.empty() || !p.scenarios.empty() ||
                     p.statusAt || !p.taskCosts.empty()))
        diagnostic(diagnostics, "/Project/history-costs",
                   "지원 기준선 값은 표준 Baseline으로 내보냅니다. 전체 승인/실적/원가 이력·시나리오는 source.ppm에 "
                   "보존합니다.");
    auto report = baseReport(p, false);
    if (!p.currencyConversions.empty())
        diagnostic(diagnostics, "/Project/CurrencyConversions",
                   "현재 통화의 환산 투영입니다. 원래 제출 금액·환율·전후 문서는 source.ppm에 보존합니다.");
    report["source"] = QJsonObject{{"name", "source.ppm"}, {"product", "PPM"}};
    report["xmlSha256"] = hash(bytes);
    report["mapping"] = mapping;
    report["diagnostics"] = diagnostics;
    ProjectXmlExchange result{p, bytes, report, false};
    if (!baselineRows.empty()) {
        QJsonArray baselineNumbers;
        std::set<int> seen;
        qsizetype times = 0;
        for (const auto &r : baselineRows) {
            seen.insert(r.number);
            times += qsizetype(r.timephased.size());
        }
        for (int n : seen)
            baselineNumbers.append(n);
        result.report["baselineExport"] = QJsonObject{{"rows", int(baselineRows.size())},
                                                      {"numbers", baselineNumbers},
                                                      {"timephased", int(times)},
                                                      {"policy", "adopted-external-slot-before-native-version"}};
    }
    if (actuals) {
        result.exportingActuals = true;
        result.report["contract"] = "jobs-mspdi-observed-v1";
        result.report["exportMode"] = "observed-actuals-v1";
        result.report["actualExport"] = actuals->report;
    }
    return result;
}
ProjectXmlExchange exportProjectXml(const Project &p) {
    return exportProjectXmlImpl(p, false);
}
ProjectXmlExchange exportProjectXmlActuals(const Project &p) {
    return exportProjectXmlImpl(p, true);
}

QString publishProjectXml(const ProjectXmlExchange &e, const QString &destination) {
    if (!e.importing) {
        const auto replay = e.exportingActuals ? exportProjectXmlActuals(e.project) : exportProjectXml(e.project);
        if (e.xml != replay.xml || e.report != replay.report)
            throw AppError(ErrorCode::XML_ACTUAL_EXPORT_INVALID,
                           {"출력 XML/매핑/실적 기록이 native 원본 재검증과 다릅니다."});
    }
    if (e.importing && (e.project.xmlBaselines || e.report.contains("xmlBaselineAdoption")))
        verifyXmlBaselineImport(e);
    if (e.importing && !e.report.contains("xmlBaselineAdoption") &&
        (!e.project.progress.empty() || e.project.statusAt || e.report.contains("xmlProgressAdoption"))) {
        const auto adoption = e.report["xmlProgressAdoption"].toObject();
        auto plan = importProjectXml(e.xml, e.report["source"].toObject()["name"].toString());
        plan.project.documentId = e.project.documentId;
        XmlProgressOptions options;
        options.statusAt = adoption["statusAt"].toString().toStdString();
        options.reporter = adoption["reporter"].toString().toStdString();
        options.reason = adoption["reason"].toString().toStdString();
        options.recordedAt = adoption["recordedAt"].toString().toStdString();
        options.confirmed = true;
        for (const auto &row : adoption["selected"].toArray())
            options.taskIds.push_back(row.toObject()["taskId"].toString().toStdString());
        const auto replay = adoptXmlProgress(plan, options);
        if (replay.project != e.project || replay.report["xmlProgressAdoption"] != e.report["xmlProgressAdoption"] ||
            replay.report["source"] != e.report["source"] || replay.report["mapping"] != e.report["mapping"] ||
            replay.report["taskProgressReview"] != e.report["taskProgressReview"] ||
            replay.report["revision"] != e.report["revision"] || replay.report["documentId"] != e.report["documentId"])
            throw AppError(ErrorCode::XML_PROGRESS_INVALID, {"채택 결과/출처가 원본 재검증과 다릅니다."});
    }
    return publishPlanExchange(e.project, e.xml, e.report, e.importing, "xml", destination);
}
QString projectXmlPreview(const ProjectXmlExchange &e) {
    auto text = QString("%1\n작업 %2 · WBS %3 · 달력 %4 · 자원 %5 · 배정 %6\n\n가져오기: %7\n원본 보존: %8\nPPM "
                        "재계산 동등성: 미검증\nMicrosoft Project 왕복: 미검증\n\n원본/진단은 교환 폴더에 보관됩니다. "
                        ".ppm만 사본 저장/이동하면 동반 파일은 자동 복제되지 않습니다.\n\n")
                    .arg(qs(e.project.name))
                    .arg(e.project.tasks.size())
                    .arg(e.project.wbsNodes.size())
                    .arg(e.project.calendars.size())
                    .arg(e.project.resources.size())
                    .arg(e.project.assignments.size())
                    .arg(e.importing ? "지원 부분 변환" : "해당 없음");
    text = text.arg(e.importing ? "XML 원본 바이트의 동반 파일" : "현재 프로젝트 전체 입력/이력의 동반 사본");
    if (!e.baselineCandidates.empty())
        text +=
            QString("외부 기준선 후보 %1건 · 번호별 별도 검토/채택 · 기본 미채택\n\n").arg(e.baselineCandidates.size());
    if (e.report.contains("xmlBaselineAdoption"))
        text += QString("외부 기준선 %1건 채택 · native 승인 이력과 구분\n\n")
                    .arg(e.project.xmlBaselines ? e.project.xmlBaselines->rows.size() : 0);
    if (e.report.contains("baselineExport")) {
        const auto baseline = e.report["baselineExport"].toObject();
        text += QString("표준 Baseline 포함 · 객체/번호 값 %1건 · 시간분포 %2건\n미확정 값 생략 · 상세 이력 source.ppm "
                        "보존\n\n")
                    .arg(baseline["rows"].toInt())
                    .arg(baseline["timephased"].toInt());
    }
    if (e.exportingActuals) {
        const auto actual = e.report["actualExport"].toObject();
        text += QString("표준 실적 포함 · 상태일 %1\n작업 진척 %2 · 작업 누계 %3 · 일별 관측 %4\n미보고 생략 · 일별 "
                        "버킷은 실제 시각 아님 · 전체 이력 source.ppm 보존\n\n")
                    .arg(actual["statusAt"].toString())
                    .arg(actual["taskProgress"].toArray().size())
                    .arg(actual["taskCost"].toArray().size())
                    .arg(actual["assignmentDays"].toArray().size());
        int shown = 0;
        const auto amount = [](const QJsonValue &v) {
            return v.isNull() ? QString("미보고") : QString::number(qint64(v.toDouble()));
        };
        const auto money = [&](const QJsonValue &v) {
            return v.isNull()
                       ? QString("미보고")
                       : qs(formatFieldCost(qint64(v.toDouble()), e.project.currency) + " " + e.project.currency);
        };
        for (const auto &value : actual["taskProgress"].toArray()) {
            if (shown++ == 2000)
                break;
            const auto row = value.toObject();
            text += QString("작업 %1 · 실제 %2분 / 잔여 %3분 → XML 기간 %4분\n")
                        .arg(row["taskId"].toString(), amount(row["actualMinutes"]), amount(row["remainingMinutes"]),
                             amount(row["projectedDuration"]));
            text += QString("  native 계획 기간 %1분\n").arg(amount(row["nativePlanDuration"]));
            text += QString("  실제 시작 %1 / 완료 %2\n")
                        .arg(row["actualStart"].isNull() ? QString("미보고") : row["actualStart"].toString(),
                             row["actualFinish"].isNull() ? QString("미보고") : row["actualFinish"].toString());
        }
        for (const auto &value : actual["costReconciliation"].toArray()) {
            if (shown++ >= 2000)
                break;
            const auto row = value.toObject();
            text += QString("작업 AC %1 · 누계 %2 / 날짜 배분 %3 / 미배분 %4\n")
                        .arg(row["taskId"].toString(), money(row["cumulative"]), money(row["datedAllocated"]),
                             money(row["unallocated"]));
        }
        text += "일별 공수 단위: 100u = 1자원·분\n";
        for (const auto &value : actual["assignmentDays"].toArray()) {
            if (shown++ >= 2000)
                break;
            const auto row = value.toObject();
            text += QString("배정 %1 / %2 · %3 · 공수 %4u · 원가 %5\n")
                        .arg(row["taskId"].toString(), row["resourceId"].toString(), row["date"].toString(),
                             amount(row["workUnitMinutes"]), money(row["actualMinor"]));
        }
        if (shown >= 2000)
            text += "추가 실적/원가 대사는 conversion.json 참조\n";
        text += "\n";
    }
    if (e.importing) {
        const auto ready = std::count_if(e.progressCandidates.begin(), e.progressCandidates.end(),
                                         [](const auto &row) { return row.present && row.issue.empty(); });
        text += QString("XML 진척 후보 %1건 · 형식 완비 %2건 · 기본값 미채택\n채택 상태일 후보: %3\n%4\n\n")
                    .arg(e.progressCandidates.size())
                    .arg(ready)
                    .arg(e.progressStatusAt ? qs(*e.progressStatusAt) : QString("미설정"))
                    .arg(qs(e.progressStatusIssue));
    }
    for (const auto &v : e.report["mapping"].toArray()) {
        const auto f = v.toObject();
        if (f["kind"] == "udf")
            text +=
                QString("UDF %1 · %2 → %3 (%4)\n")
                    .arg(f["scope"].toString(), f["name"].toString(), f["externalId"].toString(), f["type"].toString());
    }
    const auto diagnostics = e.report["diagnostics"].toArray();
    int shown = 0;
    for (const auto &d : diagnostics) {
        if (shown++ == 2000) {
            text += "이후 진단은 conversion.json 참조\n";
            break;
        }
        auto o = d.toObject();
        text += o["path"].toString() + ": " + o["message"].toString() + "\n";
    }
    return text;
}
} // namespace jobs
