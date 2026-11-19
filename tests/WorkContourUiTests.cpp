#include "ui/AssignmentPeriodsDialog.h"
#include "ui/MainWindow.h"
#include "calculation/Effort.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <iostream>
namespace {
using namespace ppm;
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void until(F f, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), s);
}
QPushButton *button(QDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "button");
    return b;
}
Project fixture(TaskType type) {
    Project p;
    p.documentId = "work-contour-ui";
    p.name = "공수·단위 곡선 역산";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}};
    p.tasks[0].taskType = type;
    if (type == TaskType::FixedWork) {
        p.tasks[0].workDriven = true;
        p.tasks[0].workUnitMinutes = 48000;
    }
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.maxUnits = 200;
    p.resources = {r};
    p.assignments = {{"T", "R", 100, {{0, 240, 50}, {240, 480, 150}}}};
    p.taskCosts = {{"T", 0}};
    return p;
}
void run() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto output = QDir::currentPath() + "/work-contour-qa";
    QDir().mkpath(output);
    for (auto type : {TaskType::FixedWork, TaskType::FixedUnits}) {
        const auto p = fixture(type);
        const auto path = tmp.filePath(type == TaskType::FixedWork ? "work.ppm" : "units.ppm").toStdString();
        store.save(path, p, {});
        Document d(store);
        d.open(path);
        {
            AssignmentPeriodsDialog dialog(d, "T");
            dialog.show();
            auto *table = dialog.findChild<QTableWidget *>("assignmentPeriodsTable");
            auto *preview = dialog.findChild<QLabel *>("assignmentPeriodsPreview");
            check(table && preview, "curve widgets");
            until([&] { return button(dialog, "generateAssignmentPeriods")->isEnabled(); }, "initial work preview");
            check(button(dialog, "applyAssignmentPeriods")->isEnabled() &&
                      !button(dialog, "editPlannedWork")->isEnabled(),
                  "work curve controls");
            const auto source = d.snapshot();
            const auto rate = type == TaskType::FixedWork ? "100" : "25";
            table->item(0, 2)->setText(rate);
            const auto expected = type == TaskType::FixedWork ? "2026-11-02 16:40" : "2026-11-03 10:00";
            until([&] { return preview->text().contains(expected); }, "inverse preview dates");
            check(d.snapshot() == source && !d.dirty(), "preview mutated document");
            dialog.grab().save(output + (type == TaskType::FixedWork ? "/fixed-work.png" : "/fixed-units.png"));
            dialog.resize(800, 600);
            QTest::qWait(20);
            check(dialog.grab().save(
                      output + (type == TaskType::FixedWork ? "/fixed-work-small.png" : "/fixed-units-small.png")),
                  "small screen");
            button(dialog, "applyAssignmentPeriods")->click();
            until([&] { return d.project()->assignments[0].periods[0].units == std::stoi(rate); }, "curve apply");
            check(d.project()->tasks[0].workUnitMinutes == 48000 &&
                      calculatePlan(*d.project()).tasks[0].finish == expected,
                  "UI applied different work basis");
            d.undo();
            check(d.project()->assignments == p.assignments, "UI curve undo");
            dialog.reject();
        }
        d.open(path);
        {
            AssignmentPeriodsDialog dialog(d, "T");
            dialog.show();
            auto *table = dialog.findChild<QTableWidget *>("assignmentPeriodsTable");
            until([&] { return button(dialog, "generateAssignmentPeriods")->isEnabled(); }, "reload");
            table->item(0, 2)->setText("75");
            const auto before = d.snapshot();
            dialog.reject();
            QTest::qWait(30);
            check(d.snapshot() == before, "cancel committed");
        }
        {
            AssignmentPeriodsDialog dialog(d, "T");
            dialog.show();
            d.renameTask(0, "문서 변경");
            check(!button(dialog, "applyAssignmentPeriods")->isEnabled(), "stale editor enabled");
            dialog.reject();
        }
        d.open(path, true);
        {
            AssignmentPeriodsDialog dialog(d, "T");
            dialog.show();
            check(!button(dialog, "applyAssignmentPeriods")->isEnabled(), "readonly inverse enabled");
            dialog.reject();
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS work contour UI: FW/FU preview equals atomic apply, exact work and actual duration, "
                 "draft/cancel/undo/stale/readonly, bounded screen\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
