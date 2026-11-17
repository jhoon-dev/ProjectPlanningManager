#include "ui/ActivityDialog.h"
#include "ui/MainWindow.h"
#include "ui/ProgressPolicyDialog.h"
#include "domain/Activities.h"
#include "persistence/JsonProjectStore.h"
#include "ProgressPolicyFixture.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace jobs;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void until(const std::function<bool()> &ready) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 30000)
        QTest::qWait(10);
    check(ready(), "UI timeout");
}
QPushButton *button(QWidget &d, const char *id) {
    auto *b = d.findChild<QPushButton *>(id);
    check(b, "button missing");
    return b;
}
void run() {
    QDir().mkpath("activity-extension-qa");
    QTemporaryDir temp;
    JsonProjectStore files;
    Document doc(files);
    doc.create("Steps 화면", "2026-11-02", {"설계", "시공"});
    doc.setDuration(0, 480);
    doc.setDuration(1, 480);
    doc.setStatusAt("2026-11-02 18:00");
    const auto taskId = doc.project()->tasks[0].id;
    ActivityDialog dialog(doc, taskId);
    dialog.show();
    QTest::qWait(30);
    auto *table = dialog.findChild<QTableWidget *>("activitySteps");
    check(table, "step table");
    button(dialog, "addActivityStep")->click();
    button(dialog, "addActivityStep")->click();
    table->item(0, 1)->setText("도면");
    table->item(1, 1)->setText("승인");
    table->item(0, 2)->setText("1");
    table->item(1, 2)->setText("3");
    button(dialog, "saveActivitySteps")->click();
    check(doc.project()->activities.size() == 1, "UI definition save");
    auto named = doc.project()->activities[0];
    named.name = "사용자 Steps 그룹";
    doc.putActivity(named);
    button(dialog, "refreshActivities")->click();
    table->item(0, 3)->setText("100.00");
    table->item(1, 3)->setText("0.00");
    dialog.findChild<QLineEdit *>("stepReporter")->setText("PM");
    dialog.findChild<QLineEdit *>("stepReason")->setText("현장 확인");
    button(dialog, "recordActivitySteps")->click();
    check(doc.project()->stepReports.size() == 1 &&
              stepsPhysicalAsOf(*doc.project(), taskId, *doc.project()->statusAt) == 2500,
          "UI weighted report");
    check(doc.project()->activities[0].name == "사용자 Steps 그룹", "report changed existing Steps group name");
    check(dialog.findChild<QLabel *>("activityProgressMetrics")->text().contains("25.00%"), "physical metric missing");
    dialog.grab().save("activity-extension-qa/steps.png");
    auto *tabs = dialog.findChild<QTabWidget *>();
    tabs->setCurrentIndex(1);
    dialog.findChild<QLineEdit *>("supportActivityName")->setText("현장 지원");
    dialog.findChild<QComboBox *>("supportFinishTask")->setCurrentIndex(1);
    button(dialog, "saveSupportActivity")->click();
    check(doc.project()->activities.size() == 2, "UI support save");
    auto *intervals = dialog.findChild<QTableWidget *>("supportActivityIntervals");
    check(intervals->rowCount() == 1 && !intervals->item(0, 2)->text().isEmpty(), "support preview rows");
    dialog.findChild<QComboBox *>("supportActivityKind")->setCurrentIndex(1);
    check(!dialog.findChild<QComboBox *>("supportStartTask")->isEnabled() &&
              !dialog.findChild<QComboBox *>("supportFinishTask")->isEnabled() &&
              dialog.findChild<QComboBox *>("supportWbs")->isEnabled(),
          "WBS summary exposed unused anchor inputs");
    dialog.findChild<QLineEdit *>("supportActivityName")->setText("프로젝트 요약");
    button(dialog, "saveSupportActivity")->click();
    check(doc.project()->activities.size() == 3 && intervals->rowCount() == 2, "WBS summary save");
    dialog.grab().save("activity-extension-qa/support.png");
    doc.undo();
    button(dialog, "saveSupportActivity")->click();
    check(doc.project()->activities.size() == 2 &&
              dialog.findChild<QLabel *>("activityExtensionStatus")->text().contains("WORKSPACE_SOURCE_CHANGED"),
          "stale UI edit accepted");
    button(dialog, "refreshActivities")->click();
    dialog.accept();
    const auto saved = temp.filePath("steps.ppm").toStdString();
    doc.saveAs(saved);
    Document ro(files);
    ro.open(saved, true);
    ActivityDialog readOnly(ro, taskId);
    readOnly.show();
    for (const auto *id : {"saveActivitySteps", "recordActivitySteps", "saveSupportActivity", "deleteSupportActivity"})
        check(!button(readOnly, id)->isEnabled(), "readonly activity mutation enabled");
    readOnly.accept();
    auto source = progress_fixture::small(effortProfile);
    source.tasks[1].predecessors.push_back("D");
    source.tasks[1].dependencyRules.push_back({"D", DependencyType::FF, 0});
    const auto path = temp.filePath("actual-source.ppm").toStdString();
    files.save(path, source, {});
    Document actual(files);
    actual.open(path);
    MainWindow main(actual);
    main.show();
    main.findChild<QAction *>("compareProgressPolicies")->trigger();
    auto *policy = dynamic_cast<ProgressPolicyDialog *>(main.findChild<QDialog *>("progressPolicyDialog"));
    check(policy, "policy dialog");
    until([&] { return policy->property("progressPolicyReady").toBool(); });
    auto *adopt = button(*policy, "adoptActualDatesProgressPolicy");
    check(adopt->isEnabled(), "Actual Dates adoption disabled");
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (!modal || modal->objectName() != "progressPolicyMetadata")
            return;
        modal->findChild<QLineEdit *>("progressPolicyAdopter")->setText("PM");
        modal->findChild<QLineEdit *>("progressPolicyReason")->setText("실제 끝점 검토");
        timer.stop();
        modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    adopt->click();
    until([&] {
        return actual.project()->progressPolicy.has_value() && policy->property("progressPolicyReady").toBool();
    });
    check(actual.project()->progressPolicy->policy == actualDatesProgressPolicy, "Actual Dates not adopted");
    const auto result = calculate(*actual.project());
    bool golden = false;
    for (const auto &r : result.tasks)
        if (r.taskId == "B")
            golden = r.finish == "2026-11-03 18:00";
    check(golden, "UI adopted wrong endpoint mask");
    for (auto *view : policy->findChildren<QTableView *>())
        if (view->model()->columnCount() == 21)
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum());
    policy->grab().save("activity-extension-qa/actual-dates.png");
    const auto out = QDir::currentPath() + "/activity-extension-qa/actual-dates.ppm";
    if (QFile::exists(out))
        check(QFile::remove(out), "old policy fixture");
    files.save(out.toStdString(), *actual.project(), {});
    policy->reject();
    main.hide();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    if (QFileInfo::exists("C:/Windows/Fonts/malgun.ttf")) {
        const int f = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
        if (f >= 0)
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(f).front(), 10));
    }
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "activity UI watchdog\n";
        std::_Exit(2);
    });
    watchdog.start(120000);
    try {
        run();
        std::cout << "PASS: Steps/support UI, readonly/stale, Actual Dates adoption and screenshots\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
