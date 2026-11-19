#include "persistence/ProjectXml.h"
#include "domain/ProgressSource.h"
#include "domain/CalculationControl.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::XML_PROGRESS_INVALID, {message});
}
std::string hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}
std::int64_t civil(const std::string &stamp) {
    using namespace std::chrono;
    return sys_days{year{std::stoi(stamp.substr(0, 4))} / month{unsigned(std::stoi(stamp.substr(5, 2)))} /
                    day{unsigned(std::stoi(stamp.substr(8, 2)))}}
                   .time_since_epoch()
                   .count() *
               1440 +
           std::stoi(stamp.substr(11, 2)) * 60 + std::stoi(stamp.substr(14, 2));
}
} // namespace
ProjectXmlExchange adoptXmlProgress(const ProjectXmlExchange &input, const XmlProgressOptions &options) {
    calculationCheckpoint();
    if (!input.importing || !options.confirmed || !validStamp(options.statusAt) || !validUtcStamp(options.recordedAt) ||
        options.reporter.empty() || options.reporter.size() > 512 || options.reason.empty() ||
        options.reason.size() > 2048 || options.taskIds.empty())
        invalid("새 XML 사본·선택 작업·상태일·작성자·사유·확인을 지정하세요.");
    const auto sha = hash(input.xml);
    if (input.report["source"].toObject()["sha256"].toString().toStdString() != sha)
        invalid("검토한 XML 원본 해시가 변경되었습니다. 다시 읽으세요.");
    auto original = importProjectXml(input.xml, input.report["source"].toObject()["name"].toString());
    original.project.documentId = input.project.documentId;
    if (input.project != original.project || input.progressCandidates != original.progressCandidates ||
        input.progressStatusAt != original.progressStatusAt ||
        input.progressStatusIssue != original.progressStatusIssue || input.report.contains("xmlProgressAdoption"))
        invalid("변경되지 않은 새 계획 변환본에서만 채택합니다. 기존/이미 채택한 실적은 병합하거나 다시 추가하지 "
                "않습니다.");
    const std::set<std::string> selected(options.taskIds.begin(), options.taskIds.end());
    if (selected.size() != options.taskIds.size())
        invalid("선택 작업이 중복됩니다.");
    auto output = input;
    auto &p = output.project;
    std::map<std::string, const Task *> tasks;
    for (const auto &t : p.tasks)
        tasks.emplace(t.id, &t);
    p.statusAt = options.statusAt;
    ++p.revision;
    QJsonArray adopted;
    auto remaining = selected;
    for (const auto &row : original.progressCandidates) {
        calculationCheckpoint();
        if (!selected.contains(row.taskId))
            continue;
        remaining.erase(row.taskId);
        const auto fail = [&](const std::string &message) {
            invalid("UID " + row.xmlUid + " → " + row.taskId + ": " + message);
        };
        if (!row.present || !row.issue.empty() || !row.actualMinutes || !row.remainingMinutes)
            fail(row.issue.empty() ? "실제/잔여 기간의 명시 값이 필요합니다." : row.issue);
        if (!tasks.contains(row.taskId))
            fail("변환된 작업이 없습니다.");
        const auto *task = tasks.at(row.taskId);
        if ((row.actualStart && *row.actualStart > options.statusAt) ||
            (row.actualFinish &&
             (!row.actualStart || *row.actualFinish < *row.actualStart || *row.actualFinish > options.statusAt)) ||
            (!row.actualStart && *row.actualMinutes != 0) || (row.actualFinish && *row.remainingMinutes != 0) ||
            (row.actualStart && !row.actualFinish && *row.remainingMinutes == 0) ||
            (!row.actualStart && *row.remainingMinutes == 0 && task->durationMinutes.value_or(1) != 0))
            fail("실제 날짜·기간·완료/잔여 상태가 모순되거나 채택 상태일 이후입니다.");
        if (row.actualStart &&
            *row.actualMinutes > civil(row.actualFinish.value_or(options.statusAt)) - civil(*row.actualStart))
            fail("실제 기간이 보고된 실제 날짜 구간을 초과합니다.");
        ProgressEntry e;
        e.id = "xml-progress-" +
               hash(QByteArray::fromStdString(p.documentId + "/" + sha + "/" + row.xmlUid + "/" + options.statusAt))
                   .substr(0, 32);
        e.taskId = row.taskId;
        e.statusAt = options.statusAt;
        e.recordedAt = options.recordedAt;
        e.reporter = options.reporter;
        e.reason = std::string(xmlProgressSourcePrefix) + sha + " uid=" + row.xmlUid + "] " +
                   input.report["source"].toObject()["name"].toString().toStdString() + " · " + options.reason;
        e.sourceDocumentId = p.documentId;
        e.recordedRevision = p.revision;
        e.planDurationMinutes = task->durationMinutes;
        e.actualStart = row.actualStart;
        e.actualFinish = row.actualFinish;
        e.actualDurationMinutes = *row.actualMinutes;
        e.remainingMinutes = *row.remainingMinutes;
        adopted.append(QJsonObject{{"xmlUid", QString::fromStdString(row.xmlUid)},
                                   {"taskId", QString::fromStdString(row.taskId)},
                                   {"entryId", QString::fromStdString(e.id)}});
        p.progress.push_back(std::move(e));
    }
    if (!remaining.empty())
        invalid("선택한 작업 ID가 XML 진척 후보에 없습니다.");
    validate(p);
    output.report["xmlProgressAdoption"] =
        QJsonObject{{"mode", "new-copy-task-progress-v1"},
                    {"sourceSha256", QString::fromStdString(sha)},
                    {"statusAt", QString::fromStdString(options.statusAt)},
                    {"reporter", QString::fromStdString(options.reporter)},
                    {"reason", QString::fromStdString(options.reason)},
                    {"recordedAt", QString::fromStdString(options.recordedAt)},
                    {"xmlStatusAt", input.progressStatusAt ? QJsonValue(QString::fromStdString(*input.progressStatusAt))
                                                           : QJsonValue(QJsonValue::Null)},
                    {"selected", adopted}};
    output.report["revision"] = QString::number(p.revision);
    output.report["documentId"] = QString::fromStdString(p.documentId);
    auto diagnostics = output.report["diagnostics"].toArray();
    diagnostics.append(QJsonObject{
        {"path", "/Project/Tasks"},
        {"message",
         QString(
             "선택한 작업 진척 %1건을 새 사본 원장에 채택했습니다. 기존 원장 병합·실제 공수/원가 채택은 하지 않습니다.")
             .arg(adopted.size())}});
    output.report["diagnostics"] = diagnostics;
    return output;
}
std::vector<ProgressEntry> prepareXmlProgressMerge(const Project &target, const ProjectXmlExchange &input,
                                                   XmlProgressOptions options,
                                                   const std::vector<XmlProgressMapping> &mapping) {
    if (!target.statusAt || options.statusAt != *target.statusAt || mapping.empty())
        invalid("현재 문서의 상태일과 선택한 UID→작업 ID 연결이 필요합니다.");
    validate(target);
    options.taskIds.clear();
    std::map<std::string, const XmlProgressCandidate *> candidates;
    std::map<std::string, const Task *> tasks;
    std::map<std::string, std::string> previous;
    std::map<std::string, std::set<std::string>> origins;
    for (const auto &r : input.progressCandidates)
        candidates.emplace(r.xmlUid, &r);
    for (const auto &t : target.tasks)
        tasks.emplace(t.id, &t);
    for (const auto &e : target.progress) {
        calculationCheckpoint();
        previous[e.taskId] = e.id;
        if (e.statusAt == options.statusAt && e.reason.starts_with(xmlProgressSourcePrefix))
            origins[e.reason.substr(0, e.reason.find(']') + 1)].insert(e.taskId);
    }
    std::set<std::string> uids, targets;
    for (const auto &m : mapping) {
        calculationCheckpoint();
        if (!uids.insert(m.xmlUid).second || !targets.insert(m.taskId).second)
            invalid("선택한 UID 또는 대상 작업 ID가 중복됩니다.");
        if (!candidates.contains(m.xmlUid) || !tasks.contains(m.taskId))
            invalid("UID 또는 대상 작업이 없습니다.");
        options.taskIds.push_back(candidates.at(m.xmlUid)->taskId);
    }
    // Reparse the exact source and reuse the explicit actual/date consistency contract.
    const auto adopted = adoptXmlProgress(input, options);
    std::map<std::string, const ProgressEntry *> adoptedRows;
    for (const auto &e : adopted.project.progress)
        adoptedRows.emplace(e.taskId, &e);
    std::vector<ProgressEntry> entries;
    for (const auto &m : mapping) {
        calculationCheckpoint();
        auto e = *adoptedRows.at(candidates.at(m.xmlUid)->taskId);
        const auto marker = e.reason.substr(0, e.reason.find(']') + 1);
        if (const auto found = origins.find(marker); found != origins.end()) {
            if (found->second != std::set<std::string>{m.taskId})
                invalid("같은 출처 UID가 이 상태일에 다른 작업으로 이미 채택되었습니다.");
            continue;
        }
        e.taskId = m.taskId;
        e.id.clear();
        e.sourceDocumentId = target.documentId;
        e.recordedRevision = target.revision + 1;
        e.planDurationMinutes = tasks.at(m.taskId)->durationMinutes;
        const auto &task = *tasks.at(m.taskId);
        if (isEffort(target) && usesWork(task)) {
            const auto &candidate = *candidates.at(m.xmlUid);
            if (!candidate.remainingWorkIssue.empty())
                invalid(candidate.remainingWorkIssue);
            e.remainingWorkUnitMinutes = candidate.remainingWorkUnitMinutes;
            const bool zeroUnstarted =
                !e.actualStart && e.actualDurationMinutes == 0 && e.remainingMinutes == 0 && task.workUnitMinutes == 0;
            if ((!e.actualFinish &&
                 (!e.remainingWorkUnitMinutes || (*e.remainingWorkUnitMinutes == 0 && !zeroUnstarted))) ||
                (e.actualFinish && e.remainingWorkUnitMinutes.value_or(0) != 0))
                invalid("공수 기반 작업에는 명시 잔여 공수가 필요합니다. 진행 중에는 양수, 완료 시에는 미보고 또는 "
                        "명시0만 허용합니다: " +
                        m.taskId);
            // The nominal duration is not the duration of the current work-driven plan.
            // Document computes the plan snapshot once in the isolated batch worker.
            e.planDurationMinutes.reset();
        }
        e.previousEntryId = previous[m.taskId];
        entries.push_back(std::move(e));
    }
    return entries;
}
} // namespace ppm
