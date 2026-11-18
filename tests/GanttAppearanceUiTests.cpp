#include "persistence/JsonProjectStore.h"
#include "reporting/GanttStyle.h"
#include "reporting/SchedulePrint.h"
#include "ui/GanttView.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <numeric>

namespace {
QGraphicsItem *item(jobs::GanttView &view, const std::string &id, const char *kind = "task") {
    for (auto *candidate : view.scene()->items())
        if (candidate->data(0).toString().toStdString() == id && candidate->data(1) == kind)
            return candidate;
    return nullptr;
}
QAbstractGraphicsShapeItem *bar(jobs::GanttView &view, const std::string &id) {
    return dynamic_cast<QAbstractGraphicsShapeItem *>(item(view, id));
}
std::vector<std::size_t> indices(const jobs::Project &p) {
    std::vector<std::size_t> rows(p.tasks.size());
    std::iota(rows.begin(), rows.end(), 0);
    return rows;
}
void capture(QWidget &widget, const QString &name) {
    const auto folder = qEnvironmentVariable("JOBS_GANTT_AUDIT_DIR");
    if (folder.isEmpty())
        return;
    QVERIFY(QDir().mkpath(folder));
    QTest::qWait(80);
    QVERIFY(widget.grab().save(QDir(folder).filePath(name + ".png")));
}
std::string fixture(jobs::Document &doc) {
    doc.create("간트 상태", "2026-11-09", {"완료", "진행", "미착수 보고", "미보고", "여유 경로", "마일스톤"});
    for (int i = 0; i < 6; ++i)
        doc.setDuration(i, i == 1 ? 960 : i == 4 ? 60 : i == 5 ? 0 : 480);
    for (int i = 1; i < 4; ++i)
        doc.setPredecessors(i, {doc.project()->tasks[i - 1].id});
    const auto baseline = doc.captureBaseline("승인", "PM", "검토", "2026-11-18T00:00:00Z");
    const auto record = [&](int row, bool started, bool completed, int remaining) {
        jobs::ProgressEntry e;
        e.taskId = doc.project()->tasks[row].id;
        if (started)
            e.actualStart = row == 0 ? "2026-11-09 09:00" : row == 4 ? "2026-11-20 09:00" : "2026-11-10 09:00";
        if (completed)
            e.actualFinish = "2026-11-09 18:00";
        e.actualDurationMinutes = started ? 480 : 0;
        e.remainingMinutes = remaining;
        e.reporter = "PM";
        e.reason = "현장 확인";
        e.recordedAt = "2026-11-20T12:00:00Z";
        doc.recordProgress(e);
    };
    doc.setStatusAt("2026-11-20 18:00");
    record(4, true, false, 60);
    doc.setStatusAt("2026-11-10 18:00");
    record(0, true, true, 0);
    record(1, true, false, 1440);
    record(2, false, false, 480);
    return baseline;
}
} // namespace
class GanttAppearanceUiTests : public QObject {
    Q_OBJECT
private slots:
    void reportingStatesSelectionAndBaselineStayIndependent() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        fixture(doc);
        const auto original = *doc.project();
        auto result = jobs::calculate(original);
        QCOMPARE(result.status, jobs::CalculationStatus::Valid);
        jobs::GanttView view;
        view.resize(1200, 500);
        view.setPlan(&original, &result, indices(original));
        view.setBaseline(&original.baselines[0]);
        view.show();
        view.fitSchedule();
        const auto &tasks = original.tasks;
        for (int i = 0; i < 5; ++i)
            QVERIFY(bar(view, tasks[i].id));
        QCOMPARE(bar(view, tasks[0].id)->brush().color(), jobs::ganttFill(jobs::GanttProgress::Completed));
        QCOMPARE(bar(view, tasks[1].id)->brush().color(), jobs::ganttFill(jobs::GanttProgress::InProgress));
        QCOMPARE(bar(view, tasks[2].id)->brush().color(), jobs::ganttFill(jobs::GanttProgress::NotStarted));
        QCOMPARE(bar(view, tasks[3].id)->brush().style(), Qt::SolidPattern);
        QVERIFY(!bar(view, tasks[3].id)->toolTip().contains(jobs::ganttProgressText(jobs::GanttProgress::Unreported)));
        QCOMPARE(bar(view, tasks[4].id)->brush().style(), Qt::BDiagPattern);
        QVERIFY(bar(view, tasks[4].id)->toolTip().contains("미보고 · 착수 여부 미확인"));
        QVERIFY(item(view, tasks[5].id, "milestone"));
        QVERIFY(item(view, tasks[3].id, "delayMarker"));
        QVERIFY(!item(view, tasks[0].id, "delayMarker"));
        const auto criticalPen = bar(view, tasks[1].id)->pen();
        QVERIFY(criticalPen.widthF() > bar(view, tasks[4].id)->pen().widthF());
        view.setSelectedTask(tasks[1].id);
        QCOMPARE(bar(view, tasks[1].id)->pen(), criticalPen);
        QVERIFY(item(view, tasks[1].id, "selectionHighlight")->isVisible());
        capture(view, "mixed-status");
        const auto marker = item(view, tasks[3].id, "delayMarker");
        std::string selected;
        view.taskSelected = [&](const auto &id) { selected = id; };
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          view.mapFromScene(marker->sceneBoundingRect().center()));
        QCOMPARE(selected, tasks[3].id);
        view.setBaseline(nullptr);
        QVERIFY(!item(view, tasks[3].id, "delayMarker"));
        QVERIFY(bar(view, tasks[3].id)->toolTip().contains("지연 여부 미정"));
        auto unknownFloat = result;
        unknownFloat.resourceLeveled = true;
        unknownFloat.levelingFloatCalculated = false;
        view.setPlan(&original, &unknownFloat, indices(original));
        QCOMPARE(bar(view, tasks[1].id)->pen().widthF(), 1.0);
        QVERIFY(bar(view, tasks[1].id)->toolTip().contains("여유/임계 미산정"));
        auto plan = jobs::calculatePlan(original);
        auto unknownLogicalFloat = plan;
        unknownLogicalFloat.logicalFloatCalculated = false;
        view.setPlan(&original, &unknownLogicalFloat, indices(original));
        QCOMPARE(bar(view, tasks[1].id)->pen().widthF(), 1.0);
        QVERIFY(bar(view, tasks[1].id)->toolTip().contains("여유/임계 미산정"));
        QVERIFY(!bar(view, tasks[1].id)->toolTip().contains("임계 작업"));
        view.setPlan(&original, &plan, indices(original));
        QCOMPARE(bar(view, tasks[4].id)->brush().style(), Qt::SolidPattern);
        QVERIFY(bar(view, tasks[4].id)->toolTip().contains("실적 상태 미반영"));
        result.status = jobs::CalculationStatus::Stale;
        view.setPlan(&original, &result, indices(original));
        QVERIFY(!view.hasSchedule() && !bar(view, tasks[1].id));
        QVERIFY(*doc.project() == original);
    }
    void futureReportingUsesCurrentPlan_data() {
        QTest::addColumn<QString>("profile");
        QTest::newRow("fs0") << QString("native-fs0-workminutes-v1");
        QTest::newRow("pdm") << QString(jobs::pdmProfile);
        QTest::newRow("constraints") << QString(jobs::constraintProfile);
        QTest::newRow("effort") << QString(jobs::effortProfile);
    }
    void futureReportingUsesCurrentPlan() {
        QFETCH(QString, profile);
        jobs::Project p;
        p.documentId = "gantt-reporting-due";
        p.name = "보고 대상";
        p.startDate = "2026-11-09";
        p.statusAt = "2026-11-10 09:00";
        p.calculationProfile = profile.toStdString();
        p.tasks = {{"A", "기한 경과", 480, {}},
                   {"B", "상태일 경계", 480, {"A"}},
                   {"C", "미래 작업", 480, {"B"}},
                   {"D", "미래 미착수 보고", 480, {"B"}},
                   {"M", "미래 마일스톤", 0, {"C"}}};
        jobs::ProgressEntry fact;
        fact.id = "report-D";
        fact.taskId = "D";
        fact.statusAt = *p.statusAt;
        fact.sourceDocumentId = p.documentId;
        fact.recordedRevision = p.revision;
        fact.recordedAt = "2026-11-10T12:00:00Z";
        fact.reporter = "PM";
        fact.reason = "착수 전 확인";
        fact.remainingMinutes = 480;
        p.progress.push_back(fact);
        // A later report must not change the earlier status date's display.
        fact.id = "report-C";
        fact.taskId = "C";
        fact.statusAt = "2026-11-12 09:00";
        p.progress.push_back(fact);
        const auto before = p;
        const auto plan = jobs::calculatePlan(p), result = jobs::calculate(p);
        QCOMPARE(plan.status, jobs::CalculationStatus::Valid);
        QCOMPARE(result.status, jobs::CalculationStatus::Valid);
        QCOMPARE(result.plannedTasks, plan.tasks);
        QCOMPARE(plan.tasks[1].start, *p.statusAt);
        QVERIFY(result.tasks[1].start > *p.statusAt);
        jobs::GanttView view;
        view.resize(1200, 500);
        view.setPlan(&p, &result, indices(p));
        for (const auto *id : {"A", "B"}) {
            QVERIFY(bar(view, id));
            QCOMPARE(bar(view, id)->brush().style(), Qt::BDiagPattern);
        }
        for (const auto *id : {"C", "M"}) {
            auto *shape = dynamic_cast<QAbstractGraphicsShapeItem *>(
                item(view, id, id == std::string("M") ? "milestone" : "task"));
            QVERIFY(shape);
            QCOMPARE(shape->brush().style(), Qt::SolidPattern);
            QCOMPARE(shape->brush().color(), jobs::ganttFill(jobs::GanttProgress::Plan));
            QVERIFY(!shape->toolTip().contains("미보고"));
            QVERIFY(shape->toolTip().contains("시작 예정"));
            QVERIFY(!shape->toolTip().contains("실적 상태 미반영"));
        }
        QVERIFY(bar(view, "D")->toolTip().contains("미착수 보고"));
        jobs::SchedulePrintSource source;
        source.project = std::make_shared<const jobs::Project>(p);
        source.columns = {{"number", "번호", 36}, {"name", "작업", 120}};
        source.generatedAt = "2026-11-18T00:00:00Z";
        source.printId = "gantt-reporting-due";
        for (int i = 0; i < int(p.tasks.size()); ++i)
            source.rows.push_back(
                {p.tasks[i].id, i + 1, {QString::number(i + 1), QString::fromStdString(p.tasks[i].name)}});
        const auto layout = jobs::buildSchedulePrint(source, {});
        QCOMPARE(layout.rows[0].progress, jobs::GanttProgress::Unreported);
        QCOMPARE(layout.rows[1].progress, jobs::GanttProgress::Unreported);
        QCOMPARE(layout.rows[2].progress, jobs::GanttProgress::Plan);
        QCOMPARE(layout.rows[3].progress, jobs::GanttProgress::NotStarted);
        QCOMPARE(layout.rows[4].progress, jobs::GanttProgress::Plan);
        view.setPlan(&p, &plan, indices(p));
        auto *milestone = dynamic_cast<QAbstractGraphicsShapeItem *>(item(view, "M", "milestone"));
        QVERIFY(milestone && milestone->pen().widthF() > 1);
        QVERIFY(milestone->toolTip().contains("임계 마일스톤"));
        QVERIFY(p == before);
    }
    void frozenCalendarWorkingMinutesDoNotFlagNonworkingGaps() {
        jobs::PlanInput input;
        input.startDate = "2026-11-09";
        jobs::TaskCalendar calendar;
        calendar.id = "site";
        calendar.periods = {{540, 720}, {780, 1020}};
        calendar.holidays = {"2026-11-11"};
        input.calendars.push_back(calendar);
        jobs::Task task;
        task.calendarId = "site";
        QCOMPARE(jobs::ganttFinishVariance(input, task, "2026-11-10 17:00", "2026-11-12 09:00"),
                 std::optional<std::int64_t>{0});
        QCOMPARE(jobs::ganttFinishVariance(input, task, "2026-11-10 17:00", "2026-11-12 10:00"),
                 std::optional<std::int64_t>{60});
        task.calendarId = "missing";
        QVERIFY(!jobs::ganttFinishVariance(input, task, "2026-11-10 17:00", "2026-11-12 10:00"));
    }
    void virtualizedRowsKeepSelectionAndProgressStyle() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("가상화 상태", "2026-11-09", std::vector<std::string>(240, "설치"));
        for (int i = 0; i < 240; ++i)
            doc.setDuration(i, 480);
        doc.setStatusAt("2026-11-09 09:00");
        const auto p = *doc.project();
        const auto result = jobs::calculate(p);
        jobs::GanttView view;
        view.resize(1000, 300);
        view.setPlan(&p, &result, indices(p));
        view.show();
        const auto id = p.tasks.back().id;
        QVERIFY(!bar(view, id));
        view.setSelectedTask(id);
        QVERIFY(bar(view, id));
        QCOMPARE(bar(view, id)->brush().style(), Qt::BDiagPattern);
        QVERIFY(bar(view, id)->pen().widthF() > 1);
        QVERIFY(item(view, id, "selectionHighlight")->isVisible());
        view.setDayWidth(16);
        view.fitSchedule();
        QVERIFY(bar(view, id) && bar(view, id)->pen().widthF() > 1);
        auto dark = view.palette();
        dark.setColor(QPalette::Base, QColor("#202020"));
        view.setPalette(dark);
        QCOMPARE(bar(view, id)->pen().color(), jobs::ganttCriticalColor(true));
        QVERIFY(item(view, id, "selectionHighlight")->isVisible());
    }
    void printedRowsUseTheSameStatesAndDelayBasis() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        const auto baseline = fixture(doc);
        jobs::SchedulePrintSource source;
        source.project = doc.snapshot();
        source.unsaved = doc.dirty();
        source.scope = "전체 6개 작업 · 간트 상태 검증";
        source.baselineId = QString::fromStdString(baseline);
        source.generatedAt = "2026-11-18T00:00:00Z";
        source.printId = "gantt-appearance";
        source.columns = {{"number", "번호", 36}, {"name", "작업", 120}};
        for (int i = 0; i < 6; ++i)
            source.rows.push_back({doc.project()->tasks[i].id,
                                   i + 1,
                                   {QString::number(i + 1), QString::fromStdString(doc.project()->tasks[i].name)}});
        auto layout = jobs::buildSchedulePrint(source, {});
        QCOMPARE(layout.status, jobs::CalculationStatus::Valid);
        QCOMPARE(layout.rows[0].progress, jobs::GanttProgress::Completed);
        QCOMPARE(layout.rows[1].progress, jobs::GanttProgress::InProgress);
        QCOMPARE(layout.rows[2].progress, jobs::GanttProgress::NotStarted);
        QCOMPARE(layout.rows[3].progress, jobs::GanttProgress::Plan);
        QCOMPARE(layout.rows[4].progress, jobs::GanttProgress::Unreported);
        QVERIFY(layout.rows[1].critical && !layout.rows[4].critical);
        QVERIFY(layout.rows[3].finishVariance && *layout.rows[3].finishVariance > 0);
        if (const auto folder = qEnvironmentVariable("JOBS_GANTT_AUDIT_DIR"); !folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            const auto name = "gantt-states-" + QString::fromStdString(source.project->documentId) + ".pdf";
            QVERIFY(jobs::exportSchedulePdf(layout, QDir(folder).filePath(name)) > 0);
        }
        layout.options.baseline = false;
        const auto withoutBaseline = jobs::buildSchedulePrint(source, layout.options);
        QVERIFY(!withoutBaseline.rows[3].finishVariance);
    }
    void pilotThirtyTasks_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void pilotThirtyTasks() {
        QFETCH(bool, dark);
        const auto palette = QApplication::palette();
        const auto restore = qScopeGuard([&] { QApplication::setPalette(palette); });
        auto colors = palette;
        colors.setColor(QPalette::Base, QColor(dark ? "#202020" : "#ffffff"));
        colors.setColor(QPalette::Window, QColor(dark ? "#292929" : "#f4f4f4"));
        colors.setColor(QPalette::Text, QColor(dark ? "#eeeeee" : "#202020"));
        colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
        colors.setColor(QPalette::Button, QColor(dark ? "#383838" : "#eeeeee"));
        colors.setColor(QPalette::ButtonText, colors.color(QPalette::Text));
        colors.setColor(QPalette::AlternateBase, QColor(dark ? "#292929" : "#f4f6f8"));
        colors.setColor(QPalette::Midlight, QColor(dark ? "#3c3c3c" : "#d9dfe5"));
        QApplication::setPalette(colors);
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        const auto path = QFINDTESTDATA("../docs/validation/weekly-pm-pilot-2026-11-18/pilot-start.ppm");
        QVERIFY(!path.isEmpty());
        doc.open(path.toStdString());
        doc.applyCalculation(jobs::calculate(*doc.project()));
        jobs::MainWindow window(doc);
        window.resize(1562, 932);
        window.show();
        auto *view = dynamic_cast<jobs::GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
        QVERIFY(view && view->hasSchedule());
        view->fitSchedule();
        int critical = 0, unreported = 0;
        for (const auto &task : doc.project()->tasks)
            if (auto *shape = bar(*view, task.id)) {
                QVERIFY(shape->brush().color() != jobs::ganttCriticalColor(dark));
                critical += shape->pen().widthF() > 1;
                unreported += shape->brush().style() == Qt::BDiagPattern;
            }
        QVERIFY(critical > 15 && unreported > 0);
        const auto tag = dark ? "dark" : "light";
        capture(window, QString(tag) + "-30-tasks");
        auto *legend = window.findChild<QMenu *>("ganttLegendMenu");
        auto *button = window.findChild<QToolButton *>("ganttLegendButton");
        QVERIFY(legend && button);
        QVERIFY(window.findChild<QLabel *>("ganttLegendText")->text().contains("착수 여부 미확인"));
        const auto text = window.findChild<QLabel *>("ganttLegendText")->text();
        for (const auto progress : {jobs::GanttProgress::Plan, jobs::GanttProgress::InProgress,
                                    jobs::GanttProgress::Completed, jobs::GanttProgress::Unreported})
            QVERIFY(text.contains(jobs::ganttFill(progress).name()));
        QVERIFY(text.contains(jobs::ganttDelayColor().name()));
        if (!qEnvironmentVariable("JOBS_GANTT_AUDIT_DIR").isEmpty()) {
            legend->popup(button->mapToGlobal(QPoint(0, button->height())));
            capture(*legend, QString(tag) + "-legend");
            legend->hide();
        }
    }
};
QTEST_MAIN(GanttAppearanceUiTests)
#include "GanttAppearanceUiTests.moc"
