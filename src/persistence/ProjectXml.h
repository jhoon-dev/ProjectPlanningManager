#pragma once
#include "domain/Project.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace jobs {
struct XmlProgressCandidate {
    std::string xmlUid, taskId, name, issue;
    std::optional<std::string> actualStart, actualFinish;
    std::optional<std::int64_t> actualMinutes, remainingMinutes;
    bool present = false;
    std::optional<std::int64_t> remainingWorkUnitMinutes = {};
    std::string remainingWorkIssue = {};
    bool operator==(const XmlProgressCandidate &) const = default;
};
struct XmlProgressOptions {
    std::string statusAt, reporter, reason, recordedAt;
    std::vector<std::string> taskIds;
    bool confirmed = false;
};
struct XmlPeriodCandidate {
    std::string key, assignmentUid, taskUid, resourceUid, name, date, start, finish, issue, totalIssue, timedFinish;
    std::optional<std::int64_t> workUnitMinutes, actualMinor, taskActualMinor;
    std::size_t workParts = 0, costParts = 0;
    bool dateBucket = false;
    bool operator==(const XmlPeriodCandidate &) const = default;
};
// Raw actual intervals are retained separately. Multi-day totals are never automatically adopted.
struct XmlActualInterval {
    std::string key, assignmentUid, taskUid, resourceUid, name, start, finish, issue, totalIssue;
    std::optional<std::int64_t> workUnitMinutes, actualMinor, taskActualMinor;
    int type = 0, unit = -1;
    std::size_t sourceIndex = 0;
    bool multipleDays = false, wholeDays = false;
    bool operator==(const XmlActualInterval &) const = default;
};
struct XmlBaselineCandidate {
    XmlBaselineRow row;
    std::string issue;
    bool operator==(const XmlBaselineCandidate &) const = default;
};
struct XmlBaselineOptions {
    std::vector<int> numbers;
    std::string reporter, reason, recordedAt;
    bool confirmed = false;
};
// A prepared, immutable-by-convention preview. No filesystem or Document mutation.
struct ProjectXmlExchange {
    Project project;
    QByteArray xml;
    QJsonObject report;
    bool importing = true;
    std::vector<XmlProgressCandidate> progressCandidates = {};
    std::optional<std::string> progressStatusAt = {};
    std::string progressStatusIssue = {};
    std::vector<XmlPeriodCandidate> periodCandidates = {};
    std::string actualCurrency = {};
    bool exportingActuals = false;
    std::vector<XmlActualInterval> actualIntervals = {};
    std::vector<XmlBaselineCandidate> baselineCandidates = {};
};
QByteArray readProjectXml(const QString &path);
ProjectXmlExchange importProjectXml(const QByteArray &, const QString &sourceName);
ProjectXmlExchange exportProjectXml(const Project &);
ProjectXmlExchange exportProjectXmlActuals(const Project &);
// Explicit task-progress adoption into a fresh XML plan copy only; never merges a current native ledger.
ProjectXmlExchange adoptXmlProgress(const ProjectXmlExchange &, const XmlProgressOptions &);
ProjectXmlExchange adoptXmlBaselines(const ProjectXmlExchange &, const XmlBaselineOptions &);
XmlBaselineMergeSubmission prepareXmlBaselineMerge(const Project &, const ProjectXmlExchange &,
                                                   const XmlBaselineOptions &, const std::vector<XmlBaselineLink> &,
                                                   bool replaceExisting);
struct XmlProgressMapping {
    std::string xmlUid, taskId;
};
// Selected mappings replace the current observation by appending a correction.
// Unselected rows keep the current observation. Status must equal the native status.
// Ongoing work-driven targets additionally require explicit task RemainingWork.
// Their observed durations do not replace the native work-based plan or forecast.
std::vector<ProgressEntry> prepareXmlProgressMerge(const Project &, const ProjectXmlExchange &, XmlProgressOptions,
                                                   const std::vector<XmlProgressMapping> &);
struct XmlPeriodMapping {
    std::string key, taskId, resourceId;
};
std::vector<PeriodActualSubmission> prepareXmlPeriodMerge(const Project &, const ProjectXmlExchange &,
                                                          const XmlProgressOptions &,
                                                          const std::vector<XmlPeriodMapping> &);
// Create-only directory publication on the same filesystem. Returns the new directory.
QString publishProjectXml(const ProjectXmlExchange &, const QString &destination);
QString projectXmlPreview(const ProjectXmlExchange &);
} // namespace jobs
