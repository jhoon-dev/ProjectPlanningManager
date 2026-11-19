#include "ProgressLevelingFixture.h"
#include "TaskTableTest.h"
#include "ui/MainWindow.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/ProgressPolicyDialog.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include "reporting/SchedulePrint.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *m) {
    if (!ok)
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
void output(Document &doc) {
    QDir().mkpath("progress-leveling-qa");
    const auto p = doc.snapshot();
    JsonProjectStore store;
    QFile::remove("progress-leveling-qa/combined.ppm");
    store.save("progress-leveling-qa/combined.ppm", *p, {});
    ReportOptions opt;
    opt.reportId = "combined-policy-leveling";
    opt.generatedAt = "2026-11-03T03:00:00Z";
    const auto report = buildReport(*p, opt);
    const auto usage = usageReport(buildUsage(*p, {"2026-11-02", "2026-11-06", {}, {}, UsagePeriod::Day}),
                                   UsageHierarchy::Tasks, "combined-usage", opt.generatedAt, true);
    PdfOptions pdf;
    pdf.font = QApplication::font();
    for (const auto &entry :
         std::vector<std::pair<QString, const ProjectReport *>>{{"schedule", &report}, {"usage", &usage}}) {
        const auto stem = "progress-leveling-qa/" + entry.first;
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
    source.printId = "combined-print";
    source.columns = {{"number", "번호", 36}, {"name", "작업", 140}, {"start", "시작", 110}, {"finish", "완료", 110}};
    for (std::size_t i = 0; i < p->tasks.size(); ++i)
        source.rows.push_back(
            {p->tasks[i].id,
             int(i + 1),
             {QString::number(i + 1), QString::fromStdString(p->tasks[i].name), "STALE_START", "STALE_FINISH"}});
    SchedulePrintOptions options;
    options.font = QApplication::font();
    const auto layout = buildSchedulePrint(source, options);
    check(layout.rows[3].cells[2] == "2026-11-03 14:00" && layout.rows[3].cells[3] == "2026-11-04 14:00",
          "print ignored combined schedule");
    QFile::remove("progress-leveling-qa/print.pdf");
    exportSchedulePdf(layout, "progress-leveling-qa/print.pdf");
}
void workflow() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("source.ppm").toStdString();
    store.save(path, combined_fixture::project(), {});
    Document doc(store);
    doc.open(path);
    ResourceComparisonDialog leveling(doc);
    leveling.show();
    until([&] { return button(leveling, "adoptLeveling")->isEnabled(); }, "policy leveling preview disabled");
    check(leveling.findChild<QLabel *>("levelingSummary")->text().contains("native-progress-override-v1"),
          "preview policy not identified");
    leveling.findChild<QLineEdit *>("levelingReason")->setText("자원 중복 배정 해소");
    button(leveling, "adoptLeveling")->click();
    until([&] { return doc.project()->remainingLeveling && button(leveling, "adoptLeveling")->isEnabled(); },
          "combined adoption");
    MainWindow main(doc);
    main.resize(1400, 900);
    main.show();
    QTest::qWait(30);
    auto *table = taskTable(main);
    check(cellText(table, 3, 3) == "2026-11-03 14:00" && cellText(table, 3, 4) == "2026-11-04 14:00",
          "table combined dates");
    check(cellText(table, 3, 5) == "0" && cellText(table, 3, 6) == "평준화 임계", "combined table leaked float");
    output(doc);
    check(leveling.grab().save("progress-leveling-qa/comparison.png"), "preview screenshot");
    check(main.grab().save("progress-leveling-qa/schedule.png"), "main screenshot");
    leveling.findChild<QLineEdit *>("scenarioName")->setText("B 정책 자원 배치");
    button(leveling, "saveScenario")->click();
    until([&] { return doc.project()->scenarios.size() == 1 && !button(leveling, "cancelLeveling")->isEnabled(); },
          "scenario save");
    check(doc.project()->scenarios.back().progressPolicy == doc.project()->progressPolicy, "UI scenario lost policy");
    main.findChild<QAction *>("compareProgressPolicies")->trigger();
    auto *policy = dynamic_cast<ProgressPolicyDialog *>(main.findChild<QDialog *>("progressPolicyDialog"));
    check(policy, "policy dialog");
    until([&] { return policy->property("progressPolicyReady").toBool(); }, "comparison blocked by leveling");
    check(button(*policy, "adoptRetainedProgressPolicy")->isEnabled(), "A comparison/adoption blocked");
    QTimer metadata;
    metadata.setInterval(10);
    QObject::connect(&metadata, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (!modal || modal->objectName() != "progressPolicyMetadata")
            return;
        bool warning = false;
        for (auto *label : modal->findChildren<QLabel *>())
            if (label->text().contains("기존 평준화 채택을 해제"))
                warning = true;
        check(warning, "policy switch release notice missing");
        modal->findChild<QLineEdit *>("progressPolicyAdopter")->setText("PM");
        modal->findChild<QLineEdit *>("progressPolicyReason")->setText("A 논리 재검토");
        metadata.stop();
        modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    metadata.start();
    button(*policy, "adoptRetainedProgressPolicy")->click();
    until(
        [&] {
            return doc.project()->progressPolicy->policy == retainedProgressPolicy && !doc.project()->remainingLeveling;
        },
        "policy switch retained stale leveled selection");
    check(doc.calculation()->tasks[1].finish == "2026-11-04 14:00", "policy switch cached wrong result");
    policy->reject();
    QTest::qWait(10);
    leveling.reject();
    main.hide();
    Document ro(store);
    ro.open("progress-leveling-qa/combined.ppm", true);
    ResourceComparisonDialog readonly(ro);
    readonly.show();
    until([&] { return !button(readonly, "cancelLeveling")->isEnabled(); }, "readonly preview");
    check(!button(readonly, "adoptLeveling")->isEnabled() && !button(readonly, "clearLeveling")->isEnabled() &&
              !button(readonly, "saveScenario")->isEnabled(),
          "readonly combined editing enabled");
    readonly.reject();
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
    try {
        workflow();
        std::cout << "combined policy leveling preview/adoption/scenario/switch/readonly/exports passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
