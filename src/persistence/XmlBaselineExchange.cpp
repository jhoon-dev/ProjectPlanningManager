#include "persistence/XmlBaselineExchange.h"
#include "persistence/XmlBaselineTimephased.h"
#include "calculation/BaselineFormulaValues.h"
#include "domain/XmlBaselines.h"
#include "domain/XmlCalendar.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <set>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string str(const QString &s) {
    return s.toStdString();
}
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 기준선: " + s});
}
std::string issue(const AppError &e) {
    return e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments.front();
}
bool known(const QString &s) {
    return !s.trimmed().isEmpty() && s.trimmed() != "NA";
}
int number(const QString &s, int maximum) {
    // Cache the fixed pattern per thread; values and bounds remain per call.
    thread_local const QRegularExpression integer("^[+-]?[0-9]+$");
    bool ok = false;
    const auto token = s.trimmed();
    auto n = token.toLongLong(&ok);
    if (!ok || n < 0 || n > maximum || !integer.match(token).hasMatch())
        invalid("번호/UID 형식을 확인하세요.");
    return int(n);
}
QString scalarText(QXmlStreamReader &reader, QStringList &errors) {
    const auto name = reader.name().toString();
    QString text;
    while (!reader.atEnd()) {
        const auto t = reader.readNext();
        if (t == QXmlStreamReader::Characters)
            text += reader.text();
        else if (t == QXmlStreamReader::StartElement) {
            errors.append("중첩 값 " + name);
            reader.skipCurrentElement();
        } else if (t == QXmlStreamReader::EndElement)
            return text;
    }
    return text;
}
std::map<QString, QString> scalars(QXmlStreamReader &r, QStringList &errors) {
    std::map<QString, QString> values;
    while (r.readNextStartElement()) {
        auto name = r.name().toString();
        auto text = scalarText(r, errors);
        if (values.contains(name))
            errors.append("중복 " + name);
        values[name] = text;
    }
    return values;
}
XmlBaselineSet metadata(const PlanInput &p) {
    XmlBaselineSet s;
    TaskCalendar c;
    c.holidays = p.holidays;
    if (!p.xmlFormulaCalendarId.empty()) {
        auto i = std::find_if(p.calendars.begin(), p.calendars.end(),
                              [&](const auto &x) { return x.id == p.xmlFormulaCalendarId; });
        if (i == p.calendars.end())
            invalid("프로젝트 달력을 찾을 수 없습니다.");
        c = *i;
    }
    s.weekdays = c.weekdays;
    s.holidays = c.holidays;
    for (auto t : c.periods)
        s.periods.emplace_back(t.begin, t.end);
    return s;
}
XmlBaselineTime readTime(const std::map<QString, QString> &input) {
    auto raw = input;
    XmlBaselineTime t;
    for (const auto &[key, value] : raw)
        if (key != "Type" && key != "UID" && key != "Start" && key != "Finish" && key != "Unit" && key != "Value")
            invalid("미지원 시간분포 필드");
    t.type = number(raw["Type"], 75);
    t.uid = std::to_string(number(raw["UID"], 2147483647));
    if (known(raw["Start"]))
        t.start = normalizeFieldDateTime(str(raw["Start"].trimmed()));
    if (known(raw["Finish"]))
        t.finish = normalizeFieldDateTime(str(raw["Finish"].trimmed()));
    if (known(raw["Unit"]))
        t.unit = number(raw["Unit"], 8);
    if (raw.contains("Value")) {
        t.value = str(raw["Value"].trimmed());
        if (!xmlBaselineTimeCost(t.type) && known(raw["Value"]))
            xmlBaselineMinutes(*t.value, maxWork * 10000);
    }
    return t;
}
void validateCandidate(const XmlBaselineCandidate &candidate, const Project &p) {
    PlanInput probe;
    probe.currency = p.currency;
    auto source = metadata(p);
    source.sourceName = "검증";
    source.sourceSha256 = std::string(64, 'a');
    source.reporter = "검증";
    source.reason = "검증";
    source.recordedAt = "2026-01-01T00:00:00Z";
    source.rows = {candidate.row};
    probe.xmlBaselines = std::move(source);
    validateXmlBaselines(probe);
}
XmlBaselineCandidate readRow(QXmlStreamReader &reader, const Project &p, const QString &uid, const std::string &kind,
                             const std::string &id, const std::string &resource, std::set<int> &affected) {
    XmlBaselineCandidate candidate;
    auto &row = candidate.row;
    row.number = -1;
    row.kind = kind;
    row.id = id;
    row.resourceId = resource;
    row.xmlUid = str(uid);
    std::map<QString, QString> fields;
    std::vector<std::map<QString, QString>> times;
    QStringList errors;
    while (reader.readNextStartElement()) {
        auto name = reader.name().toString();
        if (name == "TimephasedData")
            times.push_back(scalars(reader, errors));
        else {
            auto text = scalarText(reader, errors);
            if (name == "Number")
                try {
                    affected.insert(number(text, 10));
                } catch (const AppError &) {
                }
            if (fields.contains(name))
                errors.append("중복 " + name);
            fields[name] = text;
        }
    }
    try {
        row.number = number(fields["Number"], 10);
        if (!errors.empty())
            invalid(str(errors.join(", ")));
        for (const auto &[key, value] : fields)
            if (key != "Number" && key != "Start" && key != "Finish" && key != "Duration" && key != "Work" &&
                key != "Cost" && key != "Interim" && key != "DurationFormat" && key != "EstimatedDuration" &&
                key != "BCWS" && key != "BCWP" && key != "FixedCost")
                invalid("미지원 필드 " + str(key));
        if (known(fields["Start"]))
            row.start = normalizeFieldDateTime(str(fields["Start"].trimmed()));
        if (known(fields["Finish"]))
            row.finish = normalizeFieldDateTime(str(fields["Finish"].trimmed()));
        if (known(fields["Duration"]))
            row.duration = xmlBaselineMinutes(str(fields["Duration"].trimmed()), 5000000LL * fieldNumberScale);
        if (known(fields["Work"]))
            row.work = xmlBaselineMinutes(str(fields["Work"].trimmed()), maxWork * 10000);
        if (known(fields["Cost"]))
            row.cost = xmlBaselineMoney(str(fields["Cost"].trimmed()), p.currency);
        for (auto key : {"Interim", "EstimatedDuration"})
            if (fields.contains(key)) {
                auto value = fields[key].trimmed();
                if (value == "true")
                    value = "1";
                if (value == "false")
                    value = "0";
                row.flags[key] = str(value);
            }
        if (known(fields["DurationFormat"]))
            row.flags["DurationFormat"] = std::to_string(number(fields["DurationFormat"], 99));
        for (auto key : {"BCWS", "BCWP", "FixedCost"})
            if (known(fields[key]))
                row.money[key] = xmlBaselineMoney(str(fields[key].trimmed()), p.currency);
        for (auto &raw : times)
            row.timephased.push_back(readTime(raw));
        if (kind.empty())
            invalid("현재 변환본의 객체에 연결할 수 없습니다.");
        validateCandidate(candidate, p);
    } catch (const AppError &e) {
        candidate.issue = issue(e);
    }
    return candidate;
}
ProjectXmlExchange originalPlan(const ProjectXmlExchange &input) {
    auto p = importProjectXml(input.xml, input.report["source"].toObject()["name"].toString());
    p.project.documentId = input.project.documentId;
    if (p.report.contains("documentId"))
        p.report["documentId"] = q(p.project.documentId);
    if (input.report.contains("xmlProgressAdoption")) {
        auto a = input.report["xmlProgressAdoption"].toObject();
        XmlProgressOptions options;
        options.statusAt = str(a["statusAt"].toString());
        options.reporter = str(a["reporter"].toString());
        options.reason = str(a["reason"].toString());
        options.recordedAt = str(a["recordedAt"].toString());
        options.confirmed = true;
        for (auto r : a["selected"].toArray())
            options.taskIds.push_back(str(r.toObject()["taskId"].toString()));
        p = adoptXmlProgress(p, options);
    }
    return p;
}
} // namespace
std::string xmlBaselineDurationText(std::int64_t minutes) {
    return "PT" + formatFieldNumber(minutes * 60) + "S";
}
std::vector<XmlBaselineCandidate> readXmlBaselines(const QByteArray &bytes, const Project &p,
                                                   const QJsonArray &mapping) {
    std::map<std::pair<std::string, std::string>, std::string> ids;
    for (auto v : mapping) {
        auto r = v.toObject();
        const auto nativeId = r.contains("ppmId") ? r["ppmId"] : r["jobsId"];
        ids[{str(r["kind"].toString()), str(r["xmlUid"].toString())}] = str(nativeId.toString());
    }
    QXmlStreamReader reader(bytes);
    std::vector<QString> path;
    std::vector<XmlBaselineCandidate> out;
    std::vector<XmlBaselineCandidate> outside;
    std::size_t periodCount = 0;
    QString uid;
    std::string task, resource;
    bool parent = false;
    while (!reader.atEnd()) {
        calculationCheckpoint();
        auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            auto name = reader.name().toString();
            if (path.size() == 2 &&
                ((path.back() == "Tasks" && name == "Task") || (path.back() == "Resources" && name == "Resource") ||
                 (path.back() == "Assignments" && name == "Assignment"))) {
                parent = true;
                uid.clear();
                task.clear();
                resource.clear();
            }
            if (parent && path.size() == 3 && (name == "UID" || name == "TaskUID" || name == "ResourceUID")) {
                auto text = QString::number(number(reader.readElementText(), 2147483647));
                if (name == "UID")
                    uid = text.trimmed();
                else if (name == "TaskUID")
                    task = str(text.trimmed());
                else
                    resource = str(text.trimmed());
                continue;
            }
            if (parent && path.size() == 3 && (name == "Baseline" || name == "TimephasedData")) {
                std::string kind = path.back() == "Task"       ? (uid == "0"                         ? "project"
                                                                  : ids.contains({"task", str(uid)}) ? "task"
                                                                                                     : "wbs")
                                   : path.back() == "Resource" ? "resource"
                                                               : "assignment";
                std::string id = kind == "project"      ? ""
                                 : kind == "assignment" ? ids[{"task", task}]
                                                        : ids[{kind, str(uid)}];
                const auto rid = kind == "assignment" ? ids[{"resource", resource}] : "";
                if ((kind != "project" && id.empty()) || (kind == "assignment" && rid.empty()))
                    kind.clear();
                if (name == "TimephasedData") {
                    QStringList errors;
                    auto raw = scalars(reader, errors);
                    std::optional<int> slot;
                    try {
                        const auto type = number(raw["Type"], 76);
                        for (const auto &scope : {"task", "resource", "assignment"})
                            if (auto n = xmlBaselineTimeNumber(type, scope))
                                slot = n;
                    } catch (const AppError &) {
                    }
                    if (!slot)
                        continue; // Actual/plan/other types belong to their existing readers.
                    if (++periodCount > 100000)
                        invalid("기준선 시간분포 개수 한도");
                    XmlBaselineCandidate candidate;
                    candidate.row.number = *slot;
                    candidate.row.kind = kind;
                    candidate.row.id = id;
                    candidate.row.resourceId = rid;
                    candidate.row.xmlUid = str(uid);
                    candidate.row.timephasedOutside = true;
                    try {
                        if (!errors.empty())
                            invalid(str(errors.join(", ")));
                        auto time = readTime(raw);
                        if (time.uid != str(uid))
                            invalid("시간분포 UID가 부모 객체와 다릅니다.");
                        candidate.row.timephased.push_back(std::move(time));
                        validateCandidate(candidate, p);
                    } catch (const AppError &e) {
                        candidate.issue = issue(e);
                    }
                    outside.push_back(std::move(candidate));
                    continue;
                }
                std::set<int> affected;
                auto candidate = readRow(reader, p, uid, kind, id, rid, affected);
                if (affected.empty())
                    out.push_back(std::move(candidate));
                else
                    for (int slot : affected) {
                        auto copy = candidate;
                        copy.row.number = slot;
                        out.push_back(std::move(copy));
                    }
                if (out.size() > 100000)
                    invalid("기준선 개수 한도");
                continue;
            }
            path.push_back(name);
        } else if (token == QXmlStreamReader::EndElement) {
            if (path.size() == 3)
                parent = false;
            if (!path.empty())
                path.pop_back();
        }
    }
    if (reader.hasError())
        invalid("기준선 XML 구문 오류: " + str(reader.errorString()));
    std::map<std::pair<int, std::string>, std::size_t> seen;
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto &r = out[i].row;
        auto key = std::pair{r.number, xmlBaselineRowKey(r.kind, r.id, r.resourceId)};
        if (!seen.emplace(key, i).second) {
            out[i].issue = "같은 객체/번호의 기준선이 중복됩니다.";
            out[seen.at(key)].issue = out[i].issue;
        }
    }
    for (auto &candidate : outside) {
        auto &r = candidate.row;
        const auto key = std::pair{r.number, xmlBaselineRowKey(r.kind, r.id, r.resourceId)};
        const auto found = seen.find(key);
        if (found == seen.end()) {
            seen.emplace(key, out.size());
            out.push_back(std::move(candidate));
            if (out.size() > 100000)
                invalid("기준선 개수 한도");
            continue;
        }
        auto &target = out[found->second];
        if (!target.row.timephasedOutside && !target.row.timephased.empty())
            target.issue = "같은 번호의 시간분포가 Baseline 내부와 부모 위치에 중복됩니다.";
        target.row.timephasedOutside = true;
        target.row.timephased.insert(target.row.timephased.end(), r.timephased.begin(), r.timephased.end());
        if (!candidate.issue.empty())
            target.issue = candidate.issue;
    }
    for (auto &candidate : out)
        if (candidate.row.timephasedOutside && candidate.issue.empty()) {
            try {
                validateCandidate(candidate, p);
                std::map<int, std::map<std::string, std::string>> intervals;
                for (const auto &t : candidate.row.timephased)
                    if (t.start && t.finish && *t.start < *t.finish) {
                        auto &values = intervals[t.type];
                        const auto after = values.lower_bound(*t.start);
                        if ((after != values.end() && after->first < *t.finish) ||
                            (after != values.begin() && std::prev(after)->second > *t.start))
                            invalid("같은 종류의 기준선 시간분포가 중복/겹칩니다.");
                        values.emplace(*t.start, *t.finish);
                    }
            } catch (const AppError &e) {
                candidate.issue = issue(e);
            }
        }
    return out;
}
ProjectXmlExchange adoptXmlBaselines(const ProjectXmlExchange &input, const XmlBaselineOptions &options) {
    if (!input.importing || !options.confirmed || options.numbers.empty() || !validUtcStamp(options.recordedAt) ||
        input.project.xmlBaselines || input.report.contains("xmlBaselineAdoption"))
        invalid("변경되지 않은 새 XML 사본에서 번호·작성자·사유·확인을 지정하세요.");
    auto original = originalPlan(input);
    if (original.project != input.project || original.report != input.report ||
        original.baselineCandidates != input.baselineCandidates)
        invalid("검토한 변환본/원본/후보가 변경되었습니다.");
    std::set<int> selected(options.numbers.begin(), options.numbers.end()), found;
    if (selected.size() != options.numbers.size() || *selected.begin() < 0 || *selected.rbegin() > 10)
        invalid("선택 번호 범위/중복 오류");
    auto output = input;
    auto source = metadata(output.project);
    source.sourceName = str(input.report["source"].toObject()["name"].toString());
    source.sourceSha256 = QCryptographicHash::hash(input.xml, QCryptographicHash::Sha256).toHex().toStdString();
    source.reporter = options.reporter;
    source.reason = options.reason;
    source.recordedAt = options.recordedAt;
    for (const auto &candidate : original.baselineCandidates)
        if (selected.contains(candidate.row.number)) {
            calculationCheckpoint();
            if (!candidate.issue.empty())
                invalid(candidate.issue);
            found.insert(candidate.row.number);
            source.rows.push_back(candidate.row);
        }
    if (found != selected)
        invalid("선택한 기준선 번호가 원본에 없습니다.");
    output.project.xmlBaselines = std::move(source);
    ++output.project.revision;
    validate(output.project);
    QJsonArray numbers;
    for (int n : options.numbers)
        numbers.append(n);
    output.report["xmlBaselineAdoption"] = QJsonObject{{"numbers", numbers},
                                                       {"reporter", q(options.reporter)},
                                                       {"reason", q(options.reason)},
                                                       {"recordedAt", q(options.recordedAt)},
                                                       {"rows", int(output.project.xmlBaselines->rows.size())}};
    output.report["revision"] = QString::number(output.project.revision);
    output.report["documentId"] = q(output.project.documentId);
    auto diagnostics = output.report["diagnostics"].toArray();
    diagnostics.append(QJsonObject{{"path", "/Project/Baselines"},
                                   {"message", "선택 번호의 외부 기준선 값을 새 사본에 채택했습니다. native 승인 "
                                               "이력과 별도 보존하며 채택 번호의 참조는 외부 명시 값을 사용합니다."}});
    output.report["diagnostics"] = diagnostics;
    return output;
}
void verifyXmlBaselineImport(const ProjectXmlExchange &input) {
    auto original = originalPlan(input);
    auto a = input.report["xmlBaselineAdoption"].toObject();
    XmlBaselineOptions options;
    options.reporter = str(a["reporter"].toString());
    options.reason = str(a["reason"].toString());
    options.recordedAt = str(a["recordedAt"].toString());
    options.confirmed = true;
    for (auto n : a["numbers"].toArray())
        options.numbers.push_back(n.toInt(-1));
    auto replay = adoptXmlBaselines(original, options);
    if (replay.project != input.project || replay.report != input.report ||
        replay.baselineCandidates != input.baselineCandidates)
        invalid("기준선 채택 결과가 원본 재검증과 다릅니다.");
}
XmlBaselineMergeSubmission prepareXmlBaselineMerge(const Project &target, const ProjectXmlExchange &input,
                                                   const XmlBaselineOptions &options,
                                                   const std::vector<XmlBaselineLink> &links, bool replaceExisting) {
    validate(target);
    // Reparse exact source bytes and compare the complete preview before preparing any edit.
    auto adopted = adoptXmlBaselines(input, options);
    XmlBaselineMergeSubmission result;
    result.incoming = *adopted.project.xmlBaselines;
    result.currency = input.project.currency;
    result.links = links;
    result.confirmed = options.confirmed;
    result.replaceExisting = replaceExisting;
    for (const auto &l : links)
        if (l.kind == "resource" && !l.exclude) {
            const auto from = std::find_if(input.project.resources.begin(), input.project.resources.end(),
                                           [&](const auto &r) { return r.id == l.sourceId; });
            const auto to = std::find_if(target.resources.begin(), target.resources.end(),
                                         [&](const auto &r) { return r.id == l.targetId; });
            if (from != input.project.resources.end() && to != target.resources.end() && from->type != to->type)
                invalid("공수 단위 보존을 위해 같은 종류의 자원을 연결하세요.");
        }
    validateXmlBaselineMergeTarget(target, result);
    return result;
}
XmlBaselineIndex indexXmlBaselines(const std::vector<XmlBaselineRow> &rows) {
    XmlBaselineIndex index;
    for (const auto &r : rows)
        index[xmlBaselineRowKey(r.kind, r.id, r.resourceId)].push_back(&r);
    return index;
}
namespace {
void writeTime(QXmlStreamWriter &w, const XmlBaselineTime &t, int uid) {
    const auto value = [&](const char *key, const std::string &s) { w.writeTextElement(key, q(s)); };
    w.writeStartElement("TimephasedData");
    value("Type", std::to_string(t.type));
    value("UID", std::to_string(uid));
    if (t.start)
        value("Start", *t.start);
    if (t.finish)
        value("Finish", *t.finish);
    if (t.unit)
        value("Unit", std::to_string(*t.unit));
    if (t.value)
        value("Value", *t.value);
    w.writeEndElement();
}
} // namespace
void writeXmlBaselineTimes(QXmlStreamWriter &w, const XmlBaselineIndex &rows, const std::string &kind,
                           const std::string &id, const std::string &resource, int uid) {
    const auto found = rows.find(xmlBaselineRowKey(kind, id, resource));
    if (found == rows.end())
        return;
    for (const auto *r : found->second)
        if (r->timephasedOutside)
            for (const auto &t : r->timephased) {
                calculationCheckpoint();
                writeTime(w, t, uid);
            }
}
void writeXmlBaselines(QXmlStreamWriter &w, const XmlBaselineIndex &rows, const std::string &kind,
                       const std::string &id, const std::string &resource, int uid, const std::string &currency) {
    const auto value = [&](const char *key, const std::string &s) { w.writeTextElement(key, q(s)); };
    const auto found = rows.find(xmlBaselineRowKey(kind, id, resource));
    if (found == rows.end())
        return;
    for (const auto *pointer : found->second) {
        const auto &r = *pointer;
        calculationCheckpoint();
        w.writeStartElement("Baseline");
        if (!r.timephasedOutside)
            for (const auto &t : r.timephased)
                writeTime(w, t, uid);
        value("Number", std::to_string(r.number));
        if (r.flags.contains("Interim"))
            value("Interim", r.flags.at("Interim"));
        if (r.start)
            value("Start", *r.start);
        if (r.finish)
            value("Finish", *r.finish);
        if (r.duration)
            value("Duration", xmlBaselineDurationText(*r.duration));
        if (r.flags.contains("DurationFormat"))
            value("DurationFormat", r.flags.at("DurationFormat"));
        if (r.flags.contains("EstimatedDuration"))
            value("EstimatedDuration", r.flags.at("EstimatedDuration"));
        if (r.work)
            value("Work", xmlBaselineDurationText(*r.work));
        if (r.cost)
            value("Cost", xmlBaselineMoneyText(*r.cost, currency));
        for (auto name : {"BCWS", "BCWP", "FixedCost"})
            if (r.money.contains(name))
                value(name, xmlBaselineMoneyText(r.money.at(name), currency));
        w.writeEndElement();
    }
}
std::vector<XmlBaselineRow> projectXmlBaselines(const Project &p, QJsonArray &diagnostics) {
    std::vector<XmlBaselineRow> rows;
    if (p.xmlBaselines)
        rows = p.xmlBaselines->rows;
    std::size_t periodCount = 0;
    std::set<std::string> taskIds, wbsIds, resourceIds, assignmentIds;
    std::map<std::string, std::string> parents;
    std::map<std::string, std::vector<std::string>> members;
    for (const auto &g : p.wbsNodes) {
        wbsIds.insert(g.id);
        parents[g.id] = g.parentId;
    }
    for (const auto &t : p.tasks) {
        taskIds.insert(t.id);
        auto parent = t.wbsId;
        int depth = 0;
        while (!parent.empty()) {
            if (++depth > 64 || !parents.contains(parent))
                invalid("WBS 연결 오류");
            members[parent].push_back(t.id);
            parent = parents.at(parent);
        }
    }
    for (const auto &r : p.resources)
        resourceIds.insert(r.id);
    for (const auto &a : p.assignments)
        assignmentIds.insert(xmlBaselineRowKey("assignment", a.taskId, a.resourceId));
    for (const auto &r : rows)
        if (r.kind == "project" || (r.kind == "task" && taskIds.contains(r.id)) ||
            (r.kind == "wbs" && wbsIds.contains(r.id)) || (r.kind == "resource" && resourceIds.contains(r.id)) ||
            (r.kind == "assignment" && assignmentIds.contains(xmlBaselineRowKey(r.kind, r.id, r.resourceId))))
            periodCount += r.timephased.size();
    std::set<int> external;
    for (const auto &r : rows)
        external.insert(r.number);
    for (const auto &b : p.baselines) {
        if (b.version > 11) {
            diagnostics.append(QJsonObject{
                {"path", "/Baseline"},
                {"message",
                 "native v12 이후 승인본은 표준 Baseline0~10에 들어가지 않으므로 source.ppm에 보존합니다."}});
            continue;
        }
        if (b.version < 1 || external.contains(int(b.version - 1)))
            continue;
        const int slot = int(b.version - 1);
        const auto first = rows.size();
        const auto missingTasks = std::count_if(b.input.tasks.begin(), b.input.tasks.end(),
                                                [&](const auto &t) { return !taskIds.contains(t.id); });
        const auto missingResources = std::count_if(b.input.resources.begin(), b.input.resources.end(),
                                                    [&](const auto &r) { return !resourceIds.contains(r.id); });
        const auto missingAssignments =
            std::count_if(b.input.assignments.begin(), b.input.assignments.end(), [&](const auto &a) {
                return !assignmentIds.contains(xmlBaselineRowKey("assignment", a.taskId, a.resourceId));
            });
        if (missingTasks || missingResources || missingAssignments)
            diagnostics.append(
                QJsonObject{{"path", "/Baseline/" + QString::number(slot)},
                            {"message", "현재 없는 native 기준선 객체는 XML 행으로 재생성하지 않습니다. 동결 전체 "
                                        "합계는 유지하고 세부 이력은 source.ppm에 보존합니다."},
                            {"omittedTasks", int(missingTasks)},
                            {"omittedResources", int(missingResources)},
                            {"omittedAssignments", int(missingAssignments)}});
        const auto values = baselineExchangeValues(p, slot);
        if (!values->baselineIssue.empty())
            invalid(values->baselineIssue);
        if (!values->baselineCostIssue.empty())
            diagnostics.append(QJsonObject{{"path", "/Baseline/Cost"}, {"message", q(values->baselineCostIssue)}});
        auto row = [&](const std::string &kind, const std::string &id, const std::string &rid = {}) {
            XmlBaselineRow r;
            r.number = slot;
            r.kind = kind;
            r.id = id;
            r.resourceId = rid;
            r.xmlUid = "native";
            return r;
        };
        for (const auto &t : p.tasks)
            if (values->baselineTasks.contains(t.id)) {
                auto r = row("task", t.id);
                auto dates = values->baselineTasks.at(t.id);
                r.start = dates.first;
                r.finish = dates.second;
                if (values->baselineDurations.contains(t.id))
                    r.duration = values->baselineDurations.at(t.id) * fieldNumberScale;
                if (values->baselineTaskWork.contains(t.id))
                    r.work = values->baselineTaskWork.at(t.id) * 10000;
                if (values->baselineTaskCost.contains(t.id))
                    r.cost = values->baselineTaskCost.at(t.id);
                rows.push_back(r);
            }
        auto project = row("project", "");
        project.start = values->baselineStart;
        project.finish = values->baselineFinish;
        if (project.start && project.finish && values->baselineDurationCalendar)
            project.duration = xmlCalendarDifference(*values->baselineDurationCalendar, FieldScope::Project, nullptr,
                                                     nullptr, "", *project.start, *project.finish);
        if (values->baselineWork)
            project.work = *values->baselineWork * 10000;
        project.cost = values->baselineCost;
        rows.push_back(project);
        for (const auto &g : p.wbsNodes) {
            auto r = row("wbs", g.id);
            bool dates = !members[g.id].empty(), work = dates, cost = dates;
            std::int64_t totalWork = 0, totalCost = 0;
            for (const auto &id : members[g.id]) {
                if (!values->baselineTasks.contains(id))
                    dates = false;
                else {
                    auto d = values->baselineTasks.at(id);
                    if (!r.start || d.first < *r.start)
                        r.start = d.first;
                    if (!r.finish || d.second > *r.finish)
                        r.finish = d.second;
                }
                if (!values->baselineTaskWork.contains(id))
                    work = false;
                else {
                    auto v = values->baselineTaskWork.at(id);
                    if (v > maxWork - totalWork)
                        invalid("요약 공수 합계 한도");
                    totalWork += v;
                }
                if (!values->baselineTaskCost.contains(id) || !values->baselineTaskCost.at(id))
                    cost = false;
                else {
                    auto v = *values->baselineTaskCost.at(id);
                    if (v > maxMoney - totalCost)
                        invalid("요약 원가 합계 한도");
                    totalCost += v;
                }
            }
            if (!dates) {
                r.start.reset();
                r.finish.reset();
            } else if (values->baselineDurationCalendar)
                r.duration = xmlCalendarDifference(*values->baselineDurationCalendar, FieldScope::Project, nullptr,
                                                   nullptr, "", *r.start, *r.finish);
            if (work)
                r.work = totalWork * 10000;
            if (cost)
                r.cost = totalCost;
            rows.push_back(r);
        }
        for (const auto &resource : p.resources)
            if (values->baselineResourceWork.contains(resource.id)) {
                auto r = row("resource", resource.id);
                r.work = values->baselineResourceWork.at(resource.id) * 10000;
                if (values->baselineResourceCost.contains(resource.id))
                    r.cost = values->baselineResourceCost.at(resource.id);
                rows.push_back(r);
            }
        for (const auto &a : p.assignments) {
            auto key = std::pair{a.taskId, a.resourceId};
            if (!values->baselineAssignmentWork.contains(key))
                continue;
            auto r = row("assignment", a.taskId, a.resourceId);
            r.work = values->baselineAssignmentWork.at(key) * 10000;
            if (values->baselineAssignmentCost.contains(key))
                r.cost = values->baselineAssignmentCost.at(key);
            if (values->baselineAssignmentDates.contains(key)) {
                auto dates = values->baselineAssignmentDates.at(key);
                r.start = dates.first;
                r.finish = dates.second;
            }
            rows.push_back(r);
        }
        addNativeBaselineTimephased(p, b, rows, first, periodCount, diagnostics);
        diagnostics.append(QJsonObject{
            {"path", "/Baseline/" + QString::number(slot)},
            {"message", "native 승인본의 명시 동결 날짜/기간·배정 공수·원가를 표준 Baseline으로 "
                        "투영합니다. 상세 시간분포는 표준 요소로 출력하고 전체 이력은 source.ppm에 보존합니다."}});
    }
    // Deleted objects remain in the native snapshot; they are not silently rebound to current XML IDs.
    std::erase_if(rows, [&](const auto &r) {
        bool present = r.kind == "project";
        if (r.kind == "task")
            present = taskIds.contains(r.id);
        if (r.kind == "wbs")
            present = wbsIds.contains(r.id);
        if (r.kind == "resource")
            present = resourceIds.contains(r.id);
        if (r.kind == "assignment")
            present = assignmentIds.contains(xmlBaselineRowKey(r.kind, r.id, r.resourceId));
        if (!present)
            diagnostics.append(
                QJsonObject{{"path", "/Baseline/" + q(r.id)},
                            {"message", "현재 삭제된 기준선 객체는 XML에 만들지 않고 source.ppm에 보존합니다."}});
        return !present;
    });
    return rows;
}
} // namespace ppm
