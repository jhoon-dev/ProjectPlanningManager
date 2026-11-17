#include "UsageFixture.h"
#include "ui/PeriodActualDialog.h"
#include "ui/UsageDialog.h"
#include "ui/MainWindow.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <iostream>
namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < 15000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(PeriodActualDialog &d, const char *name) {
    return d.findChild<QPushButton *>(name);
}
void fill(PeriodActualDialog &d, int row, const QString &resource, const QString &date, const QString &work,
          const QString &cost) {
    auto *table = d.findChild<QTableWidget *>("periodActualRows");
    if (row >= table->rowCount())
        button(d, "addActualRow")->click();
    auto *choice = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
    check(choice && choice->findData(resource) >= 0, "actual resource choice missing");
    choice->setCurrentIndex(choice->findData(resource));
    for (const auto &value : std::vector<std::pair<int, QString>>{{0, date}, {2, work}, {3, cost}})
        table->item(row, value.first)->setText(value.second);
}
void post(PeriodActualDialog &d) {
    button(d, "submitPeriodActuals")->click();
    until([&] { return !button(d, "cancelPeriodActuals")->isEnabled(); }, "actual submission worker did not finish");
    if (!d.findChild<QLabel *>("periodActualError")->text().contains("제출했습니다"))
        throw std::runtime_error(d.findChild<QLabel *>("periodActualError")->text().toStdString());
}
void workflow() {
    JsonProjectStore store;
    QTemporaryDir temp;
    auto p = usage_fixture::project();
    usage_fixture::progress(p);
    p.statusAt = "2026-11-03 18:00";
    const auto path = (temp.path() + "/actuals.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    PeriodActualDialog dialog(doc, "A");
    dialog.show();
    dialog.findChild<QLineEdit *>("actualReporter")->setText("현장 담당자");
    dialog.findChild<QLineEdit *>("actualReason")->setText("관측 실제 · 누계 포함 배분");
    check(dialog.findChild<QLineEdit *>("actualCumulative")->text() == "80", "legacy cumulative not prefilled");
    fill(dialog, 0, "R", "2026-11-02", "8", "30");
    post(dialog);
    check(doc.project()->periodActuals.size() == 1 && !doc.canUndo(), "UI did not preserve immutable fact boundary");
    fill(dialog, 0, "R", "2026-11-03", "0", "0");
    post(dialog);
    fill(dialog, 0, "E", "2026-11-03", "1", "");
    post(dialog);
    auto *history = dialog.findChild<QTableView *>("periodActualHistory");
    check(history->model()->rowCount() == 3, "history rows missing");
    history->selectRow(2);
    button(dialog, "loadActualCorrection")->click();
    auto *draft = dialog.findChild<QTableWidget *>("periodActualRows");
    check(draft->item(0, 2)->text() == "48000u", "correction lost exact source work");
    draft->item(0, 2)->setText("6");
    draft->item(0, 3)->setText("25");
    post(dialog);
    check(doc.project()->periodActuals.size() == 4 && history->model()->rowCount() == 4, "correction replaced history");
    check(dialog.findChild<QLabel *>("actualBalance")->text().contains("55"), "unallocated remainder missing");
    QDir().mkpath("period-actual-qa");
    QTest::qWait(20);
    const auto submitRect = QRect(button(dialog, "submitPeriodActuals")->mapTo(&dialog, QPoint{}),
                                  button(dialog, "submitPeriodActuals")->size());
    check(dialog.rect().contains(submitRect), "submit action scrolled outside the visible dialog");
    check(dialog.grab().save("period-actual-qa/input.png"), "input screenshot failed");
    const auto stable = *doc.project();
    fill(dialog, 0, "R", "2026-11-03", "1", "1");
    fill(dialog, 1, "R", "2026-11-03", "2", "2");
    button(dialog, "submitPeriodActuals")->click();
    until([&] { return !button(dialog, "cancelPeriodActuals")->isEnabled(); }, "duplicate validation stalled");
    check(*doc.project() == stable && dialog.findChild<QLabel *>("periodActualError")->text().contains("확인"),
          "duplicate input partially committed");
    draft->setRowCount(1);
    fill(dialog, 0, "R", "2026-11-04", "1", "1");
    button(dialog, "submitPeriodActuals")->click();
    until([&] { return !button(dialog, "cancelPeriodActuals")->isEnabled(); }, "future validation stalled");
    check(*doc.project() == stable, "future actual posted");
    fill(dialog, 0, "R", "2026-11-03", "1", "0");
    button(dialog, "submitPeriodActuals")->click();
    button(dialog, "cancelPeriodActuals")->click();
    until([&] { return !button(dialog, "cancelPeriodActuals")->isEnabled(); }, "cancelled submission stalled");
    check(*doc.project() == stable, "cancelled actuals committed");
    UsageDialog usage(doc, {}, p.baselines.front().id);
    usage.show();
    auto *tree = usage.findChild<QTreeView *>("usageTree");
    until([&] { return tree->model()->rowCount() > 0; }, "actual Usage worker stalled");
    auto *metric = usage.findChild<QComboBox *>("usageMetric");
    metric->setCurrentIndex(6);
    auto values = tree->model()->index(0, 1).data(Qt::UserRole + 2).toList();
    check(values[0].toLongLong() == 36000 && values[1].toLongLong() == 6000 && values[2].toLongLong() == 25,
          "UI actual totals incorrect");
    auto counts = tree->model()->index(0, 1).data(Qt::UserRole + 3).toList();
    check(counts[0].toInt() == 3 && counts[1].toInt() == 2, "actual report coverage count wrong");
    metric->setCurrentIndex(7);
    check(tree->model()->index(0, 2).data().toString() == "80", "dated cost added to cumulative AC");
    tree->expandAll();
    QTest::qWait(20);
    check(usage.grab().save("period-actual-qa/usage.png"), "Usage screenshot failed");
    const auto result =
        buildUsage(*doc.project(), {"2026-11-02", "2026-11-08", p.baselines.front().id, {}, UsagePeriod::Day});
    const auto report =
        usageReport(result, UsageHierarchy::Tasks, "period-actual-independent", "2026-11-03T03:00:00Z", true);
    QFile::remove("period-actual-qa/actuals.xlsx");
    QFile::remove("period-actual-qa/actuals.pdf");
    exportXlsx(report, "period-actual-qa/actuals.xlsx");
    PdfOptions pdf;
    pdf.font = QApplication::font();
    exportPdf(report, "period-actual-qa/actuals.pdf", ReportFamily::All, pdf);
    ReportOptions options;
    options.reportId = "actual-standard";
    options.generatedAt = "2026-11-03T03:00:00Z";
    options.baselineId = p.baselines.front().id;
    const auto standard = buildReport(*doc.project(), options);
    check(std::any_of(standard.tables.begin(), standard.tables.end(),
                      [](const auto &t) { return t.id == "periodActualBalance"; }),
          "standard report omitted reconciliation");
    usage.reject();
    doc.save();
    fill(dialog, 0, "R", "2026-11-03", "1", "0");
    button(dialog, "submitPeriodActuals")->click();
    doc.open(path);
    until([&] { return !button(dialog, "cancelPeriodActuals")->isEnabled(); }, "stale worker did not finish");
    check(doc.project()->periodActuals.size() == 4 && !button(dialog, "submitPeriodActuals")->isEnabled(),
          "reopened document accepted stale actuals");
    dialog.reject();
    Document readOnly(store);
    readOnly.open(path, true);
    PeriodActualDialog ro(readOnly, "A");
    ro.show();
    check(!button(ro, "submitPeriodActuals")->isEnabled() &&
              ro.findChild<QTableView *>("periodActualHistory")->model()->rowCount() == 4,
          "read-only history/edit protection failed");
    ro.reject();
    MainWindow window(doc);
    window.show();
    auto *action = window.findChild<QAction *>("periodActualInput");
    check(action, "period actual menu missing");
    bool opened = false;
    QTimer::singleShot(30, [&] {
        if (auto *active = QApplication::activeModalWidget(); active && active->objectName() == "periodActualDialog") {
            opened = true;
            static_cast<QDialog *>(active)->reject();
        }
    });
    action->trigger();
    check(opened, "period actual menu did not open editor");
    window.hide();
}
} // namespace
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto id =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (id >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "period actual UI timeout\n";
        std::exit(2);
    });
    watchdog.start(180000);
    try {
        workflow();
        std::cout << "period actual UI: "
                     "input/correction/unknown-zero/history/readonly/worker-cancel/session/Usage/export passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
