#include "ui/AssignmentPeriodsDialog.h"
#include "ui/MainWindow.h"
#include "calculation/Effort.h"
#include "persistence/JsonProjectStore.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <iostream>
namespace {
using namespace jobs;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(QDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "missing button");
    return b;
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    d.create("기간별 계획 배정 화면", "2026-11-02", {"설계"});
    d.setDuration(0, 1440);
    Resource r;
    r.name = "설계 인력";
    r.maxUnits = 200;
    const auto rid = d.putResource(r);
    const auto tid = d.project()->tasks[0].id;
    d.setAssignments(tid, {{tid, rid, 100}});
    const auto output = QDir::currentPath() + "/assignment-periods-qa";
    QDir().mkpath(output);
    {
        AssignmentPeriodsDialog dialog(d, tid);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("assignmentPeriodsTable");
        until([&] { return button(dialog, "generateAssignmentPeriods")->isEnabled(); }, "preview did not load");
        button(dialog, "generateAssignmentPeriods")->click();
        check(table->rowCount() == 3 && table->item(0, 0)->text() == "0" && table->item(2, 1)->text() == "1440",
              "daily calendar rows");
        table->item(0, 2)->setText("150");
        table->item(2, 2)->setText("50");
        until([&] { return table->item(2, 3)->text().contains("2026-11-04"); }, "dated period labels");
        button(dialog, "applyAssignmentPeriods")->click();
        until(
            [&] {
                return d.project()->assignments[0].periods.size() == 3 &&
                       button(dialog, "applyAssignmentPeriods")->isEnabled();
            },
            "apply failed");
        check(d.project()->assignments[0].periods[0].units == 150, "rate edit");
        until([&] { return table->item(2, 3)->text().contains("2026-11-04"); }, "applied preview did not settle");
        check(dialog.grab().save(output + "/period-editor.png"), "editor screenshot");
        dialog.resize(800, 600);
        QTest::qWait(20);
        const auto *apply = button(dialog, "applyAssignmentPeriods");
        const QRect footer(apply->mapTo(&dialog, QPoint{}), apply->size());
        check(dialog.rect().contains(footer) && dialog.grab().save(output + "/period-editor-small.png"),
              "small editor footer clipped");
        const auto stable = *d.project();
        table->item(1, 0)->setText("0");
        button(dialog, "applyAssignmentPeriods")->click();
        check(*d.project() == stable, "invalid table partially applied");
        button(dialog, "resetAssignmentPeriods")->click();
        auto *curve = dialog.findChild<QComboBox *>("assignmentCurvePreset");
        curve->setCurrentIndex(1);
        button(dialog, "generateAssignmentCurve")->click();
        check(table->rowCount() > 3 && d.project()->assignments[0].periods.size() == 3,
              "preset committed without apply");
        button(dialog, "applyAssignmentPeriods")->click();
        button(dialog, "cancelAssignmentPeriods")->click();
        until([&] { return button(dialog, "applyAssignmentPeriods")->isEnabled(); }, "cancel did not finish");
        check(d.project()->assignments[0].periods == stable.assignments[0].periods, "cancel applied contour");
        button(dialog, "resetAssignmentPeriods")->click();
        auto *period = dialog.findChild<QComboBox *>("assignmentPeriodUnit");
        period->setCurrentIndex(1);
        until([&] { return button(dialog, "generateAssignmentPeriods")->isEnabled(); }, "week preview");
        button(dialog, "generateAssignmentPeriods")->click();
        check(table->rowCount() == 3, "week merged mixed rates");
        period->setCurrentIndex(2);
        button(dialog, "generateAssignmentPeriods")->click();
        check(table->rowCount() == 3, "month merged mixed rates");
        d.renameTask(0, "변경된 설계");
        check(!button(dialog, "applyAssignmentPeriods")->isEnabled(), "stale window editable");
        dialog.reject();
    }
    // Existing default-rate editor must retain the explicit curve when a rate changes.
    {
        MainWindow window(d);
        window.show();
        window.selectTaskById(tid);
        auto *menu = window.findChild<QAction *>("assignResources");
        check(menu && menu->isEnabled(), "assignment menu");
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                std::exit(2);
            dialog->findChild<QSpinBox *>("assignmentUnits_0")->setValue(80);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        menu->trigger();
        check(d.project()->assignments[0].units == 80 && d.project()->assignments[0].periods.size() == 3,
              "default assignment editor erased contour");
        auto *action = window.findChild<QAction *>("assignmentPeriods");
        check(action, "period menu missing");
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "assignmentPeriodsDialog")
                std::exit(2);
            dialog->reject();
        });
        action->trigger();
    }
    const auto path = temp.filePath("saved.ppm").toStdString();
    d.saveAs(path);
    {
        AssignmentPeriodsDialog dialog(d, tid);
        dialog.show();
        const auto before = *d.project();
        dialog.findChild<QComboBox *>("assignmentCurvePreset")->setCurrentIndex(2);
        button(dialog, "generateAssignmentCurve")->click();
        button(dialog, "applyAssignmentPeriods")->click();
        dialog.reject();
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(30);
        check(*d.project() == before, "closed dialog applied pending edit");
    }
    d.open(path, true);
    {
        AssignmentPeriodsDialog dialog(d, tid);
        dialog.show();
        check(!button(dialog, "applyAssignmentPeriods")->isEnabled() &&
                  !button(dialog, "generateAssignmentCurve")->isEnabled(),
              "readonly editor enabled");
        until([&] { return dialog.findChild<QLabel *>("assignmentPeriodsPreview")->text().contains(" h"); },
              "readonly preview unavailable");
        check(dialog.grab().save(output + "/period-readonly.png"), "readonly screenshot");
        dialog.reject();
    }
    d.open(path);
    d.setAssignmentPeriods(tid, rid, {});
    {
        AssignmentPeriodsDialog dialog(d, tid);
        dialog.show();
        until([&] { return button(dialog, "generateAssignmentPeriods")->isEnabled(); }, "flat preview");
        auto *period = dialog.findChild<QComboBox *>("assignmentPeriodUnit");
        period->setCurrentIndex(1);
        button(dialog, "generateAssignmentPeriods")->click();
        check(dialog.findChild<QTableWidget *>("assignmentPeriodsTable")->rowCount() == 1, "flat week split");
        dialog.reject();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "Assignment periods UI: period "
                 "rows/presets/manual/preview/apply/cancel/close/stale/readonly/default-rate preservation passed\n";
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
        run();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
