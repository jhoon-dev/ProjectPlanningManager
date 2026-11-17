#include "application/Document.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include "reporting/SchedulePrint.h"
#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/ResourceComparisonDialog.h"
#include "TaskTableTest.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTableWidget>
#include <iostream>
using namespace jobs;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void until(F f) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), "UI timeout");
}
void workflow(bool split, bool repartition = false) {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    d.create("평준화 후 여유·임계", "2026-11-02", {"작업 A", "작업 B", "독립 C"});
    for (int i = 0; i < 3; ++i) {
        d.setDuration(i, 480);
        d.setTaskCost({d.project()->tasks[i].id, 0});
    }
    Resource r;
    r.name = "공유 인력";
    r.rates = {{"2026-11-02", 60}};
    const auto rid = d.putResource(r);
    d.setAssignments(d.project()->tasks[0].id, {{d.project()->tasks[0].id, rid, 100}});
    d.setAssignments(d.project()->tasks[1].id, {{d.project()->tasks[1].id, rid, 100}});
    if (split) {
        d.setTaskEffort(0, TaskType::FixedDuration, false, 480, {}, {{"2026-11-02 12:00", "2026-11-03 09:00"}});
        d.setDuration(1, 240);
    }
    if (repartition)
        d.setDuration(2, 1440);
    const QString qa = repartition ? "repartition-float-qa" : split ? "segment-leveling-float-qa" : "leveling-float-qa";
    ResourceComparisonDialog preview(d);
    preview.show();
    auto *adopt = preview.findChild<QPushButton *>("adoptLeveling");
    until([&] { return adopt->isEnabled(); });
    auto *rows = preview.findChild<QTableWidget *>("levelingTasks");
    check(rows && rows->columnCount() == 12 &&
              rows->item(2, 9)->text() == (repartition ? "0.00"
                                           : split     ? "5.00"
                                                       : "8.00"),
          "preview float columns");
    preview.findChild<QLineEdit *>("levelingReason")->setText("자원 순서 고정 검토");
    adopt->click();
    until([&] { return d.project()->planLeveling && adopt->isEnabled(); });
    MainWindow main(d);
    main.resize(1400, 900);
    main.show();
    QTest::qWait(30);
    auto *table = taskTable(main);
    check(cellText(table, 0, 5) == (repartition ? "11" : "0") &&
              cellText(table, 0, 6) == (repartition ? "평준화 여유" : "평준화 임계") &&
              cellText(table, 2, 5) == (repartition ? "0"
                                        : split     ? "5"
                                                    : "8") &&
              cellText(table, 2, 6) == (repartition ? "평준화 임계" : "평준화 여유"),
          "main float display");
    auto *gantt = dynamic_cast<GanttView *>(main.findChild<QGraphicsView *>("ganttView"));
    int resourceEdges = 0;
    for (auto *item : gantt->scene()->items())
        if (item->data(1) == "resourceDependency") {
            ++resourceEdges;
            if (split)
                check(item->toolTip().contains("실행 구간"), "segment endpoint tooltip");
        }
    check(resourceEdges == (split ? 2 : 1), "Gantt resource handovers");
    if (split)
        check(cellText(table, 1, 5) == (repartition ? "9" : "1") && cellText(table, 1, 6) == "평준화 여유",
              "cross-split main float");
    if (repartition)
        check(cell(table, 1, 5).data(Qt::ToolTipRole).toString().contains("구간 재생성") &&
                  cell(table, 1, 5).data(Qt::ToolTipRole).toString().contains("자유여유 1h"),
              "repartition policy lost in table snapshot");
    QDir().mkpath(qa);
    check(main.grab().save(qa + "/schedule.png"), "screenshot");
    check(preview.grab().save(qa + "/comparison.png"), "preview screenshot");
    ReportOptions o;
    o.reportId = "leveling-float";
    o.generatedAt = "2026-11-03T03:00:00Z";
    const auto report = buildReport(*d.project(), o);
    PdfOptions pdf;
    pdf.font = QApplication::font();
    QFile::remove(qa + "/report.xlsx");
    QFile::remove(qa + "/report.pdf");
    exportXlsx(report, qa + "/report.xlsx");
    exportPdf(report, qa + "/report.pdf", ReportFamily::All, pdf);
    SchedulePrintSource source;
    source.project = d.snapshot();
    source.session = d.sessionGeneration();
    source.scope = "전체";
    source.generatedAt = "2026-11-03T03:00:00Z";
    source.printId = "leveling-float";
    source.columns = {
        {"number", "번호", 36}, {"name", "작업", 140}, {"totalFloat", "총여유", 80}, {"critical", "상태", 100}};
    for (std::size_t i = 0; i < d.project()->tasks.size(); ++i)
        source.rows.push_back({d.project()->tasks[i].id,
                               int(i + 1),
                               {QString::number(i + 1), QString::fromStdString(d.project()->tasks[i].name),
                                "STALE_FLOAT", "STALE_STATE"}});
    SchedulePrintOptions options;
    options.font = QApplication::font();
    const auto print = buildSchedulePrint(source, options);
    check(print.rows[2].cells[2] == (repartition ? "0"
                                     : split     ? "5"
                                                 : "8") &&
              print.rows[0].cells[3] == (repartition ? "평준화 여유" : "평준화 임계"),
          "print float");
    QFile::remove(qa + "/print.pdf");
    exportSchedulePdf(print, qa + "/print.pdf");
    if (split) {
        QFile::remove(qa + "/selected.ppm");
        d.saveAs((qa + "/selected.ppm").toStdString());
    }
    preview.reject();
    main.hide();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        workflow(false);
        workflow(true);
        workflow(true, true);
        std::cout << "leveling float UI/table/Gantt/preview/report/print passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
