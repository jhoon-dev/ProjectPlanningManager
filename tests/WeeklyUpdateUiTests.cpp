#include "TaskTableTest.h"
#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/WeeklyUpdateDialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>

namespace {
void record(ppm::Document &doc, int row, int remaining, bool complete = false) {
    ppm::ProgressEntry e;
    e.taskId = doc.project()->tasks[row].id;
    e.actualStart = "2026-11-02 09:00";
    if (complete)
        e.actualFinish = "2026-11-02 14:00";
    e.actualDurationMinutes = complete ? 240 : 60;
    e.remainingMinutes = remaining;
    e.reporter = "PM";
    e.reason = "주간 확인";
    e.recordedAt = "2026-11-17T09:00:00Z";
    doc.recordProgress(e);
}
int rowFor(QTableWidget *table, const std::string &id) {
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString().toStdString() == id)
            return r;
    return -1;
}
QByteArray bytes(const std::string &path) {
    QFile f(QString::fromStdString(path));
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
} // namespace
class WeeklyUpdateUiTests : public QObject {
    Q_OBJECT
private slots:
    void classificationAndGuards() {
        QTemporaryDir dir;
        ppm::JsonProjectStore store;
        ppm::Document doc(store);
        doc.create("Weekly", "2026-11-02",
                   {"이전 완료", "이전 보고", "이번 주 보고", "현재 정정", "미보고", "미래 보고"});
        for (int i = 0; i < 6; ++i)
            doc.setDuration(i, 240);
        doc.captureBaseline("승인", "PM", "검토", "2026-11-02T00:00:00Z");
        doc.setStatusAt("2026-11-06 18:00");
        record(doc, 0, 0, true);
        record(doc, 1, 180);
        doc.setStatusAt("2026-11-11 18:00");
        record(doc, 2, 120);
        doc.setStatusAt("2026-11-13 18:00");
        record(doc, 3, 120);
        record(doc, 3, 30);
        doc.setStatusAt("2026-11-16 18:00");
        record(doc, 5, 60);
        doc.setStatusAt("2026-11-13 18:00");
        const auto path = dir.filePath("weekly.ppm").toStdString();
        doc.saveAs(path);
        const auto original = *doc.project();
        const auto disk = bytes(path);
        ppm::WeeklyUpdateDialog dialog(doc);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("weeklyTasks");
        auto *filter = dialog.findChild<QComboBox *>("weeklyFilter");
        auto *summary = dialog.findChild<QLabel *>("weeklySummary");
        QCOMPARE(table->rowCount(), 5);
        QVERIFY(dialog.findChild<QLabel *>("weeklyHeading")->text().contains("2026-11-09"));
        QVERIFY(summary->text().contains("현재 상태일 보고 1"));
        QVERIFY(summary->text().contains("이번 주 앞선 보고 1"));
        QVERIFY(summary->text().contains("이전 주 보고 2"));
        QVERIFY(summary->text().contains("미보고 2"));
        const auto id = original.tasks[3].id;
        QVERIFY(table->item(rowFor(table, id), 4)->text().endsWith("30m"));
        QVERIFY(table->item(rowFor(table, original.tasks[5].id), 2)->text().startsWith("미보고"));
        QCOMPARE(table->item(table->currentRow(), 0)->data(Qt::UserRole).toString(),
                 QString::fromStdString(original.tasks[1].id));
        filter->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 4);
        filter->setCurrentIndex(2);
        QCOMPARE(table->rowCount(), 2);
        filter->setCurrentIndex(3);
        QCOMPARE(table->rowCount(), 2);
        filter->setCurrentIndex(4);
        QCOMPARE(table->rowCount(), 6);
        auto *search = dialog.findChild<QLineEdit *>("weeklySearch");
        search->setText("미래");
        QCOMPARE(table->rowCount(), 1);
        search->clear();
        QVERIFY(*doc.project() == original && bytes(path) == disk && !doc.dirty());
        doc.setStatusAt({});
        QTRY_VERIFY(!dialog.findChild<QPushButton *>("weeklyEditProgress")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton *>("weeklyNext")->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("weeklyHeading")->text().contains("미설정"));
        QVERIFY(summary->text().contains("상태일을 선택한 뒤"));
        doc.open(path, true);
        QTRY_VERIFY(!dialog.isVisible());
        ppm::WeeklyUpdateDialog readonly(doc);
        readonly.show();
        QVERIFY(!readonly.findChild<QPushButton *>("weeklyEditProgress")->isEnabled());
        QVERIFY(!readonly.findChild<QPushButton *>("weeklySetStatus")->isEnabled());
    }
    void realEditorContinuesAndCancels() {
        QTemporaryDir dir;
        ppm::JsonProjectStore store;
        ppm::Document doc(store);
        doc.create("Weekly entry", "2026-11-09", {"첫 작업", "둘째 작업", "셋째 작업"});
        for (int i = 0; i < 3; ++i)
            doc.setDuration(i, 480);
        doc.captureBaseline("승인", "PM", "검토", "2026-11-09T00:00:00Z");
        doc.setStatusAt("2026-11-13 18:00");
        const auto original = *doc.project();
        ppm::MainWindow window(doc);
        window.show();
        // Main view excludes the second task; the weekly queue must still address its stable ID.
        window.findChild<QLineEdit *>("taskSearch")->setText("첫 작업");
        QTest::qWait(220);
        auto *open = window.findChild<QAction *>("weeklyUpdate");
        QVERIFY(open && open->isEnabled());
        auto *tools = window.findChild<QToolBar *>("primaryTools");
        QVERIFY(tools && tools->widgetForAction(open) && tools->widgetForAction(open)->isVisible());
        int step = 0;
        QTimer tick;
        tick.setInterval(40);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, [] { qFatal("weekly UI workflow timeout"); });
        watchdog.start(12000);
        connect(&tick, &QTimer::timeout, &window, [&] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!modal)
                return;
            if (modal->objectName() == "weeklyUpdateDialog") {
                auto *tasks = modal->findChild<QTableWidget *>("weeklyTasks");
                if (step == 0) {
                    QCOMPARE(tasks->rowCount(), 3);
                    step = 1;
                    QTimer::singleShot(0, modal,
                                       [modal] { modal->findChild<QPushButton *>("weeklyEditProgress")->click(); });
                } else if (step == 4) {
                    QCOMPARE(doc.project()->progress.size(), std::size_t{2});
                    QVERIFY(modal->findChild<QLabel *>("weeklySummary")->text().contains("현재 상태일 보고 2"));
                    QCOMPARE(tasks->item(tasks->currentRow(), 0)->data(Qt::UserRole).toString(),
                             QString::fromStdString(original.tasks[2].id));
                    step = 5;
                    modal->reject();
                }
                return;
            }
            if (modal->objectName() != "progressDialog")
                return;
            auto *buttons = modal->findChild<QDialogButtonBox *>();
            if (step == 1) {
                QVERIFY(buttons->button(QDialogButtonBox::Save)->text().contains("다음"));
                modal->findChild<QLineEdit *>("remainingMinutes")->setText("0.1m");
                buttons->button(QDialogButtonBox::Save)->click();
                QVERIFY(doc.project()->progress.empty());
                QVERIFY(!modal->findChild<QLabel *>("progressError")->text().isEmpty());
                modal->findChild<QLineEdit *>("actualStart")->setText("2026-11-09 09:00");
                modal->findChild<QLineEdit *>("actualMinutes")->setText("1d");
                modal->findChild<QLineEdit *>("remainingMinutes")->setText("1d");
                modal->findChild<QLineEdit *>("progressReporter")->setText("PM");
                modal->findChild<QLineEdit *>("progressReason")->setText("첫 확인");
                step = 2;
                buttons->button(QDialogButtonBox::Save)->click();
            } else if (step == 2 && doc.project()->progress.size() == 1 &&
                       modal->property("progressTaskId").toString() == QString::fromStdString(original.tasks[1].id)) {
                QVERIFY(modal->findChild<QLineEdit *>("actualStart")->text().isEmpty());
                modal->findChild<QLineEdit *>("remainingMinutes")->setText("2d");
                modal->findChild<QLineEdit *>("progressReporter")->setText("PM");
                modal->findChild<QLineEdit *>("progressReason")->setText("둘째 확인");
                step = 3;
                buttons->button(QDialogButtonBox::Save)->click();
            } else if (step == 3 && doc.project()->progress.size() == 2 &&
                       modal->property("progressTaskId").toString() == QString::fromStdString(original.tasks[2].id)) {
                modal->findChild<QLineEdit *>("remainingMinutes")->setText("3d");
                step = 4;
                buttons->button(QDialogButtonBox::Cancel)->click();
            }
        });
        tick.start();
        open->trigger();
        tick.stop();
        watchdog.stop();
        QCOMPARE(step, 5);
        QCOMPARE(doc.project()->progress[0].taskId, original.tasks[0].id);
        QCOMPARE(doc.project()->progress[1].taskId, original.tasks[1].id);
        QCOMPARE(doc.project()->progress[1].remainingMinutes, std::int64_t{960});
        QVERIFY(doc.project()->baselines == original.baselines && doc.project()->tasks == original.tasks);
        const auto path = dir.filePath("saved.ppm").toStdString();
        doc.saveAs(path);
        const auto disk = bytes(path);
        QTimer::singleShot(60, &window, [&] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(d);
            QVERIFY(d->findChild<QLabel *>("weeklySummary")->text().contains("현재 상태일 보고 2"));
            d->reject();
        });
        open->trigger();
        QVERIFY(bytes(path) == disk && !doc.dirty());
        doc.open(path);
        QCOMPARE(doc.project()->progress.size(), std::size_t{2});
        QVERIFY(doc.project()->baselines == original.baselines);
    }
};
QTEST_MAIN(WeeklyUpdateUiTests)
#include "WeeklyUpdateUiTests.moc"
