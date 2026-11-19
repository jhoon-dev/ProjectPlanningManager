#include "ui/OutcomeSummary.h"
#include "ui/TaskInput.h"
#include "calculation/WorkCalendar.h"
#include <QHeaderView>
#include <QTableWidget>
#include <set>
namespace ppm {
QTableWidget *outcomeTable(QWidget *parent, const char *name) {
    auto *table = new QTableWidget(0, 6, parent);
    table->setObjectName(name);
    table->setAccessibleName("완료일·편차·현재 적용 상태 비교");
    table->setHorizontalHeaderLabels(
        {"일정", "프로젝트 완료일", "기준선 대비", "조정 전 대비", "적용 / 검증 상태", "자원별 과부하 시간 합계"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const int widths[]{180, 155, 145, 145, 245, 205};
    for (int i = 0; i < 6; ++i)
        table->setColumnWidth(i, widths[i]);
    table->setMinimumHeight(180);
    table->setMaximumHeight(235);
    return table;
}
const Baseline *outcomeBaseline(const Project &p, const std::string &id) {
    for (const auto &b : p.baselines)
        if (b.id == id)
            return &b;
    return nullptr;
}
QString outcomeDelta(const PlanInput *input, const std::optional<std::string> &from,
                     const std::optional<std::string> &to) {
    if (!input || !from || !to)
        return "—";
    try {
        WorkCalendar calendar(*input, Task{});
        const auto minutes = calendar.offset(*to) - calendar.offset(*from);
        const auto sign = minutes > 0 ? "+" : minutes < 0 ? "−" : "";
        const auto amount = std::abs(minutes);
        return sign + taskDurationText(amount, taskDayMinutes(*input, Task{})) + " (" + sign +
               QString::number(double(amount) / 60, 'g', 10) + "h)";
    } catch (const std::exception &) {
        return "—";
    }
}
QString outcomeLoad(const ResourceLoad &load) {
    if (load.days.empty() && load.status != CalculationStatus::Valid)
        return "미정 · 유효한 부하 없음";
    std::int64_t total = 0;
    bool unknown = false;
    for (const auto &day : load.days) {
        total += day.overloadMinutes;
        unknown |= !day.capacity;
    }
    return (unknown ? "가용량 미정 · 알려진 구간 " : "") + QString::number(double(total) / 60, 'g', 10) + "h";
}
QString outcomeAdoption(const Project &p) {
    const auto &selection = p.remainingLeveling ? p.remainingLeveling : p.planLeveling;
    if (!selection)
        return "현재 적용: 자원 조정 채택 없음 · 논리 일정";
    return QString("현재 적용: ") + (p.remainingLeveling ? "잔여 예측" : "계획 (실적 반영 아님)") +
           " 자원 조정 채택 · " + QString::fromStdString(selection->reason);
}
QString outcomeCoverage(const Project &p) {
    if (!p.statusAt)
        return "전체 프로젝트 · 상태일 미설정 · 실적 반영 예측 없음";
    std::set<std::string> reported;
    for (const auto &entry : p.progress)
        if (entry.statusAt <= *p.statusAt)
            reported.insert(entry.taskId);
    return QString(
               "전체 프로젝트 · 상태일 %1 · 보고 범위 %2/%3 · 미보고는 착수 여부 미확인 (기간 기준: 계획 잔여 가정)")
        .arg(QString::fromStdString(*p.statusAt))
        .arg(reported.size())
        .arg(p.tasks.size());
}
std::optional<std::string> outcomeFinish(const ScheduleResult &result) {
    return result.status == CalculationStatus::Valid ? result.projectFinish : std::nullopt;
}
void outcomeRow(QTableWidget *table, const QString &name, const std::optional<std::string> &finish,
                const PlanInput *calendar, const std::optional<std::string> &baseline,
                const std::optional<std::string> &original, const QString &status, const QString &load) {
    const auto row = table->rowCount();
    table->insertRow(row);
    const QStringList values{name,
                             finish ? QString::fromStdString(*finish) : "—",
                             outcomeDelta(calendar, baseline, finish),
                             outcomeDelta(calendar, original, finish),
                             status,
                             load};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values[column]);
        item->setToolTip(values[column] + "\n편차: 선택 기준선 달력 근무시간 (+지연 / −앞당김). 기준선이 없으면 기준선 "
                                          "대비는 —, 조정 전 대비는 현재 기본 달력을 사용합니다.");
        table->setItem(row, column, item);
    }
}
} // namespace ppm
