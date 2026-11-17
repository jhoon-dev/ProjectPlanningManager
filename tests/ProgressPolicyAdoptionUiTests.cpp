#include "ProgressPolicyFixture.h"
#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/AsyncEditController.h"
#include "ui/ProgressPolicyDialog.h"
#include "ui/GanttView.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "reporting/SchedulePrint.h"
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace jobs;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void until(F f, const char *m) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 20000)
        QTest::qWait(10);
    check(f(), m);
}
QPushButton *button(QWidget &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "button missing");
    return b;
}
void ready(QWidget &d) {
    until([&] { return d.property("progressPolicyReady").toBool(); }, "comparison timeout");
}
void adopt(QWidget &d, const char *name, const std::function<void()> &during = {}) {
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (!modal || modal->objectName() != "progressPolicyMetadata")
            return;
        auto *buttons = modal->findChild<QDialogButtonBox *>();
        check(!buttons->button(QDialogButtonBox::Ok)->isEnabled(), "blank metadata accepted");
        modal->findChild<QLineEdit *>("progressPolicyAdopter")->setText("PM");
        modal->findChild<QLineEdit *>("progressPolicyReason")->setText("잔여 예측 검토");
        timer.stop();
        if (during)
            during();
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    button(d, name)->click();
    check(!timer.isActive(), "metadata missing");
}
void exports(Document &doc) {
    const auto p = doc.snapshot();
    ReportOptions options;
    options.reportId = "progress-adoption";
    options.generatedAt = "2026-11-03T03:00:00Z";
    const auto report = buildReport(*p, options);
    const auto usage = usageReport(buildUsage(*p, {"2026-11-02", "2026-11-06", {}, {}, UsagePeriod::Day}),
                                   UsageHierarchy::Tasks, "progress-adoption-usage", options.generatedAt, true);
    PdfOptions pdf;
    pdf.font = QApplication::font();
    for (const auto &entry :
         std::vector<std::pair<QString, const ProjectReport *>>{{"schedule", &report}, {"usage", &usage}}) {
        const auto stem = "progress-policy-adoption-qa/" + entry.first;
        QFile::remove(stem + ".xlsx");
        QFile::remove(stem + ".pdf");
        exportXlsx(*entry.second, stem + ".xlsx");
        exportPdf(*entry.second, stem + ".pdf", ReportFamily::All, pdf);
    }
    SchedulePrintSource source;
    source.project = p;
    source.session = doc.sessionGeneration();
    source.unsaved = true;
    source.scope = "전체";
    source.generatedAt = "2026-11-03T03:00:00Z";
    source.printId = "adopted-progress-print";
    source.columns = {{"number", "번호", 36}, {"name", "작업", 140}, {"start", "시작", 110}, {"finish", "완료", 110}};
    for (std::size_t i = 0; i < p->tasks.size(); ++i)
        source.rows.push_back(
            {p->tasks[i].id,
             int(i + 1),
             {QString::number(i + 1), QString::fromStdString(p->tasks[i].name), "STALE_START", "STALE_FINISH"}});
    SchedulePrintOptions print;
    print.font = QApplication::font();
    const auto layout = buildSchedulePrint(source, print);
    check(layout.rows[1].cells[3] == "2026-11-03 14:00", "print used original forecast");
    QFile::remove("progress-policy-adoption-qa/print.pdf");
    exportSchedulePdf(layout, "progress-policy-adoption-qa/print.pdf");
    JsonProjectStore store;
    QFile::remove("progress-policy-adoption-qa/adopted.ppm");
    store.save("progress-policy-adoption-qa/adopted.ppm", *p, {});
    const auto xml = exportProjectXml(*p);
    const auto xer = exportPrimaveraXer(*p);
    check(projectXmlPreview(xml).contains("진척 정책") && primaveraXerPreview(xer).contains("진척 정책"),
          "exchange loss missing");
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = progress_fixture::small();
    Resource r;
    r.id = "R";
    r.name = "인력";
    r.rates = {{"2026-11-02", 60}, {"2026-11-04", 120}};
    p.resources = {r};
    p.assignments = {{"B", "R", 100}};
    for (const auto &t : p.tasks)
        p.taskCosts.push_back({t.id, 0});
    const auto path = temp.filePath("source.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    doc.applyCalculation(calculate(p));
    MainWindow main(doc);
    main.resize(1400, 900);
    main.show();
    main.findChild<QAction *>("compareProgressPolicies")->trigger();
    auto *dialog = dynamic_cast<ProgressPolicyDialog *>(main.findChild<QDialog *>("progressPolicyDialog"));
    check(dialog, "dialog");
    ready(*dialog);
    adopt(*dialog, "adoptOverrideProgressPolicy");
    until([&] { return doc.project()->progressPolicy.has_value() && dialog->property("progressPolicyReady").toBool(); },
          "adoption not committed");
    auto *table = taskTable(main);
    check(cellText(table, 1, 4) == "2026-11-03 14:00", "main table not refreshed");
    auto *gantt = dynamic_cast<GanttView *>(main.findChild<QGraphicsView *>("ganttView"));
    check(gantt, "Gantt");
    bool inactive = false;
    for (auto *item : gantt->scene()->items())
        if (item->data(1) == "dependency" && item->toolTip().contains("채택 진척 정책 비활성"))
            inactive = true;
    check(inactive, "inactive Gantt edge missing");
    QDir().mkpath("progress-policy-adoption-qa");
    QTest::qWait(40);
    check(dialog->grab().save("progress-policy-adoption-qa/adoption.png"), "adoption screenshot");
    check(main.grab().save("progress-policy-adoption-qa/schedule.png"), "schedule screenshot");
    exports(doc);
    button(*dialog, "clearProgressPolicy")->click();
    until([&] { return !doc.project()->progressPolicy && dialog->property("progressPolicyReady").toBool(); }, "clear");
    check(doc.calculation()->tasks[1].finish == "2026-11-04 14:00", "clear cache");
    auto *editor = dynamic_cast<AsyncEditController *>(main.findChild<QObject *>("asyncEditController"));
    check(editor, "editor");
    editor->beforeApply = [&] { button(*dialog, "cancelProgressPolicy")->click(); };
    adopt(*dialog, "adoptOverrideProgressPolicy");
    until([&] { return !editor->busy(); }, "cancel finish");
    check(!doc.project()->progressPolicy, "cancelled adoption committed");
    editor->beforeApply = {};
    button(*dialog, "refreshProgressPolicy")->click();
    ready(*dialog);
    adopt(*dialog, "adoptRetainedProgressPolicy");
    until([&] { return doc.project()->progressPolicy && dialog->property("progressPolicyReady").toBool(); }, "adopt A");
    check(doc.project()->progressPolicy->policy == retainedProgressPolicy, "wrong A");
    adopt(*dialog, "adoptOverrideProgressPolicy", [&] { doc.renameTask(0, "modal stale"); });
    check(doc.project()->progressPolicy->policy == retainedProgressPolicy, "stale metadata adopted");
    button(*dialog, "refreshProgressPolicy")->click();
    ready(*dialog);
    adopt(*dialog, "adoptOverrideProgressPolicy", [&] { doc.saveAs(temp.filePath("copy.ppm").toStdString()); });
    check(doc.project()->progressPolicy->policy == retainedProgressPolicy, "SaveAs during metadata adopted");
    dialog->reject();
    QTest::qWait(10);
    main.hide();
    Document readonly(store);
    readonly.open(path, true);
    MainWindow ro(readonly);
    ro.findChild<QAction *>("compareProgressPolicies")->trigger();
    auto *inspection = dynamic_cast<ProgressPolicyDialog *>(ro.findChild<QDialog *>("progressPolicyDialog"));
    ready(*inspection);
    check(!button(*inspection, "adoptRetainedProgressPolicy")->isEnabled() &&
              !button(*inspection, "adoptOverrideProgressPolicy")->isEnabled() &&
              !button(*inspection, "clearProgressPolicy")->isEnabled(),
          "readonly adoption enabled");
    inspection->reject();
    QTest::qWait(10);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    try {
        workflow();
        std::cout << "progress policy adoption UI/metadata/stale/table/Gantt/exports passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
