#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/ComparisonDialog.h"
#include "reporting/ReportExport.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTest>
#include <QTableWidget>
#include <algorithm>
#include <iostream>

namespace {
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
void ready(jobs::Document &doc, jobs::MainWindow &window) {
    bool triggered = false;
    for (auto *action : window.findChildren<QAction *>())
        if (action->text().contains("일정 계산(&C)")) {
            action->trigger();
            triggered = true;
            break;
        }
    check(triggered, "calculate action missing");
    QElapsedTimer timer;
    timer.start();
    while ((!doc.calculation() || doc.calculation()->status != jobs::CalculationStatus::Valid ||
            doc.calculation()->inputRevision != doc.project()->revision) &&
           timer.elapsed() < 10000)
        QTest::qWait(10);
    check(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Valid &&
              doc.calculation()->inputRevision == doc.project()->revision,
          "UI calculation did not finish");
    QTest::qWait(30);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // The Windows offscreen plugin has no native font discovery.
    const auto fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf");
    const auto fontId = QFontDatabase::addApplicationFont(fontPath);
    if (fontId >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 9));
#endif
    try {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("잔여 일정 임계 검증", "2026-11-02", {"완료된 기초", "후속 공사 B", "후속 공사 C", "독립 공사 D"});
        for (std::size_t i = 0; i < 4; ++i)
            doc.setDuration(i, 480);
        doc.setPredecessors(1, {doc.project()->tasks[0].id});
        doc.setPredecessors(2, {doc.project()->tasks[1].id});
        const auto baselineId = doc.captureBaseline("최초 계획", "PM", "승인", "2026-11-02T00:00:00Z");
        doc.setStatusAt("2026-11-02 18:00");
        jobs::ProgressEntry e;
        e.taskId = doc.project()->tasks[0].id;
        e.actualStart = "2026-11-02 09:00";
        e.actualFinish = "2026-11-02 18:00";
        e.actualDurationMinutes = 480;
        e.reporter = "PM";
        e.reason = "완료 보고";
        e.recordedAt = "2026-11-02T09:00:00Z";
        doc.recordProgress(e);
        jobs::MainWindow window(doc);
        window.resize(1400, 850);
        window.show();
        ready(doc, window);
        auto *table = taskTable(window);
        auto *gantt = dynamic_cast<jobs::GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
        check(table && gantt && cellText(table, 0, 5) == "—" && cellText(table, 0, 6) == "실제 완료" &&
                  cellText(table, 1, 5) == "0" && cellText(table, 1, 6) == "예측 임계" &&
                  cellText(table, 3, 5) == "8" && cellText(table, 3, 6) == "예측 여유",
              "table did not distinguish completed, critical and noncritical forecast work");
        table->setColumnHidden(5, false);
        table->setColumnHidden(6, false);
        table->scrollTo(cell(table, 1, 6));
        check(cell(table, 3, 5).data(Qt::ToolTipRole).toString().contains("자유여유 8h"), "free float tooltip missing");
        int criticalLabels = 0, driving = 0;
        for (auto *item : gantt->scene()->items()) {
            if (item->data(1) == "label" && item->toolTip().contains("예측 임계 작업"))
                ++criticalLabels;
            if (item->data(1) == "dependency") {
                auto *path = dynamic_cast<QGraphicsPathItem *>(item);
                if (path && path->pen().color() ==
                                jobs::ganttCriticalColor(gantt->palette().color(QPalette::Base).lightness() < 128))
                    ++driving;
            }
        }
        check(criticalLabels == 2 && driving == 1, "future critical labels/driving chain incorrect");
        jobs::ComparisonDialog comparison(doc, baselineId, &window);
        comparison.show();
        auto *comparisonTable = comparison.findChild<QTableWidget *>("comparisonTable");
        for (int i = 0; i < 500 && comparisonTable->rowCount() != 4; ++i)
            QTest::qWait(10);
        check(comparisonTable->rowCount() == 4 && comparisonTable->item(0, 15)->text() == "—" &&
                  comparisonTable->item(3, 15)->text() == "8.00" && comparisonTable->item(3, 16)->text() == "8.00" &&
                  comparisonTable->item(1, 17)->text() == "예측 임계",
              "comparison did not show remaining float");
        comparison.hide();
        const auto original = *doc.project();
        jobs::ReportOptions options;
        options.reportId = "forecast-float-ui";
        options.generatedAt = "2026-11-02T10:00:00Z";
        options.baselineId = baselineId;
        auto report = jobs::buildReport(original, options);
        const auto kpi = std::find_if(report.kpis.begin(), report.kpis.end(),
                                      [](const auto &k) { return k.id == "forecastCritical"; });
        check(kpi != report.kpis.end() && std::get<std::int64_t>(kpi->value.value) == 2, "forecast KPI mismatch");
        QDir artifacts("forecast-float-qa");
        check(QDir().mkpath(artifacts.path()), "cannot create QA directory");
        check(window.grab().save(artifacts.filePath("forecast.png")), "cannot save UI capture");
        const auto xlsx = artifacts.filePath("forecast.xlsx"), pdf = artifacts.filePath("forecast.pdf");
        QFile::remove(xlsx);
        QFile::remove(pdf);
        jobs::exportXlsx(report, xlsx, jobs::ReportFamily::Schedule);
        jobs::PdfOptions pdfOptions;
        pdfOptions.font = QApplication::font();
        check(jobs::exportPdf(report, pdf, jobs::ReportFamily::Schedule, pdfOptions) > 0, "forecast PDF failed");
        check(*doc.project() == original, "view/export changed ledger or baseline");
        doc.setDuration(3, 960);
        ready(doc, window);
        check(cellText(table, 3, 5) == "0" && cellText(table, 3, 6) == "예측 임계",
              "cached table float did not refresh");
        doc.setStatusAt(std::nullopt);
        ready(doc, window);
        check(cellText(table, 3, 6) == "여유" && cellText(table, 3, 5) == "8",
              "plan mode retained forecast label/float");
        std::cout << "forecast float UI/table/gantt/KPI/XLSX/PDF passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
