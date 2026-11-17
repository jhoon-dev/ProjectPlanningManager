#include "persistence/ProjectXml.h"
#include "domain/CalculationControl.h"
#include "domain/PeriodActuals.h"
#include <QCryptographicHash>
#include <map>
#include <set>
#include <algorithm>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::XML_PROGRESS_INVALID, {s});
}
} // namespace
std::vector<PeriodActualSubmission> prepareXmlPeriodMerge(const Project &p, const ProjectXmlExchange &input,
                                                          const XmlProgressOptions &options,
                                                          const std::vector<XmlPeriodMapping> &mappings) {
    calculationCheckpoint();
    if (!p.statusAt || options.statusAt != *p.statusAt || !options.confirmed || mappings.empty() ||
        mappings.size() > 1000 || !validUtcStamp(options.recordedAt) ||
        QString::fromStdString(options.reporter).trimmed().isEmpty() || options.reporter.size() > 512 ||
        QString::fromStdString(options.reason).trimmed().isEmpty() || options.reason.size() > 2048)
        invalid("현재 상태일·작성자·사유·선택 배정과 채택 확인이 필요합니다.");
    validate(p);
    const auto digest = QCryptographicHash::hash(input.xml, QCryptographicHash::Sha256).toHex().toStdString();
    if (!input.importing || input.report["source"].toObject()["sha256"].toString().toStdString() != digest)
        invalid("XML 원본 해시가 변경되었습니다.");
    auto original = importProjectXml(input.xml, input.report["source"].toObject()["name"].toString());
    original.project.documentId = input.project.documentId;
    if (input.project != original.project || input.periodCandidates != original.periodCandidates ||
        input.actualCurrency != original.actualCurrency)
        invalid("변경되지 않은 XML 원본 검토본이 필요합니다.");
    std::map<std::string, const XmlPeriodCandidate *> candidates;
    std::map<std::string, const Resource *> resources;
    std::set<std::pair<std::string, std::string>> assignments;
    for (const auto &c : original.periodCandidates)
        candidates.emplace(c.key, &c);
    for (const auto &r : p.resources)
        resources.emplace(r.id, &r);
    for (const auto &a : p.assignments)
        assignments.emplace(a.taskId, a.resourceId);
    const auto current = periodActualsAsOf(p, *p.statusAt);
    using Pair = std::pair<std::string, std::string>;
    std::map<std::string, Pair> sourceAssignments;
    std::map<std::string, std::string> taskMap, resourceMap;
    std::set<std::string> selected;
    std::set<PeriodActualKey> keys;
    std::map<std::string, PeriodActualSubmission> batches;
    std::map<std::string, std::int64_t> sourceTotals;
    // Old source facts survive later manual corrections and native ID migration.
    std::map<std::string, std::set<Pair>> importedTargets;
    std::map<std::string, std::set<PeriodActualKey>> importedKeys;
    const auto prefix = "[XML-PERIOD sha256=" + digest + " assignment=";
    for (const auto &e : p.periodActuals) {
        calculationCheckpoint();
        if (e.statusAt != *p.statusAt || !e.reason.starts_with(prefix))
            continue;
        const auto end = e.reason.find(']', prefix.size());
        if (end == std::string::npos)
            continue;
        const auto uid = e.reason.substr(prefix.size(), end - prefix.size());
        importedTargets[uid].insert({e.taskId, e.resourceId});
        importedKeys[uid].insert(periodActualKey(e));
    }
    for (const auto &m : mappings) {
        calculationCheckpoint();
        if (!selected.insert(m.key).second || !candidates.contains(m.key))
            invalid("선택 행이 없거나 중복됩니다.");
        const auto &c = *candidates.at(m.key);
        if (!c.issue.empty() || (!c.workUnitMinutes && !c.actualMinor) || c.date > p.statusAt->substr(0, 10) ||
            (!c.timedFinish.empty() && c.timedFinish > *p.statusAt) || !resources.contains(m.resourceId) ||
            !assignments.contains({m.taskId, m.resourceId}) ||
            (resources.at(m.resourceId)->type != "labor" && resources.at(m.resourceId)->type != "equipment"))
            invalid("선택 행의 날짜 범위·일별 값·대상 노동/장비 배정을 확인하세요: " + m.key + " " + c.issue);
        const Pair target{m.taskId, m.resourceId};
        if (sourceAssignments.contains(c.assignmentUid) && sourceAssignments.at(c.assignmentUid) != target)
            invalid("같은 XML 배정을 서로 다른 native 배정에 연결할 수 없습니다.");
        sourceAssignments[c.assignmentUid] = target;
        if ((taskMap.contains(c.taskUid) && taskMap.at(c.taskUid) != m.taskId) ||
            (resourceMap.contains(c.resourceUid) && resourceMap.at(c.resourceUid) != m.resourceId))
            invalid("같은 원본 작업/자원 UID의 연결이 일관되지 않습니다.");
        taskMap[c.taskUid] = m.taskId;
        resourceMap[c.resourceUid] = m.resourceId;
        const PeriodActualKey key{m.taskId, m.resourceId, c.date};
        if (!keys.insert(key).second)
            invalid("여러 원본 행이 같은 배정·날짜에 중복 연결되었습니다.");
        if (importedTargets.contains(c.assignmentUid) && importedTargets.at(c.assignmentUid) != std::set<Pair>{target})
            invalid("이 원본 배정은 같은 상태일에 다른 native 배정으로 이미 채택되었습니다.");
        if (importedKeys[c.assignmentUid].contains(key))
            continue;
        if (c.actualMinor) {
            if (original.actualCurrency != p.currency || !c.taskActualMinor || !c.totalIssue.empty())
                invalid("일별 원가에는 같은 통화의 명시 작업 누계 AC가 필요합니다: " + c.totalIssue);
            if (sourceTotals.contains(m.taskId) && sourceTotals.at(m.taskId) != *c.taskActualMinor)
                invalid("같은 native 작업에 연결한 XML 누계 AC가 다릅니다.");
            sourceTotals[m.taskId] = *c.taskActualMinor;
        }
        auto &batch = batches[c.assignmentUid];
        batch.reporter = options.reporter;
        batch.recordedAt = options.recordedAt;
        batch.reason = prefix + c.assignmentUid + "] " +
                       input.report["source"].toObject()["name"].toString().toStdString() + " · " + options.reason;
        PeriodActualEntry e;
        e.taskId = m.taskId;
        e.resourceId = m.resourceId;
        e.date = c.date;
        const auto old = current.contains(key) ? current.at(key) : nullptr;
        e.workUnitMinutes = c.workUnitMinutes ? c.workUnitMinutes : old ? old->workUnitMinutes : std::nullopt;
        e.actualMinor = c.actualMinor ? c.actualMinor : old ? old->actualMinor : std::nullopt;
        batch.entries.push_back(std::move(e));
    }
    std::vector<PeriodActualSubmission> result;
    for (auto &[uid, batch] : batches) {
        const auto &task = batch.entries.front().taskId;
        const bool hasCost = std::any_of(batch.entries.begin(), batch.entries.end(),
                                         [](const auto &e) { return e.actualMinor.has_value(); });
        if (hasCost) {
            std::optional<std::int64_t> total;
            if (sourceTotals.contains(task))
                total = sourceTotals.at(task);
            else if (const auto *old = costAsOf(p, task, *p.statusAt))
                total = old->actualMinor;
            batch.totals.push_back({task, total});
        }
        result.push_back(std::move(batch));
    }
    return result;
}
} // namespace jobs
