#include "ui/AssignmentPeriodsDialog.h"
#include "ui/PlannedWorkDialog.h"
#include "calculation/Effort.h"
#include "persistence/JsonProjectStore.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <future>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void until(F f, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), message);
}
QPushButton *button(QDialog &d, const char *name) {
    auto *b = d.findChild<QPushButton *>(name);
    check(b, "missing button");
    return b;
}
void ready(PlannedWorkDialog &d) {
    until([&] { return button(d, "convertPlannedWork")->isEnabled(); }, "load did not finish");
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("계획 공수 화면", "2026-11-02", {"설계"});
    doc.setDuration(0, 1440);
    Resource r;
    r.name = "설계 인력";
    auto rid = doc.putResource(r);
    auto tid = doc.project()->tasks[0].id;
    doc.setAssignments(tid, {{tid, rid, 100}});
    const auto output = QDir::currentPath() + "/planned-work-qa";
    QDir().mkpath(output);
    {
        const auto before = *doc.project();
        PlannedWorkDialog d(doc, doc.snapshot(), tid, rid);
        d.show();
        ready(d);
        auto *table = d.findChild<QTableWidget *>("plannedWorkTable");
        check(table->rowCount() == 3 && table->item(0, 3)->text() == "8.0000", "initial rows");
        table->item(0, 4)->setText("");
        button(d, "convertPlannedWork")->click();
        check(!button(d, "usePlannedWork")->isEnabled() && *doc.project() == before, "blank applied");
        table->item(0, 4)->setText("2");
        table->item(1, 4)->setText("0");
        table->item(2, 4)->setText("10");
        button(d, "convertPlannedWork")->click();
        until([&] { return button(d, "usePlannedWork")->isEnabled(); }, "conversion did not finish");
        check(*doc.project() == before && d.converted()->periods.size() == 3, "preview mutated document");
        check(d.grab().save(output + "/planned-work.png"), "screenshot");
        d.resize(800, 600);
        QTest::qWait(20);
        auto *use = button(d, "usePlannedWork");
        check(d.rect().contains(QRect(use->mapTo(&d, QPoint{}), use->size())), "small footer clipped");
        check(d.grab().save(output + "/planned-work-small.png"), "small screenshot");
        table->item(0, 4)->setText("3");
        check(!d.converted() && !button(d, "usePlannedWork")->isEnabled(), "edited target kept old result");
        d.findChild<QComboBox *>("plannedWorkPeriod")->setCurrentIndex(1);
        ready(d);
        check(table->rowCount() == 1 && table->item(0, 4)->text() == "24.0000", "unit reset");
        d.reject();
        check(*doc.project() == before, "cancel applied");
    }
    {
        AssignmentPeriodsDialog parent(doc, tid);
        parent.show();
        const auto before = *doc.project();
        bool converted = false;
        QTimer::singleShot(0, [&] {
            auto *d = dynamic_cast<PlannedWorkDialog *>(QApplication::activeModalWidget());
            check(d, "child modal missing");
            ready(*d);
            auto *table = d->findChild<QTableWidget *>("plannedWorkTable");
            table->item(0, 4)->setText("2");
            table->item(1, 4)->setText("0");
            table->item(2, 4)->setText("10");
            button(*d, "convertPlannedWork")->click();
            until([&] { return button(*d, "usePlannedWork")->isEnabled(); }, "modal conversion");
            button(*d, "usePlannedWork")->click();
            converted = true;
        });
        button(parent, "editPlannedWork")->click();
        check(converted && *doc.project() == before, "child committed directly");
        auto *table = parent.findChild<QTableWidget *>("assignmentPeriodsTable");
        check(table->rowCount() == 3 && table->item(0, 2)->text() == "25", "parent draft missing");
        button(parent, "applyAssignmentPeriods")->click();
        until([&] { return doc.project()->revision == before.revision + 1; }, "document apply");
        check(doc.project()->assignments[0].periods[2].units == 125, "applied rate");
        parent.reject();
        doc.undo();
        check(doc.project()->assignments == before.assignments, "single undo");
        doc.redo();
    }
    {
        PlannedWorkDialog d(doc, doc.snapshot(), tid, rid);
        d.show();
        ready(d);
        const auto before = *doc.project();
        auto *table = d.findChild<QTableWidget *>("plannedWorkTable");
        table->item(0, 4)->setText("3");
        QThreadPool::globalInstance()->waitForDone();
        const int previous = QThreadPool::globalInstance()->maxThreadCount();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        std::promise<void> release;
        auto hold = release.get_future().share();
        auto blocker = QtConcurrent::run([hold] { hold.wait(); });
        button(d, "convertPlannedWork")->click();
        button(d, "cancelPlannedWork")->click();
        release.set_value();
        ready(d);
        check(!d.converted() && *doc.project() == before, "cancelled conversion applied");
        QThreadPool::globalInstance()->setMaxThreadCount(previous);
        button(d, "convertPlannedWork")->click();
        doc.renameTask(0, "새 이름");
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(20);
        check(!d.converted() && !button(d, "usePlannedWork")->isEnabled(), "stale conversion enabled");
        d.reject();
    }
    {
        PlannedWorkDialog d(doc, doc.snapshot(), tid, rid);
        d.show();
        ready(d);
        doc.setWriteAuthorization([] { return false; });
        check(!button(d, "convertPlannedWork")->isEnabled() && !button(d, "usePlannedWork")->isEnabled(),
              "revoked role editable");
        d.reject();
        doc.setWriteAuthorization({});
    }
    const auto path = temp.filePath("work.ppm").toStdString();
    doc.saveAs(path);
    doc.open(path, true);
    {
        AssignmentPeriodsDialog d(doc, tid);
        d.show();
        check(!button(d, "editPlannedWork")->isEnabled(), "readonly work edit enabled");
        d.reject();
    }
    doc.open(path);
    {
        PlannedWorkDialog d(doc, doc.snapshot(), tid, rid);
        d.show();
        button(d, "cancelPlannedWork")->click();
        until([&] { return button(d, "reloadPlannedWork")->isEnabled(); }, "cancel initial load");
        check(!d.converted(), "cancelled initial load result");
        button(d, "reloadPlannedWork")->click();
        ready(d);
        check(d.findChild<QTableWidget *>("plannedWorkTable")->rowCount() == 3, "reload after cancellation");
        d.reject();
    }
    {
        PlannedWorkDialog d(doc, doc.snapshot(), tid, rid);
        d.reject();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS planned work UI: preview, draft transfer, atomic apply, undo, input invalidation, cancel, "
                 "stale, role, readonly, close\n";
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
    } catch (const ppm::AppError &e) {
        std::cerr << "FAIL: " << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        std::cerr << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
