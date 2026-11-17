#include "TaskTableTest.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/EditorUi.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 15000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(ResourceComparisonDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "button missing");
    return b;
}
void workflow() {
    JsonProjectStore store;
    QTemporaryDir temp;
    Document doc(store);
    doc.create("자원 평준화 채택 검증", "2026-11-02", {"기초 A", "설비 B", "시운전 C"});
    for (int i = 0; i < 3; ++i) {
        doc.setDuration(i, 480);
        doc.setTaskCost({doc.project()->tasks[i].id, 0});
    }
    Resource r;
    r.name = "공유 작업자";
    r.rates = {{"2026-11-02", 1}, {"2026-11-03", 2}};
    auto rid = doc.putResource(r);
    const auto assignmentSource = doc.snapshot();
    for (const auto &t : assignmentSource->tasks)
        doc.setAssignments(t.id, {{t.id, rid, 100}});
    const auto baseline = doc.captureBaseline("논리 기준선", "PM", "승인", "2026-11-03T00:00:00Z");
    const auto scenario = doc.saveScenario("이전 입력", "task-order", 260, "2026-11-03T00:00:00Z",
                                           doc.project()->documentId, doc.project()->revision);
    doc.renameTask(0, "기초 A 수정");
    ResourceComparisonDialog dialog(doc);
    prepareEditorDialog(dialog);
    dialog.show();
    until([&] { return button(dialog, "adoptLeveling")->isEnabled(); }, "comparison not ready");
    auto *source = dialog.findChild<QComboBox *>("levelingSource");
    source->setCurrentIndex(source->findData(QString::fromStdString(scenario)));
    until([&] { return !button(dialog, "cancelLeveling")->isEnabled(); }, "saved comparison not ready");
    check(!button(dialog, "adoptLeveling")->isEnabled() &&
              dialog.findChild<QLabel *>("levelingSummary")->text().contains("다릅니다"),
          "changed saved source could be adopted");
    source->setCurrentIndex(0);
    until([&] { return button(dialog, "adoptLeveling")->isEnabled(); }, "current comparison not ready");
    dialog.findChild<QLineEdit *>("levelingReason")->setText("공유 작업자 과부하 해소");
    button(dialog, "adoptLeveling")->click();
    until([&] { return doc.project()->planLeveling && button(dialog, "adoptLeveling")->isEnabled(); },
          "adoption worker did not commit");
    check(doc.calculation() && doc.calculation()->resourceLeveled &&
              doc.calculation()->projectFinish == "2026-11-04 18:00",
          "adoption cache missing");
    QDir().mkpath("leveling-selection-qa");
    QTest::qWait(30);
    const auto rect =
        QRect(button(dialog, "adoptLeveling")->mapTo(&dialog, QPoint{}), button(dialog, "adoptLeveling")->size());
    check(dialog.rect().contains(rect), "adoption action clipped");
    check(dialog.grab().save("leveling-selection-qa/adoption.png"), "comparison screenshot failed");
    MainWindow window(doc);
    window.resize(1400, 850);
    window.show();
    QAction *calculateAction = nullptr;
    for (auto *action : window.findChildren<QAction *>())
        if (action->text().contains("일정 계산(&C)"))
            calculateAction = action;
    check(calculateAction, "calculation action missing");
    calculateAction->trigger();
    auto *table = taskTable(window);
    until(
        [&] {
            return calculateAction->isEnabled() && doc.calculation() &&
                   doc.calculation()->status == CalculationStatus::Valid && cellText(table, 1, 3) == "2026-11-03 09:00";
        },
        "main table did not follow adoption");
    check(cellText(table, 1, 5) == "0" && cellText(table, 1, 6) == "평준화 임계", "table leaked CPM float");
    check(window.statusBar()->currentMessage().contains("배정 자원 검증 통과"),
          "status bar lost selected resource status");
    auto *gantt = dynamic_cast<GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
    bool label = false;
    for (auto *item : gantt->scene()->items())
        if (item->data(1) == "label" && item->toolTip().contains("평준화"))
            label = true;
    check(label, "Gantt adoption label missing");
    table->setColumnHidden(5, false);
    table->setColumnHidden(6, false);
    table->scrollTo(cell(table, 1, 6));
    QTest::qWait(30);
    check(window.grab().save("leveling-selection-qa/schedule.png"), "schedule screenshot failed");
    ReportOptions options;
    options.reportId = "leveling-independent";
    options.generatedAt = "2026-11-03T03:00:00Z";
    options.baselineId = baseline;
    const auto report = buildReport(*doc.project(), options);
    const auto usage =
        usageReport(buildUsage(*doc.project(), {"2026-11-02", "2026-11-08", baseline, {}, UsagePeriod::Day}),
                    UsageHierarchy::Tasks, "leveling-usage", "2026-11-03T03:00:00Z", true);
    PdfOptions pdf;
    pdf.font = QApplication::font();
    for (const auto &entry :
         std::vector<std::pair<QString, const ProjectReport *>>{{"schedule", &report}, {"usage", &usage}}) {
        const auto stem = "leveling-selection-qa/" + entry.first;
        QFile::remove(stem + ".xlsx");
        QFile::remove(stem + ".pdf");
        exportXlsx(*entry.second, stem + ".xlsx");
        exportPdf(*entry.second, stem + ".pdf", ReportFamily::All, pdf);
    }
    window.hide();
    button(dialog, "clearLeveling")->click();
    until([&] { return !doc.project()->planLeveling && button(dialog, "adoptLeveling")->isEnabled(); },
          "clear did not finish");
    check(doc.calculation()->projectFinish == "2026-11-02 18:00", "clear did not restore logical schedule");
    const auto stable = *doc.project();
    button(dialog, "adoptLeveling")->click();
    button(dialog, "cancelLeveling")->click();
    until([&] { return !button(dialog, "cancelLeveling")->isEnabled(); }, "cancel did not finish");
    check(*doc.project() == stable, "cancelled adoption committed");
    button(dialog, "runLeveling")->click();
    until([&] { return button(dialog, "adoptLeveling")->isEnabled(); }, "recalculate after cancel failed");
    const auto path = (temp.path() + "/selection.ppm").toStdString();
    doc.saveAs(path);
    button(dialog, "runLeveling")->click();
    until([&] { return button(dialog, "adoptLeveling")->isEnabled(); }, "saved comparison failed");
    button(dialog, "adoptLeveling")->click();
    doc.open(path);
    until([&] { return !button(dialog, "cancelLeveling")->isEnabled(); }, "stale submission stalled");
    check(!doc.project()->planLeveling, "same-id reopened session accepted stale adoption");
    button(dialog, "runLeveling")->click();
    until([&] { return button(dialog, "adoptLeveling")->isEnabled(); }, "reopened source comparison failed");
    button(dialog, "adoptLeveling")->click();
    until([&] { return doc.project()->planLeveling && button(dialog, "adoptLeveling")->isEnabled(); },
          "reopened source adoption failed");
    doc.save();
    doc.open(path);
    const auto reopened = *doc.project();
    button(dialog, "clearLeveling")->click();
    QTest::qWait(30);
    check(*doc.project() == reopened && doc.project()->planLeveling, "old dialog cleared reopened selected source");
    dialog.reject();
    Document ro(store);
    ro.open(path, true);
    ResourceComparisonDialog readOnly(ro);
    readOnly.show();
    until([&] { return !button(readOnly, "cancelLeveling")->isEnabled(); }, "read-only comparison stalled");
    check(!button(readOnly, "adoptLeveling")->isEnabled() && !button(readOnly, "clearLeveling")->isEnabled(),
          "read-only adoption enabled");
    readOnly.reject();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto id =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (id >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    try {
        workflow();
        std::cout << "adoption UI: compare/source/commit/table/Gantt/release/cancel/session/readonly/export passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
