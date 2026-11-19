#include "ui/DatedRemainingWorkDialog.h"
#include "ui/MainWindow.h"
#include "persistence/JsonProjectStore.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDateEdit>
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
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void until(F f, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), s);
}
QPushButton *button(QDialog &d, const char *s) {
    auto *b = d.findChild<QPushButton *>(s);
    check(b, "button");
    return b;
}
void ready(QDialog &d) {
    until([&] { return button(d, "previewDatedRemainingWork")->isEnabled(); }, "basis load");
}
void preview(QDialog &d) {
    button(d, "previewDatedRemainingWork")->click();
    until([&] { return button(d, "applyDatedRemainingWork")->isEnabled(); }, "preview validation");
}
void advanced() {
    QTemporaryDir temp;
    JsonProjectStore store;
    QDir().mkpath("dated-effort-qa");
    for (auto type : {TaskType::FixedWork, TaskType::FixedUnits}) {
        Document doc(store);
        doc.create("고정 공수·단위 날짜 목표", "2026-11-02", {"설계"});
        Resource resource;
        resource.name = "설계 인력";
        resource.maxUnits = 200;
        const auto rid = doc.putResource(resource), tid = doc.project()->tasks[0].id;
        doc.setAssignments(tid, {{tid, rid, 100}});
        doc.setTaskEffort(0, type, true, 960, 96000, {});
        doc.setStatusAt("2026-11-02 09:00");
        const auto before = *doc.project();
        {
            DatedRemainingWorkDialog dialog(doc, tid);
            dialog.show();
            ready(dialog);
            auto *table = dialog.findChild<QTableWidget *>("datedRemainingTable");
            check(table->rowCount() == 2, "advanced inherited rows");
            table->item(0, 3)->setText("2");
            auto *date = dialog.findChild<QDateEdit *>("datedRemainingNewDate");
            check(date, "add date picker");
            date->setDate(QDate(2026, 11, 6));
            button(dialog, "addDatedRemainingDate")->click();
            check(table->rowCount() == 3, "future target row");
            table->item(2, 3)->setText("2");
            preview(dialog);
            check(*doc.project() == before, "advanced preview changed input");
            check(dialog.findChild<QLabel *>("datedRemainingMessage")->text().contains("2026-11-06 18:00"),
                  "inverse completion missing");
            const auto stem = type == TaskType::FixedWork ? "fixed-work" : "fixed-units";
            check(dialog.grab().save(QString("dated-effort-qa/") + stem + ".png"), "advanced screenshot");
            dialog.resize(800, 600);
            QTest::qWait(30);
            auto *apply = button(dialog, "applyDatedRemainingWork");
            check(dialog.rect().contains(QRect(apply->mapTo(&dialog, QPoint{}), apply->size())),
                  "advanced small footer");
            check(dialog.grab().save(QString("dated-effort-qa/") + stem + "-small.png"), "advanced small screenshot");
            table->item(0, 3)->setText("20");
            check(!apply->isEnabled(), "advanced stale certificate");
            button(dialog, "previewDatedRemainingWork")->click();
            ready(dialog);
            check(!apply->isEnabled() && *doc.project() == before, "infeasible total applied");
            table->item(0, 3)->setText("2");
            preview(dialog);
            apply->click();
            until([&] { return doc.project()->remainingWork.size() == 1; }, "advanced apply");
            ready(dialog);
            check(doc.project()->tasks == before.tasks && doc.project()->progress == before.progress,
                  "advanced facts changed");
            check(calculate(*doc.project()).tasks[0].finish == "2026-11-06 18:00", "advanced applied dates");
            check(table->rowCount() == 3 && !table->item(2, 4)->text().contains("구간 밖"),
                  "valid future date marked outside");
            dialog.reject();
        }
        doc.undo();
        check(doc.project()->remainingWork.empty() && doc.project()->tasks == before.tasks, "advanced undo");
        doc.redo();
        const auto path = temp.filePath(type == TaskType::FixedWork ? "work.ppm" : "units.ppm").toStdString();
        doc.saveAs(path);
        doc.open(path, true);
        DatedRemainingWorkDialog readonly(doc, tid);
        readonly.show();
        until([&] { return readonly.findChild<QTableWidget *>("datedRemainingTable")->rowCount() == 3; },
              "advanced readonly rows");
        check(!button(readonly, "addDatedRemainingDate")->isEnabled() &&
                  !button(readonly, "applyDatedRemainingWork")->isEnabled(),
              "advanced readonly actions");
        readonly.reject();
    }
    std::cout << "PASS dated effort UI: FixedWork/FixedUnits, future date input, inverse completion preview, invalid "
                 "total, preservation/undo and readonly, native/screens at default/800x600\n";
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("날짜별 잔여 화면", "2026-11-02", {"설계"});
    doc.setDuration(0, 1920);
    Resource r;
    r.name = "설계 인력";
    auto rid = doc.putResource(r);
    auto tid = doc.project()->tasks[0].id;
    doc.setAssignments(tid, {{tid, rid, 100}});
    doc.setStatusAt("2026-11-02 18:00");
    ProgressEntry e;
    e.taskId = tid;
    e.statusAt = *doc.project()->statusAt;
    e.recordedAt = "2026-11-06T01:00:00Z";
    e.reporter = "담당";
    e.reason = "관측";
    e.actualStart = "2026-11-02 09:00";
    e.actualDurationMinutes = 480;
    e.remainingMinutes = 1440;
    doc.recordProgress(e);
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    {
        DatedRemainingWorkDialog d(doc, tid);
        d.show();
        ready(d);
        const auto before = *doc.project();
        auto *table = d.findChild<QTableWidget *>("datedRemainingTable");
        check(table->rowCount() == 3 && table->item(0, 3)->text().isEmpty(), "initial inherited rows");
        table->item(0, 3)->setText("2");
        table->item(1, 3)->setText("0");
        table->item(2, 3)->setText("10");
        preview(d);
        check(*doc.project() == before, "preview committed");
        check(d.grab().save(output + "/remaining-editor.png"), "screen");
        d.resize(800, 600);
        QTest::qWait(20);
        auto *apply = button(d, "applyDatedRemainingWork");
        check(d.rect().contains(QRect(apply->mapTo(&d, QPoint{}), apply->size())), "small footer");
        check(d.grab().save(output + "/remaining-editor-small.png"), "small screenshot");
        table->item(0, 3)->setText("3");
        check(!apply->isEnabled(), "changed target kept certified result");
        table->item(0, 3)->setText("-1");
        button(d, "previewDatedRemainingWork")->click();
        check(!apply->isEnabled() && *doc.project() == before, "bad value applied");
        table->item(0, 3)->setText("2");
        preview(d);
        apply->click();
        until([&] { return doc.project()->revision == before.revision + 1; }, "commit");
        ready(d);
        check(doc.project()->remainingWork.size() == 1 && doc.project()->remainingWork[0].days.size() == 3,
              "saved goals");
        check(doc.project()->progress == before.progress && doc.project()->assignments == before.assignments,
              "altered facts or plan");
        button(d, "clearDatedRemainingWork")->click();
        until([&] { return doc.project()->remainingWork.empty(); }, "clear selected");
        ready(d);
        doc.undo();
        check(doc.project()->remainingWork.size() == 1 && !apply->isEnabled(), "undo/stale view");
        button(d, "reloadDatedRemainingWork")->click();
        ready(d);
        preview(d);
        doc.renameTask(0, "변경");
        check(!apply->isEnabled(), "stale certificate enabled");
        button(d, "reloadDatedRemainingWork")->click();
        ready(d);
        QThreadPool::globalInstance()->waitForDone();
        const int count = QThreadPool::globalInstance()->maxThreadCount();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        std::promise<void> release;
        auto wait = release.get_future().share();
        auto blocker = QtConcurrent::run([wait] { wait.wait(); });
        table->item(0, 3)->setText("4");
        const auto stable = *doc.project();
        button(d, "previewDatedRemainingWork")->click();
        button(d, "cancelDatedRemainingWork")->click();
        release.set_value();
        ready(d);
        check(!apply->isEnabled() && *doc.project() == stable, "cancelled result applied");
        QThreadPool::globalInstance()->setMaxThreadCount(count);
        doc.setWriteAuthorization([] { return false; });
        check(!button(d, "previewDatedRemainingWork")->isEnabled() && !apply->isEnabled(), "revoked role");
        d.reject();
        doc.setWriteAuthorization({});
    }
    {
        doc.setStatusAt("2026-11-05 18:00");
        DatedRemainingWorkDialog d(doc, tid);
        d.show();
        ready(d);
        auto *table = d.findChild<QTableWidget *>("datedRemainingTable");
        check(table->rowCount() > 3 && table->item(0, 4)->text().contains("구간 밖"), "outdated input hidden");
        button(d, "clearAllDatedRemainingWork")->click();
        until([&] { return doc.project()->remainingWork.empty(); }, "clear all");
        ready(d);
        d.reject();
        doc.undo();
    }
    const auto path = temp.filePath("dated.ppm").toStdString();
    doc.saveAs(path);
    doc.open(path, true);
    {
        DatedRemainingWorkDialog d(doc, tid);
        d.show();
        until([&] { return d.findChild<QTableWidget *>("datedRemainingTable")->rowCount() > 0; }, "readonly view");
        check(!button(d, "applyDatedRemainingWork")->isEnabled() &&
                  !button(d, "clearAllDatedRemainingWork")->isEnabled(),
              "readonly action");
        d.reject();
    }
    doc.open(path);
    {
        MainWindow w(doc);
        w.show();
        auto *a = w.findChild<QAction *>("datedRemainingWork");
        check(a, "menu");
        QTimer::singleShot(0, [] {
            auto *d = dynamic_cast<DatedRemainingWorkDialog *>(QApplication::activeModalWidget());
            if (!d)
                std::exit(2);
            d->reject();
        });
        a->trigger();
    }
    {
        DatedRemainingWorkDialog d(doc, tid);
        d.reject();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS dated remaining UI: inherited/zero, preview, edits invalidate certificate, atomic apply/clear, "
                 "undo, stale, cancel, role, outdated/readonly, close/menu\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
        advanced();
    } catch (const ppm::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
