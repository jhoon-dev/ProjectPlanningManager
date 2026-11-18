#include "formatting/PlanTokens.h"
#include "ui/GanttView.h"
#include "calculation/Effort.h"
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTime>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QColor criticalColor(bool dark) {
    return ganttCriticalColor(dark);
}
QColor normalColor() {
    return QColor("#4997dd");
}
} // namespace
GanttView::GanttView(QWidget *parent) : QGraphicsView(parent) {
    setObjectName("ganttView");
    setAccessibleName("간트 일정 · 표와 같은 작업 선택");
    setScene(new QGraphicsScene(this));
    scene()->setItemIndexMethod(QGraphicsScene::NoIndex);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    // The date header is painted in viewport coordinates. Scene scroll blits must
    // not move cached task pixels into that fixed header.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { ensureVisibleRows(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (navigationChanged)
            navigationChanged();
    });
    rebuild();
}
qreal GanttView::position(const QString &stamp) const {
    const auto day = QDate::fromString(stamp.left(10), Qt::ISODate);
    const auto time = QTime::fromString(stamp.mid(11), "HH:mm");
    return (qreal(firstDay_.daysTo(day)) + qreal(time.hour() * 60 + time.minute()) / 1440) * dayWidth_;
}
void GanttView::setPlan(const Project *p, const ScheduleResult *result, const std::vector<std::size_t> &indices,
                        bool editable, std::shared_ptr<const Project> snapshot, std::uint64_t calculationGeneration) {
    EvaluationScope evaluation;
    cancelDrag();
    const bool valid = p && result && result->status == CalculationStatus::Valid &&
                       result->documentId == p->documentId && result->inputRevision == p->revision &&
                       result->profile == p->calculationProfile && result->projectFinish;
    const bool reusable = input_ && p && snapshot && inputSource_.lock() == snapshot && revision_ == p->revision;
    if (valid && reusable && calculationGeneration && renderedCalculation_ == calculationGeneration &&
        displayedIndices_ == indices) {
        const bool enabled = editable && !result->isForecast && !result->resourceLeveled;
        if (editable_ != enabled) {
            editable_ = enabled;
            rebuild();
        }
        return;
    }
    auto previousInput = std::move(input_);
    inputSource_ = snapshot;
    displayedIndices_ = indices;
    renderedCalculation_ = calculationGeneration;
    renderCache_ = std::make_shared<EvaluationCache>();
    editable_ = false;
    const auto x = horizontalScrollBar()->value(), y = verticalScrollBar()->value();
    rows_.clear();
    displayedFinish_.clear();
    rowIndex_.clear();
    dayCount_ = 0;
    holidays_.clear();
    displayRowCount_ = indices.size();
    isForecast_ = result && result->isForecast;
    resourceLeveled_ = result && result->resourceLeveled;
    if (valid) {
        // A weak source identity permits reuse without retaining the document's
        // COW state and forcing its next ordinary edit to copy the whole project.
        input_ = reusable ? std::move(previousInput) : std::make_shared<PlanInput>(*p);
        documentId_ = p->documentId;
        revision_ = p->revision;
        editable_ = editable && !isForecast_ && !resourceLeveled_;
        firstDay_ = QDate::fromString(qs(p->startDate), Qt::ISODate);
        const auto last = QDate::fromString(qs(*result->projectFinish).left(10), Qt::ISODate);
        planFirstDay_ = firstDay_;
        planLastDay_ = last;
        dayCount_ = std::max(7, int(firstDay_.daysTo(last)) + 2);
        for (const auto &day : p->holidays)
            holidays_.insert(QDate::fromString(qs(day), Qt::ISODate));
        std::unordered_map<std::string, const TaskSchedule *> dates;
        for (const auto &row : result->tasks)
            dates.emplace(row.taskId, &row);
        std::unordered_map<std::string, const TaskSchedule *> planned;
        for (const auto &row : result->plannedTasks)
            planned.emplace(row.taskId, &row);
        std::unordered_set<std::string> started;
        for (const auto &entry : p->progress)
            if (entry.actualStart)
                started.insert(entry.taskId);
        std::unordered_map<std::string, std::vector<ResourceOrderLink>> resourceLinks;
        for (const auto &link : result->resourceOrderLinks)
            resourceLinks[link.taskId].push_back(link);
        bool complete = true;
        for (auto index : indices) {
            const auto &t = p->tasks.at(index);
            auto it = dates.find(t.id);
            if (it == dates.end()) {
                complete = false;
                break;
            }
            const auto &s = *it->second;
            rows_.push_back({t.id, QString::number(index + 1) + ". " + qs(t.name), qs(s.start), qs(s.finish),
                             s.critical, t.predecessors, s.drivingPredecessors, s.remainingStart, s.completedActual});
            const auto *fact = p->statusAt ? progressAsOf(*p, t.id) : nullptr;
            rows_.back().progress = ganttProgress(result->isForecast, s.completedActual, fact, p->statusAt,
                                                  planned.contains(t.id) ? planned.at(t.id) : nullptr);
            rows_.back().policyIgnoredIncoming = result->isForecast && p->progressPolicy &&
                                                 p->progressPolicy->policy == overrideProgressPolicy && fact &&
                                                 fact->actualStart && !fact->actualFinish;
            rows_.back().edges = dependencies(t);
            rows_.back().calendar = qs(taskCalendarName(*p, t));
            auto &added = rows_.back();
            if (const auto resource = resourceLinks.find(t.id); resource != resourceLinks.end())
                added.resourceLinks = resource->second;
            if (hasScheduleFloat(*result, s))
                added.floatMinutes = {s.totalFloatMinutes, s.freeFloatMinutes};
            added.editable = !started.contains(t.id);
            added.constraintType = t.constraintType;
            added.constraintAt = t.constraintAt;
            added.deadlineAt = t.deadlineAt;
            if (isEffort(*p) && !s.completedActual && s.start != s.finish) {
                added.executionTask = result->isForecast ? pendingTask(*p, t) : t;
                added.executionDates = s;
            }
            if (t.deadlineAt) {
                const auto due = QDate::fromString(qs(*t.deadlineAt).left(10), Qt::ISODate);
                if (due >= planFirstDay_ && due <= planFirstDay_.addDays(366 * 20 - 1))
                    planLastDay_ = std::max(planLastDay_, due);
            }
            for (const auto &c : p->calendars)
                if (c.id == t.calendarId) {
                    rows_.back().calendar += " · 근무 요일 ";
                    for (int d : c.weekdays)
                        rows_.back().calendar += QString::number(d) + " ";
                    for (const auto &period : c.periods)
                        rows_.back().calendar += QString(" %1:%2–%3:%4")
                                                     .arg(period.begin / 60, 2, 10, QChar('0'))
                                                     .arg(period.begin % 60, 2, 10, QChar('0'))
                                                     .arg(period.end / 60, 2, 10, QChar('0'))
                                                     .arg(period.end % 60, 2, 10, QChar('0'));
                    rows_.back().calendar += " · 휴일 ";
                    for (const auto &date : c.holidays)
                        rows_.back().calendar += qs(date) + " ";
                }
        }
        if (!complete) {
            rows_.clear();
            dayCount_ = 0;
        }
    }
    for (std::size_t i = 0; i < rows_.size(); ++i)
        rowIndex_.emplace(rows_[i].id, i);
    for (const auto &row : rows_)
        displayedFinish_ = std::max(displayedFinish_, row.finish);
    rebuild();
    horizontalScrollBar()->setValue(x);
    verticalScrollBar()->setValue(y);
    ensureVisibleRows();
}
void GanttView::setBaseline(const Baseline *baseline) {
    std::vector<BaselineDate> dates;
    std::optional<PlanInput> calendar;
    if (baseline) {
        calendar.emplace();
        calendar->startDate = baseline->input.startDate;
        calendar->calculationProfile = baseline->input.calculationProfile;
        calendar->calendar = baseline->input.calendar;
        calendar->holidays = baseline->input.holidays;
        calendar->calendars = baseline->input.calendars;
        std::unordered_map<std::string, const Task *> tasks;
        for (const auto &task : baseline->input.tasks)
            tasks.emplace(task.id, &task);
        for (const auto &row : baseline->schedule) {
            Task task;
            if (const auto found = tasks.find(row.taskId); found != tasks.end()) {
                task.id = row.taskId;
                task.calendarId = found->second->calendarId;
                task.breaks = found->second->breaks;
            }
            dates.push_back({row.taskId, qs(row.start), qs(row.finish), std::move(task)});
        }
    }
    if (dates == baselineDates_ && calendar == baselineCalendar_)
        return;
    baselineDates_ = std::move(dates);
    baselineCalendar_ = std::move(calendar);
    baselineIndex_.clear();
    for (std::size_t i = 0; i < baselineDates_.size(); ++i)
        baselineIndex_.emplace(baselineDates_[i].id, i);
    rebuild();
}
void GanttView::setDayWidth(int width) {
    if (width != 16 && width != 64)
        return;
    auto day = qreal(planFirstDay_.daysTo(firstDay_)) + horizontalScrollBar()->value() / dayWidth_;
    fitted_ = false;
    dayWidth_ = width;
    rebuild();
    horizontalScrollBar()->setValue(int(day * dayWidth_));
}
void GanttView::setSelectedTask(const std::string &id) {
    const bool changed = selectedId_ != id;
    selectedId_ = id;
    if (changed && rows_.size() > 200) {
        const auto row = rowIndex_.find(id);
        if (row != rowIndex_.end()) {
            const auto y = HeaderHeight + int(row->second) * RowHeight;
            const auto top = verticalScrollBar()->value();
            if (y < top + HeaderHeight || y + RowHeight > top + viewport()->height())
                verticalScrollBar()->setValue(std::max(0, y - HeaderHeight));
        }
    }
    if (changed && !rebuilding_ && !chartSelection_)
        focusSelectedTask(false);
    if (!selectionBand_ && hasSchedule()) {
        selectionBand_ = scene()->addRect({}, Qt::NoPen);
        selectionBand_->setData(1, "selectionHighlight");
        selectionBand_->setAcceptedMouseButtons(Qt::NoButton);
        selectionBand_->setZValue(0);
    }
    if (selectionBand_) {
        const auto row = rowIndex_.find(id);
        selectionBand_->setVisible(row != rowIndex_.end());
        selectionBand_->setData(0, qs(id));
        if (row != rowIndex_.end()) {
            auto highlight = palette().color(QPalette::Highlight);
            highlight.setAlpha(45);
            selectionBand_->setBrush(highlight);
            selectionBand_->setRect(0, HeaderHeight + int(row->second) * RowHeight, sceneRect().width(), RowHeight);
        }
    }
    for (auto *item : scene()->items()) {
        if (item->data(1) != "label")
            continue;
        auto *label = dynamic_cast<QGraphicsSimpleTextItem *>(item);
        if (label) {
            auto font = label->font();
            font.setBold(item->data(0).toString() == qs(id));
            label->setFont(font);
        }
    }
    for (auto *item : scene()->items())
        if (item->data(1) == "resizeHandle")
            item->setVisible(item->data(0).toString() == qs(id));
    viewport()->update();
}
bool GanttView::hasSchedule() const {
    return dayCount_ > 0 && !rows_.empty();
}
void GanttView::focusSelectedTask(bool center) {
    const auto it = rowIndex_.find(selectedId_);
    if (!hasSchedule() || it == rowIndex_.end() || drag_)
        return;
    const auto &row = rows_[it->second];
    const auto begin = position(row.start), end = position(row.finish);
    const auto left = horizontalScrollBar()->value();
    const auto width = viewport()->width();
    if (center || begin < left + 16 || end > left + width - 16)
        horizontalScrollBar()->setValue(
            int((end - begin < width - 32 ? (begin + end) / 2 : begin + width / 2 - 16) - width / 2));
}
void GanttView::fitSchedule() {
    if (!hasSchedule())
        return;
    fitted_ = true;
    rebuild();
    horizontalScrollBar()->setValue(0);
    if (navigationChanged)
        navigationChanged();
}
QString GanttView::displayedFinish() const {
    return displayedFinish_;
}
bool GanttView::finishVisible() const {
    if (!hasSchedule() || displayedFinish_.isEmpty())
        return false;
    const auto at = position(displayedFinish_);
    return at >= horizontalScrollBar()->value() && at <= horizontalScrollBar()->value() + viewport()->width() - 8;
}
void GanttView::focusFinish() {
    if (hasSchedule() && !displayedFinish_.isEmpty())
        horizontalScrollBar()->setValue(int(position(displayedFinish_) - viewport()->width() * .75));
    if (navigationChanged)
        navigationChanged();
}
void GanttView::ensureVisibleRows() {
    if (rebuilding_ || rows_.size() <= 200 || !dayCount_)
        return;
    const int first = std::max(0, (verticalScrollBar()->value() - HeaderHeight) / RowHeight);
    const int last = std::min(int(rows_.size()) - 1, (verticalScrollBar()->value() + viewport()->height()) / RowHeight);
    if (first < renderedFirst_ || last > renderedLast_)
        rebuild();
}
void GanttView::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (fitted_ && hasSchedule())
        rebuild();
    else
        ensureVisibleRows();
    if (navigationChanged)
        navigationChanged();
}
void GanttView::rebuild() {
    if (rebuilding_)
        return;
    QScopedValueRollback<bool> guard(rebuilding_, true);
    EvaluationScope evaluation({}, renderCache_);
    renderedFirst_ = renderedLast_ = -1;
    cancelDrag();
    scene()->clear();
    selectionBand_ = nullptr;
    if (dayCount_ == 0) {
        auto *message =
            scene()->addText("일정 계산 후 간트가 표시됩니다.\n기간과 선행관계를 입력하고 F9로 계산하세요.");
        message->setDefaultTextColor(palette().color(QPalette::Text));
        message->setPos(12, 16);
        scene()->setSceneRect(0, 0, 380, std::max(120, HeaderHeight + int(displayRowCount_) * RowHeight));
        return;
    }
    firstDay_ = planFirstDay_;
    auto lastDay = planLastDay_;
    if (fitted_ && !rows_.empty()) {
        firstDay_ = QDate::fromString(rows_.front().start.left(10), Qt::ISODate);
        lastDay = firstDay_;
        for (const auto &row : rows_) {
            firstDay_ = std::min(firstDay_, QDate::fromString(row.start.left(10), Qt::ISODate));
            lastDay = std::max(lastDay, QDate::fromString(row.finish.left(10), Qt::ISODate));
        }
    }
    for (const auto &date : baselineDates_) {
        if (!rowIndex_.contains(date.id))
            continue;
        firstDay_ = std::min(firstDay_, QDate::fromString(date.start.left(10), Qt::ISODate));
        lastDay = std::max(lastDay, QDate::fromString(date.finish.left(10), Qt::ISODate));
    }
    dayCount_ = std::max(7, int(firstDay_.daysTo(lastDay)) + 2);
    if (fitted_)
        dayWidth_ = std::clamp(qreal(std::max(1, viewport()->width() - 20)) / dayCount_, qreal(.05), qreal(64));
    if (rows_.empty()) {
        auto *message = scene()->addText("선택한 WBS에 작업이 없습니다.");
        message->setDefaultTextColor(palette().color(QPalette::Text));
        message->setPos(12, HeaderHeight + 8);
    }
    const auto width = dayCount_ * dayWidth_;
    const auto height = HeaderHeight + int(rows_.size()) * RowHeight;
    scene()->setSceneRect(0, 0, width, std::max(height, 120));
    renderedFirst_ =
        rows_.size() > 200 ? std::max(0, (verticalScrollBar()->value() - HeaderHeight) / RowHeight - 4) : 0;
    renderedLast_ = rows_.size() > 200 ? std::min(int(rows_.size()) - 1,
                                                  (verticalScrollBar()->value() + viewport()->height()) / RowHeight + 4)
                                       : int(rows_.size()) - 1;
    for (int visible = renderedFirst_; visible <= renderedLast_; ++visible) {
        const auto i = std::size_t(visible);
        auto &row = rows_[i];
        if (!row.piecesReady && input_ && row.executionTask && row.executionDates) {
            const auto &pending = *row.executionTask;
            const auto &s = *row.executionDates;
            if (usesWork(pending)) {
                auto spans = executionSpans(*input_, pending, s);
                std::sort(spans.begin(), spans.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
                WorkCalendar clock(*input_);
                std::vector<std::pair<std::int64_t, std::int64_t>> merged;
                for (const auto &span : spans) {
                    if (!merged.empty() && span.start <= merged.back().second)
                        merged.back().second = std::max(merged.back().second, span.finish);
                    else
                        merged.emplace_back(span.start, span.finish);
                }
                for (const auto &[begin, end] : merged)
                    row.pieces.emplace_back(clock.boundary(begin, false), clock.boundary(end, true));
            } else
                row.pieces = WorkCalendar(*input_, pending).segments(s.remainingStart.value_or(s.start), s.finish);
            row.piecesReady = true;
        }
        const auto y = HeaderHeight + int(i) * RowHeight + RowHeight / 2;
        const auto start = position(row.start), finish = position(row.finish);
        if (baselineIndex_.contains(row.id)) {
            const auto &old = baselineDates_[baselineIndex_.at(row.id)];
            const auto x = position(old.start), end = position(old.finish);
            auto *oldBar = scene()->addRect(QRectF(x, y + 8, std::max(qreal(3), end - x), 3),
                                            QPen(QColor("#a0a0a0"), 1, Qt::DashLine), QBrush(QColor("#a0a0a0")));
            oldBar->setData(0, qs(row.id));
            oldBar->setData(1, "baseline");
            oldBar->setToolTip("고정 기준선\n" + old.start + " → " + old.finish);
            oldBar->setZValue(1);
        }
        QAbstractGraphicsShapeItem *bar;
        const bool milestone = row.start == row.finish;
        if (milestone) {
            QPolygonF diamond;
            diamond << QPointF(start, y - 7) << QPointF(start + 7, y) << QPointF(start, y + 7) << QPointF(start - 7, y);
            bar = scene()->addPolygon(diamond);
        } else if (input_ && isEffort(*input_) && !row.completedActual) {
            const auto first = row.remainingStart ? qs(*row.remainingStart) : row.start;
            QPainterPath path;
            if (isForecast_ && first > row.start)
                path.addRect(QRectF(start, y - 3, std::max(qreal(1), position(first) - start), 6));
            for (const auto &[begin, end] : row.pieces)
                path.addRect(QRectF(position(qs(begin)), y - 6,
                                    std::max(qreal(1), position(qs(end)) - position(qs(begin))), 12));
            bar = scene()->addPath(path);
            auto *connector = scene()->addLine(start, y, finish, y, QPen(normalColor(), 1, Qt::DotLine));
            connector->setData(0, qs(row.id));
            connector->setData(1, "splitConnector");
            connector->setToolTip("점선: 비근무/중단 구간 · 공수·부하에서 제외");
            connector->setZValue(1);
        } else
            bar = scene()->addRect(QRectF(start, y - 6, std::max(qreal(3), finish - start), 12));
        const bool critical = row.floatMinutes && row.critical && !row.completedActual;
        const bool dark = palette().color(QPalette::Base).lightness() < 128;
        bar->setBrush(QBrush(ganttFill(row.progress),
                             row.progress == GanttProgress::Unreported ? Qt::BDiagPattern : Qt::SolidPattern));
        bar->setPen(QPen(critical ? ganttCriticalColor(dark) : QColor("#334354"), critical ? 2.2 : 1.0));
        bar->setData(0, qs(row.id));
        bar->setData(1, milestone ? "milestone" : "task");
        bar->setZValue(2);
        const auto kind = row.completedActual ? "실제 완료"
                          : resourceLeveled_ ? (row.floatMinutes ? (critical ? "평준화 임계 · 계산 기준은 진단 참조"
                                                                             : "평준화 여유 · 계산 기준은 진단 참조")
                                                                 : "평준화 채택 · 여유/임계 미산정")
                          : isForecast_
                              ? (row.floatMinutes ? (critical ? "예측 임계 작업" : "예측 여유 작업") : "실적 반영 예측")
                          : !row.floatMinutes ? "여유/임계 미산정"
                          : critical          ? (milestone ? "임계 마일스톤" : "임계 작업")
                          : milestone         ? "마일스톤"
                                              : "여유 작업";
        const auto progressText = isForecast_ && row.progress == GanttProgress::Plan
                                      ? QString("시작 예정 · 현재 계획 시작이 상태일 이후")
                                      : ganttProgressText(row.progress);
        QString tooltip = row.name + "\n" + kind + " · " + progressText + "\n" + row.start + " → " + row.finish +
                          "\nID: " + qs(row.id);
        bool delayed = false;
        if (baselineCalendar_ && baselineIndex_.contains(row.id)) {
            const auto &old = baselineDates_[baselineIndex_.at(row.id)];
            const auto variance = old.task.id.empty()
                                      ? std::nullopt
                                      : ganttFinishVariance(*baselineCalendar_, old.task, old.finish.toStdString(),
                                                            row.finish.toStdString());
            delayed = variance && *variance > 0;
            tooltip += "\n선택 기준선 완료: " + old.finish + " · " +
                       (!variance ? "비교 미정"
                        : delayed ? "기준선보다 늦음 (기준선 작업 달력 근무분)"
                                  : "기준선보다 늦지 않음");
        } else
            tooltip += "\n기준선 비교 없음 · 지연 여부 미정";
        if (delayed) {
            QPolygonF flag;
            flag << QPointF(finish - 5, y - 12) << QPointF(finish + 5, y - 12) << QPointF(finish, y - 7);
            auto *marker = scene()->addPolygon(flag, QPen(ganttDelayColor()), QBrush(ganttDelayColor()));
            marker->setData(0, qs(row.id));
            marker->setData(1, "delayMarker");
            marker->setToolTip(tooltip);
            marker->setZValue(4);
        }
        if (row.floatMinutes)
            tooltip += QString("\n총여유 %1h · 자유여유 %2h (작업 달력 · %3)")
                           .arg(double(row.floatMinutes->first) / 60, 0, 'g', 12)
                           .arg(double(row.floatMinutes->second) / 60, 0, 'g', 12)
                           .arg(resourceLeveled_ ? "채택 자원 순서 기준" : "자원 용량 평준화 전");
        tooltip += "\n작업 달력: " + row.calendar +
                   "\n막대는 시작~완료 구간입니다. 비근무 시간은 기간/자원 공수에서 제외합니다.";
        tooltip += "\n제약: " + qs(constraintName(row.constraintType)) + " (" + qs(enumToken(row.constraintType)) +
                   ")" + (row.constraintAt ? " " + qs(*row.constraintAt) : QString{}) +
                   "\n마감: " + (row.deadlineAt ? qs(*row.deadlineAt) : "없음");
        tooltip += (editable_ && row.editable)
                       ? "\n드래그=날짜 이동 · 오른쪽 끝/Shift+드래그=기간 · 15분 격자 · Esc=취소 · 적용 후 자동 재계산"
                       : "\n현재 작업은 드래그할 수 없습니다 (읽기 전용/예측/실제 시작 이력).";
        if (row.remainingStart)
            tooltip += "\n잔여 작업 재개: " + qs(*row.remainingStart);
        int hidden = 0;
        for (const auto &prev : row.predecessors)
            if (!rowIndex_.contains(prev))
                ++hidden;
        if (hidden)
            tooltip += "\n현재 WBS 밖 선행: " + QString::number(hidden) + "개 (전체 프로젝트 계산에 반영됨)";
        bar->setToolTip(tooltip);
        if (editable_ && row.editable && !milestone) {
            auto *handle =
                scene()->addRect(QRectF(finish - 3, y - 8, 6, 16), QPen(QColor("#153b5a")), QBrush(QColor("#d6eafa")));
            handle->setData(0, qs(row.id));
            handle->setData(1, "resizeHandle");
            handle->setZValue(5);
            handle->setToolTip("오른쪽 끝 드래그: 작업 달력 근무 기간 변경 · 기존 제약 유지");
            handle->setVisible(row.id == selectedId_);
        }
        if (row.deadlineAt) {
            const auto x = position(qs(*row.deadlineAt));
            const auto color = row.finish > qs(*row.deadlineAt) ? QColor("#e66140") : QColor("#bd7d15");
            if (x >= 0 && x <= width) {
                auto *marker = scene()->addLine(x, y - 14, x, y + 13, QPen(color, 2, Qt::DashLine));
                marker->setData(0, qs(row.id));
                marker->setData(1, "deadline");
                marker->setToolTip("마감 " + qs(*row.deadlineAt) + " · 완료 " + row.finish +
                                   " · 경고 기준 (날짜 강제 이동 없음)");
                marker->setZValue(1);
            }
        }
        QStringList flags;
        if (row.progress != GanttProgress::Plan)
            flags << (row.progress == GanttProgress::Completed    ? "완료"
                      : row.progress == GanttProgress::InProgress ? "진행"
                      : row.progress == GanttProgress::NotStarted ? "미착수 보고"
                                                                  : "미보고");
        if (critical)
            flags << "임계";
        else if ((resourceLeveled_ || isForecast_) && !row.floatMinutes && !row.completedActual)
            flags << "여유 미정";
        if (delayed)
            flags << "기준선 지연";
        auto *label = scene()->addSimpleText(row.name + (milestone ? " ◆" : "") +
                                             (flags.isEmpty() ? "" : " [" + flags.join(" · ") + "]"));
        label->setBrush(palette().color(QPalette::Text));
        label->setPos(finish + 9, y - 9);
        label->setZValue(3);
        label->setData(0, qs(row.id));
        label->setData(1, "label");
        label->setToolTip(tooltip);
        for (const auto &resource : row.resourceLinks) {
            const auto it = rowIndex_.find(resource.predecessorId);
            if (it == rowIndex_.end())
                continue;
            const auto &prev = rows_[it->second];
            const auto x1 = position(resource.releaseAt.empty() ? prev.finish : qs(resource.releaseAt)),
                       target = !resource.acquireAt.empty() ? position(qs(resource.acquireAt))
                                : row.remainingStart        ? position(qs(*row.remainingStart))
                                                            : start;
            const auto y1 = HeaderHeight + int(it->second) * RowHeight + RowHeight / 2;
            QPainterPath path(QPointF(x1, y1));
            path.lineTo(x1 + 7, y1);
            path.lineTo(x1 + 7, y - 8);
            path.lineTo(target, y - 8);
            path.lineTo(target, y);
            auto *link = scene()->addPath(path, QPen(resource.driving && critical && prev.critical && prev.floatMinutes
                                                         ? criticalColor(dark)
                                                         : QColor("#8755a1"),
                                                     1.5, Qt::DashLine));
            link->setData(1, "resourceDependency");
            link->setData(0, qs(resource.predecessorId + "->" + row.id));
            link->setToolTip("자원 순서 " + qs(resource.resourceId) + " · " + QString::number(resource.units) + "% · " +
                             (resource.driving ? "driving" : "여유") +
                             (resource.releaseAt.empty()
                                  ? QString{}
                                  : "\n실행 구간 " + qs(resource.releaseAt) + " → " + qs(resource.acquireAt)));
            link->setZValue(1);
        }
        for (const auto &edge : row.edges) {
            const auto &predecessor = edge.predecessorId;
            auto found = rowIndex_.find(predecessor);
            if (found == rowIndex_.end())
                continue;
            const auto &prev = rows_[found->second];
            auto x1 = position(dependencyFromFinish(edge.type) ? prev.finish : prev.start);
            auto y1 = HeaderHeight + int(found->second) * RowHeight + RowHeight / 2;
            const auto target = dependencyToFinish(edge.type)       ? finish
                                : isForecast_ && row.remainingStart ? position(qs(*row.remainingStart))
                                                                    : start;
            auto elbow = std::max(x1 + 8, target - 10);
            QPainterPath path(QPointF(x1, y1));
            path.lineTo(elbow, y1);
            path.lineTo(elbow, y - 10);
            path.lineTo(target - 5, y - 10);
            path.lineTo(target - 5, y);
            path.lineTo(target, y);
            const bool driving = !row.policyIgnoredIncoming && critical && prev.critical && prev.floatMinutes &&
                                 std::find(row.driving.begin(), row.driving.end(), predecessor) != row.driving.end();
            const auto color = row.policyIgnoredIncoming ? QColor("#7b8d9b")
                               : driving                 ? criticalColor(dark)
                                                         : palette().color(QPalette::Text);
            const bool historical = isForecast_ && row.completedActual;
            auto *link = scene()->addPath(
                path, QPen(color, 1, (historical || row.policyIgnoredIncoming) ? Qt::DashLine : Qt::SolidLine));
            link->setZValue(1);
            link->setData(0, qs(predecessor + "->" + row.id));
            link->setData(1, "dependency");
            link->setToolTip(qs(enumToken(edge.type)) + (edge.lagMinutes >= 0 ? "+" : "") +
                             QString::number(edge.lagMinutes) + "m · 후행 달력 근무분 · " +
                             (row.policyIgnoredIncoming ? "채택 진척 정책 비활성 · "
                              : historical              ? "실제 날짜 고정 · "
                                                        : "") +
                             qs(predecessor) + " → " + qs(row.id));
            QPolygonF arrow;
            arrow << QPointF(target, y) << QPointF(target - 5, y - 3) << QPointF(target - 5, y + 3);
            scene()->addPolygon(arrow, QPen(color), QBrush(color))->setZValue(1);
        }
    }
    setSelectedTask(selectedId_);
}
void GanttView::drawBackground(QPainter *painter, const QRectF &rect) {
    painter->fillRect(rect, palette().brush(QPalette::Base));
    if (!dayCount_)
        return;
    auto first = std::max(0, int(rect.left() / dayWidth_));
    auto last = std::min(dayCount_ - 1, int(rect.right() / dayWidth_));
    for (int i = first; i <= last; ++i) {
        const auto d = firstDay_.addDays(i);
        const auto x = i * dayWidth_;
        if (dayWidth_ >= 6 && (d.dayOfWeek() > 5 || holidays_.contains(d)))
            painter->fillRect(QRectF(x, rect.top(), dayWidth_, rect.height()), QColor(128, 128, 128, 25));
        if (dayWidth_ >= 6 || (dayWidth_ >= 2 && d.day() == 1) ||
            (dayWidth_ < 2 && dayWidth_ >= .5 && d.day() == 1 && (d.month() - 1) % 3 == 0) ||
            (dayWidth_ < .5 && d.dayOfYear() == 1)) {
            painter->setPen(QPen(palette().color(QPalette::Midlight), 0));
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
    }
    painter->setPen(QPen(palette().color(QPalette::Midlight), 0));
    for (int y = HeaderHeight + std::max(0, int((rect.top() - HeaderHeight) / RowHeight)) * RowHeight;
         y <= rect.bottom(); y += RowHeight)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}
void GanttView::drawForeground(QPainter *painter, const QRectF &rect) {
    if (!dayCount_)
        return;
    const auto top = mapToScene(QPoint(0, 0)).y();
    auto opaqueBase = palette().color(QPalette::Base);
    opaqueBase.setAlpha(255);
    painter->fillRect(QRectF(rect.left(), top, rect.width(), HeaderHeight), opaqueBase);
    painter->fillRect(QRectF(rect.left(), top, rect.width(), HeaderHeight), palette().brush(QPalette::AlternateBase));
    painter->setPen(palette().color(QPalette::Text));
    auto first = std::max(0, int(rect.left() / dayWidth_));
    auto last = std::min(dayCount_ - 1, int(rect.right() / dayWidth_));
    const auto metrics = painter->fontMetrics();
    const int yearStep = std::max(1, int(std::ceil(64 / (365 * dayWidth_))));
    qreal labelRight = rect.left() - 1000;
    for (int i = first; i <= last; ++i) {
        const auto d = firstDay_.addDays(i);
        const auto x = i * dayWidth_;
        if (dayWidth_ >= 64 || (dayWidth_ >= 12 && d.dayOfWeek() == 1) ||
            (dayWidth_ < 12 && dayWidth_ >= 2 && d.day() == 1) ||
            (dayWidth_ < 2 && dayWidth_ >= .5 && d.day() == 1 && (d.month() - 1) % 3 == 0) ||
            (dayWidth_ < .5 && d.dayOfYear() == 1 && d.year() % yearStep == 0) || i == 0) {
            const auto label = dayWidth_ >= 64   ? d.toString("MM-dd")
                               : dayWidth_ >= 12 ? d.toString("yyyy-MM-dd")
                               : dayWidth_ >= 2  ? d.toString("yyyy-MM")
                               : dayWidth_ >= .5 ? QString("%1 Q%2").arg(d.year()).arg((d.month() - 1) / 3 + 1)
                                                 : d.toString("yyyy");
            const auto detail = dayWidth_ >= 12 ? d.toString("ddd") : QString{};
            const auto width = std::max(metrics.horizontalAdvance(label), metrics.horizontalAdvance(detail)) + 2;
            if (x + 3 < labelRight + 8)
                continue;
            painter->drawText(QRectF(x + 3, top + 3, width, 18), Qt::AlignLeft, label);
            painter->drawText(QRectF(x + 3, top + 23, width, 17), Qt::AlignLeft, detail);
            labelRight = x + 3 + width;
        }
    }
    painter->drawLine(QPointF(rect.left(), top + HeaderHeight), QPointF(rect.right(), top + HeaderHeight));
}
} // namespace jobs
