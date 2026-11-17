#include "calculation/XmlProgressReports.h"
#include "domain/ProgressSource.h"
#include <map>
#include <algorithm>
namespace jobs {
void appendXmlProgressAudit(ProjectReport &report, const Project &p, const std::set<std::string> &included) {
    std::set<std::string> imported;
    for (const auto &e : p.progress)
        if (e.reason.starts_with(xmlProgressSourcePrefix) && included.contains(e.taskId))
            imported.insert(e.taskId);
    if (imported.empty())
        return;
    ReportTable t;
    t.id = "xmlProgressAudit";
    t.title = "XML 채택 작업의 진척 원장";
    t.family = ReportFamily::Schedule;
    t.note =
        "원본 해시/UID는 최초 채택 사유에 보존합니다. 후속 정정과 미래 상태일 보고도 포함한 전체 이력이며, "
        "현재 상태일에서 선택한 보고를 구별합니다. 실제 공수·원가 채택이나 원본 제품 재계산 검증을 뜻하지 않습니다.";
    t.columns = {{"작업 ID", 24},   {"보고 ID", 30},   {"상태일", 21},    {"실제 시작", 21},
                 {"실제 완료", 21}, {"실제 분", 14},   {"잔여 분", 14},   {"작성자", 18},
                 {"작성 UTC", 23},  {"이전 보고", 30}, {"현재 선택", 14}, {"출처/사유", 80}};
    const auto text = [](const std::string &s) { return ReportCell{s, CellFormat::Text}; };
    const bool work = std::any_of(p.progress.begin(), p.progress.end(),
                                  [&](const auto &e) {
                                      return imported.contains(e.taskId) && e.remainingWorkUnitMinutes.has_value();
                                  }) ||
                      std::any_of(p.tasks.begin(), p.tasks.end(), [&](const auto &task) {
                          return imported.contains(task.id) && isEffort(p) && usesWork(task);
                      });
    if (work) {
        t.columns.push_back({"보고 당시 계획 분", 20});
        t.columns.push_back({"잔여 공수 u", 18});
        t.note +=
            " 공수 기준 작업의 수신 잔여 분은 보고값이며 예측 기간은 명시 잔여 공수로 별도 계산합니다. 100u=1자원·분.";
    }
    std::map<std::string, const ProgressEntry *> latest;
    if (p.statusAt)
        for (const auto &e : p.progress) {
            calculationCheckpoint();
            if (e.statusAt > *p.statusAt || !imported.contains(e.taskId))
                continue;
            auto &old = latest[e.taskId];
            if (!old || old->statusAt <= e.statusAt)
                old = &e;
        }
    for (const auto &e : p.progress) {
        calculationCheckpoint();
        if (!imported.contains(e.taskId))
            continue;
        t.rows.push_back({text(e.taskId),
                          text(e.id),
                          text(e.statusAt),
                          text(e.actualStart.value_or("미설정")),
                          text(e.actualFinish.value_or("미설정")),
                          {e.actualDurationMinutes, CellFormat::Integer},
                          {e.remainingMinutes, CellFormat::Integer},
                          text(e.reporter),
                          text(e.recordedAt),
                          text(e.previousEntryId),
                          text(latest.contains(e.taskId) && latest.at(e.taskId) == &e ? "선택" : ""),
                          text(e.reason)});
        if (work) {
            const auto value = [](const std::optional<std::int64_t> &v) {
                return v ? ReportCell{*v, CellFormat::Integer} : ReportCell{{}, CellFormat::Integer};
            };
            t.rows.back().push_back(value(e.planDurationMinutes));
            t.rows.back().push_back(value(e.remainingWorkUnitMinutes));
        }
    }
    report.tables.push_back(std::move(t));
}
} // namespace jobs
