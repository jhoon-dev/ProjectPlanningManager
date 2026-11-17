#include "TaskTableTest.h"
#include "calculation/Effort.h"
#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/TaskInput.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class DurationInputTests : public QObject {
    Q_OBJECT
private slots:
    void exactUnits() {
        for (const auto &text : {"2", "2d", "2일", "16h", "16시간", "960m", "960분", " 2 D "})
            QCOMPARE(jobs::taskDurationInput(text), std::optional<std::int64_t>{960});
        QCOMPARE(jobs::taskDurationInput("1.5d"), std::optional<std::int64_t>{720});
        QCOMPARE(jobs::taskDurationInput("0.125d"), std::optional<std::int64_t>{60});
        QCOMPARE(jobs::taskDurationInput("2d", 420), std::optional<std::int64_t>{840});
        QCOMPARE(jobs::taskDurationInput("2d", 1440), std::optional<std::int64_t>{2880});
        QCOMPARE(jobs::taskDurationInput("5000000m"), std::optional<std::int64_t>{5000000});
        QVERIFY(!jobs::taskDurationInput(" "));
        for (const auto &bad :
             {"-1", "1ed", "1w", "nan", "1e3", "0.001d", "0.1m", "5000001m", "999999999999999999999999999999999999d"})
            QVERIFY_EXCEPTION_THROWN(jobs::taskDurationInput(bad), std::exception);
        QVERIFY_EXCEPTION_THROWN(jobs::taskDurationInput("1d", 0), std::exception);
        QCOMPARE(jobs::taskDurationInput("1h", 0), std::optional<std::int64_t>{60});
    }
    void exactDisplayRoundtrip() {
        for (int day : {240, 420, 450, 480, 600, 1440})
            for (std::int64_t minutes : {0, 1, 11, 60, 210, 420, 480, 960, 1001, 5000000})
                QCOMPARE(jobs::taskDurationInput(jobs::taskDurationText(minutes, day), day),
                         std::optional<std::int64_t>{minutes});
        QCOMPARE(jobs::taskDurationText(960), QString("2d"));
        QCOMPARE(jobs::taskDurationText(1), QString("1m"));
        QCOMPARE(jobs::taskDurationText(60, 420), QString("1h"));
    }
    void sevenHourWorkflow() {
        QTemporaryDir directory;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("Seven hour days", "2026-11-09", {"설비 설치"});
        jobs::TaskCalendar calendar;
        calendar.name = "7시간 현장";
        calendar.periods = {{540, 720}, {780, 1020}};
        const auto id = doc.putCalendar(calendar);
        doc.setTaskCalendar(0, id);
        doc.setStatusAt("2026-11-10 17:00");
        jobs::MainWindow window(doc);
        window.show();
        auto *table = taskTable(window);
        QVERIFY(table);
        table->setCurrentCell(0, 1);
        table->selectRow(0);
        QVERIFY(setCellText(table, 0, 1, "2"));
        QCOMPARE(doc.project()->tasks[0].durationMinutes, std::optional<std::int64_t>{840});
        QCOMPARE(cellText(table, 0, 1), QString("2d"));
        QVERIFY(window.findChild<QLabel *>("durationInputHint")->text().contains("1d=7h"));
        const auto before = *doc.project();
        for (const auto &value : {"14h", "840m", "2d"}) {
            QVERIFY(setCellText(table, 0, 1, value));
            QVERIFY(*doc.project() == before);
        }
        const auto calculated = jobs::calculatePlan(*doc.project());
        QCOMPARE(calculated.status, jobs::CalculationStatus::Valid);
        QCOMPARE(calculated.tasks[0].finish, std::string("2026-11-10 17:00"));
        table->selectRow(0);
        QAction *action = nullptr;
        for (auto *candidate : window.findChildren<QAction *>())
            if (candidate->text() == "선택 작업 실적")
                action = candidate;
        QVERIFY(action);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->reject();
        });
        watchdog.start(10000);
        bool visited = false;
        QTimer::singleShot(60, &window, [&] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(d);
            const auto closeOnFailure = qScopeGuard([&] {
                if (!visited)
                    d->reject();
            });
            QVERIFY(d->findChild<QLabel *>("progressDurationHelp")->text().contains("1d=7h"));
            auto *actual = d->findChild<QLineEdit *>("actualMinutes");
            auto *remaining = d->findChild<QLineEdit *>("remainingMinutes");
            QVERIFY(actual && remaining);
            QCOMPARE(remaining->text(), QString("2d"));
            d->findChild<QLineEdit *>("actualStart")->setText("2026-11-09 09:00");
            d->findChild<QLineEdit *>("progressReporter")->setText("PM");
            d->findChild<QLineEdit *>("progressReason")->setText("주간 확인");
            actual->setText("0.1m");
            auto *save = d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            save->click();
            QVERIFY(doc.project()->progress.empty());
            QVERIFY(!d->findChild<QLabel *>("progressError")->text().isEmpty());
            auto *error = d->findChild<QLabel *>("progressError");
            auto *scroll = d->findChild<QScrollArea *>("editorScrollArea");
            QVERIFY(
                scroll->viewport()->rect().contains(QRect(error->mapTo(scroll->viewport(), QPoint()), error->size())));
            actual->setText("1d");
            remaining->setText("7h");
            visited = true;
            save->click();
        });
        action->trigger();
        QVERIFY(visited);
        QCOMPARE(doc.project()->progress.size(), std::size_t{1});
        QCOMPARE(doc.project()->progress.back().actualDurationMinutes, std::int64_t{420});
        QCOMPARE(doc.project()->progress.back().remainingMinutes, std::int64_t{420});
        const auto path = directory.filePath("days.ppm").toStdString();
        doc.saveAs(path);
        doc.open(path);
        QCOMPARE(doc.project()->tasks[0].durationMinutes, std::optional<std::int64_t>{840});
        QCOMPARE(doc.project()->progress.back().remainingMinutes, std::int64_t{420});
    }
};
QTEST_MAIN(DurationInputTests)
#include "DurationInputTests.moc"
