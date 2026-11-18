#include "reporting/SchedulePrint.h"
#include "reporting/SchedulePrintExtras.h"
#include "reporting/ExportFile.h"
#include "calculation/Effort.h"
#include "calculation/WorkCalendar.h"
#include "calculation/Tracking.h"
#include "formatting/DiagnosticText.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPdfWriter>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace jobs {
namespace {
[[noreturn]] void fail(const char *message) {
    throw std::runtime_error(message);
}
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QString meetingVariance(const PlanInput *calendar, const std::optional<std::string> &before,
                        const std::optional<std::string> &after) {
    if (!calendar || !before || !after)
        return "—";
    std::int64_t minutes = 0;
    try {
        WorkCalendar clock(*calendar, Task{});
        minutes = clock.offset(*after) - clock.offset(*before);
    } catch (const std::exception &) {
        return "—";
    }
    const auto sign = minutes > 0 ? "+" : minutes < 0 ? "−" : "";
    const auto amount = std::abs(minutes);
    const auto hours = sign + QString::number(double(amount) / 60, 'g', 12) + "h";
    return amount % 480 == 0 ? sign + QString::number(amount / 480) + "d (" + hours + ")" : hours;
}
QStringList wrap(const QString &text, const QFont &font, double width) {
    QStringList result;
    for (const auto &paragraph : text.split('\n')) {
        if (paragraph.isEmpty()) {
            result << QString{};
            continue;
        }
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(std::max(1.0, width));
            result << paragraph.mid(line.textStart(), line.textLength());
        }
        layout.endLayout();
    }
    return result.isEmpty() ? QStringList{QString{}} : result;
}
double civil(const QString &stamp) {
    const auto date = QDate::fromString(stamp.left(10), Qt::ISODate);
    if (!date.isValid() || stamp.size() != 16)
        fail("인쇄 일정 시각이 올바르지 않습니다.");
    return double(date.toJulianDay()) + (stamp.mid(11, 2).toInt() * 60 + stamp.mid(14, 2).toInt()) / 1440.0;
}
void paginate(SchedulePrintLayout &out, std::size_t begin, std::size_t end, bool provenance,
              const std::vector<std::vector<int>> &columnTiles, const std::vector<std::pair<QDate, QDate>> &timeTiles) {
    std::vector<std::vector<SchedulePrintFragment>> vertical(1);
    double used = 0;
    const int maxLines = std::max(1, int((out.bodyHeight - 6) / out.lineHeight));
    for (std::size_t r = begin; r < end; ++r) {
        calculationCheckpoint();
        const auto &row = out.rows[r];
        for (int line = 0; line < row.maxLines;) {
            auto count = std::min(maxLines, row.maxLines - line);
            auto height = count * out.lineHeight + 6;
            if (used + height > out.bodyHeight && !vertical.back().empty()) {
                vertical.emplace_back();
                used = 0;
            }
            vertical.back().push_back({r, line, count, height});
            used += height;
            line += count;
        }
    }
    for (int v = 0; v < int(vertical.size()); ++v)
        for (int c = 0; c < int(columnTiles.size()); ++c)
            for (int t = 0; t < int(timeTiles.size()); ++t) {
                calculationCheckpoint();
                if (out.pages.size() >= 2000)
                    fail("인쇄 페이지 한도(2,000)를 초과했습니다. 작업·기간·열 범위를 줄이세요.");
                out.pages.push_back({columnTiles[c], vertical[v], timeTiles[t].first, timeTiles[t].second, v + 1, c + 1,
                                     t + 1, provenance});
            }
}
QColor referenceColor(ScheduleBarKind kind) {
    return QColor(kind == ScheduleBarKind::Summary ? "#30574b" : "#777777");
}
} // namespace
SchedulePrintLayout buildSchedulePrint(const SchedulePrintSource &source, const SchedulePrintOptions &options,
                                       const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    if (!source.project || source.columns.size() < 2 || source.columns.size() > 128 || source.rows.size() > 10000 ||
        source.generatedAt.isEmpty() || source.printId.isEmpty() || !options.page.isValid() || options.fontPoints < 8 ||
        options.fontPoints > 12 || options.tablePercent < 40 || options.tablePercent > 70 || options.nameWidth < 80 ||
        options.nameWidth > 240 || options.daysPerPage < 1 || options.daysPerPage > 366 ||
        options.from.isValid() != options.to.isValid() ||
        (options.from.isValid() && (options.from > options.to || options.from.daysTo(options.to) > 7320)))
        fail("인쇄 범위 또는 페이지 설정을 확인하세요.");
    SchedulePrintLayout out;
    out.source = source;
    out.options = options;
    out.options.page.setMode(QPageLayout::StandardMode);
    out.content = out.options.page.paintRect(QPageLayout::Point);
    out.font = options.font;
#ifdef Q_OS_MACOS
    if (out.font.family().startsWith('.')) {
        const auto families = QFontDatabase::families();
        for (const auto &name : QStringList{"Arial", "Helvetica", "Noto Sans"})
            if (families.contains(name)) {
                out.font.setFamily(name);
                break;
            }
        if (out.font.family().startsWith('.'))
            fail("인쇄용 공개 글꼴을 찾지 못했습니다.");
    }
#endif
    out.font.setPixelSize(options.fontPoints);
    out.lineHeight = std::ceil(QFontMetricsF(out.font).height()) + 2;
    out.bodyTop = out.content.top() + (options.meetingSummary ? 136 : 96);
    out.bodyHeight = out.content.height() - (options.meetingSummary ? 172 : 132);
    out.tableWidth = std::floor(out.content.width() * options.tablePercent / 100);
    if (out.bodyHeight < out.lineHeight + 6 || out.content.width() - out.tableWidth < 100 || out.tableWidth < 150)
        fail("용지의 출력 영역이 너무 작습니다. 여백·글자·표 비율을 조정하세요.");
    out.columns = options.meetingSummary ? std::vector<SchedulePrintColumn>{{"number", "번호", 36},
                                                                            {"name", "작업", 110},
                                                                            {"finish", "현재 완료", 84},
                                                                            {"baselineFinish", "기준선 완료", 84},
                                                                            {"finishVariance", "완료 편차", 68},
                                                                            {"reportState", "상태일 보고", 64}}
                                         : source.columns;
    out.widths.resize(out.columns.size());
    out.widths[0] = 36;
    out.widths[1] = std::min(double(options.nameWidth), out.tableWidth - 76);
    const auto detailWidth = out.tableWidth - out.widths[0] - out.widths[1];
    std::vector<std::vector<int>> columnTiles{{0, 1}};
    double occupied = 0;
    for (int c = 2; c < int(out.columns.size()); ++c) {
        if (!std::isfinite(out.columns[c].width))
            fail("열 너비가 올바르지 않습니다.");
        out.widths[c] = std::clamp(out.columns[c].width, 40.0, detailWidth);
        if (occupied + out.widths[c] > detailWidth + 0.01 && columnTiles.back().size() > 2) {
            columnTiles.push_back({0, 1});
            occupied = 0;
        }
        columnTiles.back().push_back(c);
        occupied += out.widths[c];
    }
    const auto &p = *source.project;
    const auto result = calculate(p);
    out.status = result.status;
    out.mode = result.resourceLeveled ? (result.levelingFloatCalculated
                                             ? (result.calculationPolicy.ends_with("resource-material-float-v1")
                                                    ? "평준화 · 자재 입고·재고 제약 여유/임계"
                                                : result.calculationPolicy.ends_with("resource-repartition-float-v3")
                                                    ? "평준화 · 구간 재생성 여유/임계"
                                                    : "평준화 · 자원 순서 여유/임계")
                                             : "평준화 채택 · 여유/임계 미산정")
               : result.isForecast    ? "잔여 예측"
                                      : "현재 계획";
    if (result.status != CalculationStatus::Valid)
        out.mode += " · 계산 불완전/충돌 · 활성 날짜/막대 미표시";
    const auto plan = result.isForecast ? calculatePlan(p) : result;
    std::unordered_map<std::string, const Task *> tasks;
    std::unordered_map<std::string, const TaskSchedule *> active, planned, baseline;
    for (const auto &t : p.tasks)
        tasks.emplace(t.id, &t);
    if (result.status == CalculationStatus::Valid)
        for (const auto &s : result.tasks)
            active.emplace(s.taskId, &s);
    if (plan.status == CalculationStatus::Valid)
        for (const auto &s : plan.tasks)
            planned.emplace(s.taskId, &s);
    const Baseline *b = nullptr;
    std::unordered_map<std::string, const Task *> baselineTasks;
    if (options.baseline && !source.baselineId.isEmpty()) {
        for (const auto &candidate : p.baselines)
            if (qs(candidate.id) == source.baselineId)
                b = &candidate;
        if (!b)
            fail("선택한 기준선을 찾지 못했습니다.");
        verifyBaseline(*b);
        for (const auto &s : b->schedule)
            baseline.emplace(s.taskId, &s);
        for (const auto &task : b->input.tasks)
            baselineTasks.emplace(task.id, &task);
    }
    std::set<std::string> reported;
    if (p.statusAt)
        for (const auto &entry : p.progress)
            if (entry.statusAt <= *p.statusAt)
                reported.insert(entry.taskId);
    out.meetingContext = "상태일 " + (p.statusAt ? qs(*p.statusAt) : "미설정") + " · 기준선 " +
                         (b ? "v" + QString::number(b->version) + " " + qs(b->name) : "없음/미표시") +
                         QString(" · 전체 보고 %1/%2 · 미보고는 진척 미정").arg(reported.size()).arg(p.tasks.size());
    out.meetingResult =
        QString(p.remainingLeveling ? "잔여 예측 · 자원 조정 채택"
                : p.planLeveling    ? "계획 · 자원 조정 채택"
                : result.isForecast ? "잔여 예측 · 조정 전"
                                    : "현재 계획") +
        " · 전체 완료 " +
        (result.status == CalculationStatus::Valid && result.projectFinish ? qs(*result.projectFinish) : "미정") +
        " · 기준선 대비 " +
        meetingVariance(b ? &b->input : nullptr, b ? std::optional<std::string>{b->projectFinish} : std::nullopt,
                        result.status == CalculationStatus::Valid ? result.projectFinish : std::nullopt);
    QDate first, last;
    std::set<std::string> seen;
    qsizetype characters = 0;
    std::size_t segments = 0;
    auto extent = [&](const QString &start, const QString &finish) {
        const auto a = QDate::fromString(start.left(10), Qt::ISODate),
                   z = QDate::fromString(finish.left(10), Qt::ISODate);
        if (!first.isValid() || a < first)
            first = a;
        if (!last.isValid() || z > last)
            last = z;
    };
    for (const auto &input : source.rows) {
        calculationCheckpoint();
        if (!tasks.contains(input.taskId) || !seen.insert(input.taskId).second ||
            input.cells.size() != qsizetype(source.columns.size()))
            fail("인쇄 작업/열 스냅샷이 올바르지 않습니다.");
        if (options.selectedOnly && !source.selected.contains(input.taskId))
            continue;
        const auto &t = *tasks.at(input.taskId);
        const auto *s = active.contains(t.id) ? active.at(t.id) : nullptr;
        SchedulePrintRow row;
        row.taskId = t.id;
        row.cells = options.meetingSummary
                        ? QStringList{QString{}, QString{}, QString{}, QString{}, QString{}, QString{}}
                        : input.cells;
        row.cells[0] = QString::number(input.number);
        row.cells[1] = qs(t.name);
        for (int c = 2; c < row.cells.size(); ++c) {
            const auto &key = out.columns[c].key;
            if (key == "start")
                row.cells[c] = s ? qs(s->start) : "미정";
            if (key == "finish")
                row.cells[c] = s ? qs(s->finish) : "미정";
            if (key == "baselineFinish")
                row.cells[c] = baseline.contains(t.id) ? qs(baseline.at(t.id)->finish) : "—";
            if (key == "finishVariance")
                row.cells[c] = meetingVariance(
                    b ? &b->input : nullptr,
                    baseline.contains(t.id) ? std::optional<std::string>{baseline.at(t.id)->finish} : std::nullopt,
                    s ? std::optional<std::string>{s->finish} : std::nullopt);
            if (key == "reportState") {
                const auto *entry = progressAsOf(p, t.id);
                row.cells[c] = !p.statusAt           ? "상태일 없음"
                               : !entry              ? "미보고 · 미정"
                               : entry->actualFinish ? "완료 보고"
                                                     : "보고 있음";
            }
            if (key == "totalFloat")
                row.cells[c] = s && hasScheduleFloat(result, *s)
                                   ? QString::number(double(s->totalFloatMinutes) / 60, 'g', 12)
                                   : "—";
            if (key == "critical")
                row.cells[c] =
                    !s                   ? "미산정"
                    : s->completedActual ? "실제 완료"
                    : result.resourceLeveled
                        ? (hasScheduleFloat(result, *s) ? (s->critical ? "평준화 임계" : "평준화 여유") : "평준화 채택")
                    : hasScheduleFloat(result, *s) ? (s->critical ? "임계" : "여유")
                                                   : "미산정";
            if (key == "duration" && usesWork(t))
                row.cells[c] =
                    planned.contains(t.id)
                        ? QString::number(double(TaskExecution(p, t).duration(*planned.at(t.id))) / 60, 'g', 12)
                        : "미정";
        }
        if (s) {
            row.progress =
                ganttProgress(result.isForecast, s->completedActual, p.statusAt ? progressAsOf(p, t.id) : nullptr);
            row.critical = s->critical && hasScheduleFloat(result, *s) && !s->completedActual;
            if (b && baseline.contains(t.id) && baselineTasks.contains(t.id))
                row.finishVariance =
                    ganttFinishVariance(b->input, *baselineTasks.at(t.id), baseline.at(t.id)->finish, s->finish);
            extent(qs(s->start), qs(s->finish));
            auto kind = s->completedActual                            ? ScheduleBarKind::Actual
                        : s->critical && hasScheduleFloat(result, *s) ? ScheduleBarKind::Critical
                        : result.isForecast                           ? ScheduleBarKind::Remaining
                                                                      : ScheduleBarKind::Plan;
            auto start = s->remainingStart.value_or(s->start);
            if (s->completedActual || s->start == s->finish)
                row.bars.push_back({qs(s->start), qs(s->finish), kind});
            else {
                if (s->remainingStart && s->start < *s->remainingStart && p.statusAt)
                    row.bars.push_back(
                        {qs(s->start), qs(std::min(*s->remainingStart, *p.statusAt)), ScheduleBarKind::Actual});
                auto pending = result.isForecast ? pendingTask(p, t) : t;
                if (usesWork(pending)) {
                    auto spans = executionSpans(p, pending, *s);
                    WorkCalendar clock(p);
                    std::sort(spans.begin(), spans.end(),
                              [](const auto &a, const auto &z) { return a.start < z.start; });
                    std::vector<std::pair<std::int64_t, std::int64_t>> merged;
                    for (const auto &span : spans) {
                        if (!merged.empty() && span.start <= merged.back().second)
                            merged.back().second = std::max(merged.back().second, span.finish);
                        else
                            merged.emplace_back(span.start, span.finish);
                    }
                    for (const auto &[a, z] : merged)
                        row.bars.push_back({qs(clock.boundary(a, false)), qs(clock.boundary(z, true)), kind});
                } else
                    for (const auto &[a, z] : WorkCalendar(p, pending).segments(start, s->finish))
                        row.bars.push_back({qs(a), qs(z), kind});
            }
        }
        if (baseline.contains(t.id)) {
            const auto &base = *baseline.at(t.id);
            row.bars.push_back({qs(base.start), qs(base.finish), ScheduleBarKind::Baseline});
            extent(qs(base.start), qs(base.finish));
        }
        segments += row.bars.size();
        if (segments > 1000000)
            fail("인쇄 구간 한도(1,000,000)를 초과했습니다.");
        for (int c = 0; c < row.cells.size(); ++c) {
            characters += row.cells[c].size();
            if (characters > 16 * 1024 * 1024)
                fail("인쇄 텍스트 한도를 초과했습니다.");
            auto lines = wrap(row.cells[c], out.font, out.widths[c] - 6);
            row.maxLines = std::max(row.maxLines, int(lines.size()));
            row.lines.push_back(std::move(lines));
        }
        out.rows.push_back(std::move(row));
    }
    out.taskCount = out.rows.size();
    prepareScheduleExtras(out, result);
    const auto bodyRowCount = out.rows.size();
    if (!first.isValid())
        first = QDate::fromString(qs(p.startDate), Qt::ISODate);
    if (!last.isValid())
        last = first;
    if (options.from.isValid()) {
        first = options.from;
        last = options.to;
    }
    if (first.daysTo(last) > 7320)
        fail("인쇄 기간 한도(20년)를 초과했습니다.");
    out.options.from = first;
    out.options.to = last;
    std::vector<std::pair<QDate, QDate>> timeTiles;
    for (auto at = first; at <= last;) {
        auto end = options.fitTime ? last : std::min(last, at.addDays(options.daysPerPage - 1));
        timeTiles.emplace_back(at, end);
        at = end.addDays(1);
    }
    paginate(out, 0, bodyRowCount, false, columnTiles, timeTiles);
    std::vector<std::pair<QString, QString>> facts{
        {"프로젝트", qs(p.name)},
        {"문서 ID", qs(p.documentId)},
        {"revision", QString::number(p.revision)},
        {"저장 상태", source.unsaved ? "미저장 메모리 revision" : "저장된 원본"},
        {"범위", source.scope},
        {"출력 구성", options.meetingSummary
                          ? "회의용 핵심 열 · 완료 편차는 기준선 기본 달력 근무시간 (1d=8h, +지연/−앞당김)"
                          : "화면 표시 상세 열"},
        {"작업 수",
         QString::number(out.taskCount) + (options.selectedOnly ? " · 표시 작업 중 선택" : " · 표시 작업 전체")},
        {"일정", out.mode + " / " + qs(result.engineVersion) + " / " + qs(result.calculationPolicy)},
        {"출력 기간", first.toString(Qt::ISODate) + " ~ " + last.toString(Qt::ISODate)},
        {"시간축",
         options.fitTime ? "지정 기간 가로 한 장 맞춤" : QString::number(options.daysPerPage) + "일씩 가로 페이지"},
        {"상태일", p.statusAt ? qs(*p.statusAt) : "미설정"},
        {"기준선",
         b ? qs(b->name) + " / " + qs(b->id) + " / v" + QString::number(b->version) + " / " + qs(b->engineVersion)
           : "미표시"},
        {"생성 UTC", source.generatedAt},
        {"인쇄 ID", source.printId},
        {"출력 정책",
         "schedule-print-v1 · 원본 번호/작업명 반복 · 표시 열 가로 나눔 · 긴 행 이어짐 · PDF/인쇄 동일 배치"},
        {"범례", "파랑=계획/미착수 보고, 청록=진행 보고, 초록=완료/실제 날짜 경과 구간(관측 공수 아님), "
                 "회색 빗금=미보고(착수 여부 미확인), 굵은 테두리=임계, 주황 표시=선택 기준선보다 완료가 늦음 "
                 "(기준선 작업 달력 근무분), 회색 가는 막대=기준선, 점선=상태일, ◆=마일스톤. "
                 "기준선 없음/비교 불가는 지연 미정. 평준화 여유/임계 계산 기준은 출처·진단 참조."}};
    if (options.wbs)
        facts.emplace_back("WBS 요약",
                           QString::number(out.summaryCount) + " · 표시/선택 작업만 집계 · 기간/여유 합산 안 함");
    if (options.relations) {
        facts.emplace_back("관계선", QString::number(out.relations.size()) + " · 범위 밖/날짜 미산정 " +
                                         QString::number(out.omittedRelations));
        for (const auto &e : out.relations)
            facts.emplace_back("관계 참조", e.label + " / " + e.start + " → " + e.finish);
    }
    if (p.planLeveling || p.remainingLeveling) {
        const auto &s = p.planLeveling ? *p.planLeveling : *p.remainingLeveling;
        facts.emplace_back("평준화 채택",
                           qs(s.policy) + " / " + qs(s.priority) + " / " + qs(s.reason) + " / " + qs(s.adoptedAt));
    }
    if (options.meetingSummary) {
        facts.emplace_back("회의용 상태", out.meetingContext);
        facts.emplace_back("전체 결과", out.meetingResult);
    }
    for (std::size_t c = 0; c < out.columns.size(); ++c)
        facts.emplace_back("출력 열", out.columns[c].key + " / " + out.columns[c].title + " / " +
                                          QString::number(out.widths[c]) + "pt");
    for (const auto &d : result.diagnostics)
        facts.emplace_back(qs(d.code), qs(diagnosticMessage(d)));
    for (const auto &[key, value] : facts) {
        SchedulePrintRow row;
        row.cells = {key, value};
        row.lines = {wrap(key, out.font, 84), wrap(value, out.font, out.content.width() - 96)};
        row.maxLines = std::max(row.lines[0].size(), row.lines[1].size());
        out.rows.push_back(std::move(row));
    }
    paginate(out, bodyRowCount, out.rows.size(), true, {{0, 1}}, {{first, last}});
    return out;
}
void paintSchedulePage(QPainter &painter, const SchedulePrintLayout &layout, int index) {
    if (index < 0 || index >= int(layout.pages.size()))
        fail("인쇄 페이지 번호가 올바르지 않습니다.");
    const auto &page = layout.pages[index];
    const auto &box = layout.content;
    painter.save();
    painter.setFont(layout.font);
    painter.setPen(QColor("#152c40"));
    auto bold = layout.font;
    bold.setBold(true);
    bold.setPixelSize(std::min(12, layout.options.fontPoints + 2));
    painter.setFont(bold);
    painter.drawText(QRectF(box.left(), box.top(), box.width(), 18),
                     QFontMetricsF(bold).elidedText("PPM · " + QString::fromUtf8(layout.source.project->name) +
                                                        (page.provenance ? " · 인쇄 출처" : " · 일정표·간트"),
                                                    Qt::ElideRight, box.width()));
    auto caption = layout.font;
    caption.setPixelSize(9);
    painter.setFont(caption);
    painter.drawText(QRectF(box.left(), box.top() + 20, box.width(), 16),
                     QFontMetricsF(caption).elidedText("revision " + QString::number(layout.source.project->revision) +
                                                           " · " + layout.mode + " · " +
                                                           (layout.source.unsaved ? "미저장 메모리" : "저장된 원본"),
                                                       Qt::ElideRight, box.width()));
    painter.drawText(QRectF(box.left(), box.top() + 37, box.width(), 16),
                     "생성 UTC " + layout.source.generatedAt + " · " + QString::number(index + 1) + " / " +
                         QString::number(layout.pages.size()) + " 페이지");
    if (layout.options.meetingSummary) {
        painter.drawText(QRectF(box.left(), box.top() + 55, box.width(), 16),
                         QFontMetricsF(caption).elidedText(layout.meetingContext, Qt::ElideRight, box.width()));
        painter.drawText(QRectF(box.left(), box.top() + 74, box.width(), 16),
                         QFontMetricsF(caption).elidedText(layout.meetingResult, Qt::ElideRight, box.width()));
    }
    const auto footer = QString("행 %1 · 열 %2 · 기간 %3").arg(page.rowPage).arg(page.columnPage).arg(page.timePage);
    painter.drawText(QRectF(box.left(), box.bottom() - 24, box.width(), 12),
                     QFontMetricsF(layout.font)
                         .elidedText("문서 " + QString::fromUtf8(layout.source.project->documentId) + " · " + footer,
                                     Qt::ElideRight, box.width()));
    auto foot = layout.font;
    foot.setPixelSize(8);
    painter.setFont(foot);
    painter.drawText(QRectF(box.left(), box.bottom() - 11, box.width(), 12),
                     QFontMetricsF(foot).elidedText("파랑 계획/미착수 · 청록 진행 · 초록 완료/실제 날짜 · 빗금 미보고 "
                                                    "· 굵은 테두리 임계 · 주황 기준선 지연 · 범례 전문: 출처",
                                                    Qt::ElideRight, box.width()));
    painter.setFont(layout.font);
    double x = box.left();
    const auto top = box.top() + (layout.options.meetingSummary ? 96 : 56);
    std::vector<double> widths;
    for (const auto c : page.columns)
        widths.push_back(page.provenance ? (c == 0 ? 90 : box.width() - 90) : layout.widths[c]);
    if (!page.provenance) {
        painter.setFont(caption);
        double used = 0;
        for (double width : widths)
            used += width;
        widths.back() += layout.tableWidth - used;
    }
    for (std::size_t j = 0; j < page.columns.size(); ++j) {
        painter.fillRect(QRectF(x, top, widths[j], 40), QColor("#e9eff5"));
        painter.drawRect(QRectF(x, top, widths[j], 40));
        const auto title = page.provenance ? QString(j == 0 ? "항목" : "값") : layout.columns[page.columns[j]].title;
        auto lines = wrap(title, layout.font, widths[j] - 6);
        if (lines.size() > 2)
            lines[1] = QFontMetricsF(layout.font)
                           .elidedText(lines.mid(1).join(" ") + " (출처)", Qt::ElideRight, widths[j] - 6);
        painter.save();
        painter.setClipRect(QRectF(x + 2, top + 1, widths[j] - 4, 38));
        for (int l = 0; l < std::min(2, int(lines.size())); ++l)
            painter.drawText(QPointF(x + 3, top + 3 + l * layout.lineHeight + QFontMetricsF(layout.font).ascent()),
                             lines[l]);
        painter.restore();
        x += widths[j];
    }
    const auto chartLeft = box.left() + layout.tableWidth;
    const auto chartWidth = box.right() - chartLeft;
    const auto days = page.from.daysTo(page.to) + 1;
    auto position = [&](const QString &stamp) {
        return chartLeft + (civil(stamp) - double(page.from.toJulianDay())) / days * chartWidth;
    };
    if (!page.provenance) {
        painter.fillRect(QRectF(chartLeft, top, chartWidth, 40), QColor("#e9eff5"));
        painter.drawRect(QRectF(chartLeft, top, chartWidth, 40));
        painter.drawText(QRectF(chartLeft + 3, top + 2, chartWidth - 6, 15),
                         page.from.toString(Qt::ISODate) + " ~ " + page.to.toString(Qt::ISODate));
        const auto step = std::max(1, int(std::ceil(double(days) / std::max(1.0, chartWidth / 40))));
        for (int day = 0; day < days; day += step) {
            auto at = page.from.addDays(day);
            double xx = chartLeft + double(day) / days * chartWidth;
            painter.setPen(QColor("#d2d9df"));
            painter.drawLine(QPointF(xx, top + 20), QPointF(xx, layout.bodyTop));
            painter.setPen(QColor("#152c40"));
            painter.drawText(QRectF(xx + 2, top + 22, 40, 16), at.toString("MM-dd"));
        }
        painter.setFont(layout.font);
    }
    double y = layout.bodyTop;
    if (page.fragments.empty())
        painter.drawText(QRectF(box.left() + 4, y + 6, box.width() - 8, 20), "선택 범위에 작업이 없습니다.");
    for (const auto &fragment : page.fragments) {
        const auto &row = layout.rows[fragment.row];
        x = box.left();
        for (std::size_t j = 0; j < page.columns.size(); ++j) {
            const auto c = page.columns[j];
            const QRectF cell(x, y, widths[j], fragment.height);
            painter.fillRect(cell, fragment.row % 2 ? QColor("#f5f8fa") : Qt::white);
            painter.setPen(QColor("#c7d0d9"));
            painter.drawRect(cell);
            painter.save();
            painter.setClipRect(cell.adjusted(2, 1, -2, -1));
            painter.setPen(QColor("#152c40"));
            if (!page.provenance && c == 0)
                painter.drawText(QRectF(x + 3, y + 3, widths[j] - 6, layout.lineHeight),
                                 row.cells[0] + (fragment.line ? "+" : ""));
            else
                for (int line = 0; line < fragment.count; ++line) {
                    int at = fragment.line + line;
                    if (at < row.lines[c].size())
                        painter.drawText(
                            QPointF(x + 3, y + 3 + line * layout.lineHeight + QFontMetricsF(layout.font).ascent()),
                            row.lines[c][at]);
                }
            painter.restore();
            x += widths[j];
        }
        if (!page.provenance) {
            painter.save();
            painter.setClipRect(QRectF(chartLeft, y, chartWidth, fragment.height));
            painter.fillRect(QRectF(chartLeft, y, chartWidth, fragment.height),
                             QColor(fragment.row % 2 ? "#f5f8fa" : "#ffffff"));
            painter.setPen(QColor("#e2e7ed"));
            const auto step = std::max(1, int(std::ceil(double(days) / std::max(1.0, chartWidth / 40))));
            for (int d = 0; d < days; d += step) {
                const auto gx = chartLeft + double(d) / days * chartWidth;
                painter.drawLine(QPointF(gx, y), QPointF(gx, y + fragment.height));
            }
            const double center = y + std::min(16.0, fragment.height / 2);
            for (const auto &bar : row.bars) {
                double a = position(bar.start), z = position(bar.finish);
                const bool reference = bar.kind == ScheduleBarKind::Baseline || bar.kind == ScheduleBarKind::Summary;
                const auto c = reference                             ? referenceColor(bar.kind)
                               : bar.kind == ScheduleBarKind::Actual ? ganttFill(GanttProgress::Completed)
                                                                     : ganttFill(row.progress);
                painter.setPen(QPen(!reference && row.critical ? ganttCriticalColor() : c,
                                    !reference && row.critical ? 1.5 : 0.8));
                painter.setBrush(QBrush(c, !reference && row.progress == GanttProgress::Unreported ? Qt::BDiagPattern
                                                                                                   : Qt::SolidPattern));
                if (bar.start == bar.finish) {
                    if (a >= chartLeft && a <= box.right())
                        painter.drawPolygon(QPolygonF{QPointF(a, center - 4), QPointF(a + 4, center),
                                                      QPointF(a, center + 4), QPointF(a - 4, center)});
                } else if (z > chartLeft && a < box.right()) {
                    a = std::max(a, chartLeft);
                    z = std::min(z, box.right());
                    const bool base = bar.kind == ScheduleBarKind::Baseline;
                    painter.drawRect(QRectF(a, base ? std::min(center + 6, y + fragment.height - 3) : center - 4,
                                            std::max(0.3, z - a), base ? 2 : 8));
                }
            }
            if (row.finishVariance && *row.finishVariance > 0) {
                QString finish;
                for (const auto &bar : row.bars)
                    if (bar.kind != ScheduleBarKind::Baseline && bar.kind != ScheduleBarKind::Summary)
                        finish = std::max(finish, bar.finish);
                if (!finish.isEmpty()) {
                    const auto end = position(finish);
                    if (end >= chartLeft && end <= box.right()) {
                        painter.setPen(QPen(ganttDelayColor(), 0.5));
                        painter.setBrush(ganttDelayColor());
                        painter.drawPolygon(QPolygonF{QPointF(end - 3, center - 8), QPointF(end + 3, center - 8),
                                                      QPointF(end, center - 5)});
                    }
                }
            }
            painter.restore();
            painter.setPen(QColor("#c7d0d9"));
            painter.drawRect(QRectF(chartLeft, y, chartWidth, fragment.height));
        }
        y += fragment.height;
    }
    if (!page.provenance && layout.source.project->statusAt) {
        const auto xStatus = position(QString::fromUtf8(*layout.source.project->statusAt));
        if (xStatus >= chartLeft && xStatus <= box.right()) {
            painter.setPen(QPen(QColor("#a02a58"), 1, Qt::DashLine));
            painter.drawLine(QPointF(xStatus, top + 40), QPointF(xStatus, y));
        }
    }
    paintScheduleRelations(painter, layout, page);
    painter.restore();
}
int paintSchedule(QPagedPaintDevice &device, const SchedulePrintLayout &layout, int first, int last,
                  const std::shared_ptr<CalculationControl> &control, int copies, bool reverse, bool collate) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    if (!last)
        last = int(layout.pages.size());
    if (first < 1 || last < first || last > int(layout.pages.size()) || copies < 1 || copies > 99 ||
        (last - first + 1) * copies > 20000)
        fail("인쇄 페이지 범위를 확인하세요.");
    const auto actual = device.pageLayout().fullRect(QPageLayout::Point).size(),
               expected = layout.options.page.fullRect(QPageLayout::Point).size();
    if (std::abs(actual.width() - expected.width()) > 2 || std::abs(actual.height() - expected.height()) > 2)
        fail("프린터 용지가 미리보기와 다릅니다. 페이지 설정을 맞춘 후 다시 인쇄하세요.");
    QPainter painter(&device);
    if (!painter.isActive())
        fail("인쇄 장치를 열지 못했습니다.");
    std::vector<int> order;
    for (int i = first; i <= last; ++i)
        order.push_back(i);
    if (reverse)
        std::reverse(order.begin(), order.end());
    std::vector<int> pages;
    if (collate)
        for (int c = 0; c < copies; ++c)
            pages.insert(pages.end(), order.begin(), order.end());
    else
        for (int i : order)
            for (int c = 0; c < copies; ++c)
                pages.push_back(i);
    bool started = false;
    for (int page : pages) {
        calculationCheckpoint();
        if (started && !device.newPage())
            fail("다음 인쇄 페이지를 만들지 못했습니다.");
        started = true;
        painter.save();
        painter.scale(device.logicalDpiX() / 72.0, device.logicalDpiY() / 72.0);
        paintSchedulePage(painter, layout, page - 1);
        painter.restore();
    }
    if (!painter.end())
        fail("인쇄를 완료하지 못했습니다.");
    return int(pages.size());
}
int exportSchedulePdf(const SchedulePrintLayout &layout, const QString &path,
                      const std::shared_ptr<CalculationControl> &control) {
    ExportFile output(path, "pdf");
    int pages = 0;
    {
        QPdfWriter writer(&output.file);
        writer.setResolution(72);
        auto page = layout.options.page;
        page.setMode(QPageLayout::FullPageMode);
        writer.setPageLayout(page);
        writer.setTitle(QString::fromUtf8(layout.source.project->name) + " · 일정표·간트");
        writer.setCreator("PPM");
        pages = paintSchedule(writer, layout, 1, 0, control);
    }
    if (control && control->cancelRequested.load())
        throw CalculationCancelled{};
    output.commit();
    return pages;
}
} // namespace jobs
