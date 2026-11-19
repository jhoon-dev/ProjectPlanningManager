#include "TaskTableTest.h"
#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/TaskTableModel.h"
#include "ui/GanttView.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace {
QByteArray bytes(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
void capture(QWidget &widget, const QString &name) {
    const auto folder = qEnvironmentVariable("PPM_JOURNEY_AUDIT_DIR");
    if (folder.isEmpty())
        return;
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(widget.grab().save(QDir(folder).filePath(name + ".png")));
}
int rowFor(QTableWidget *table, const std::string &id) {
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString().toStdString() == id)
            return row;
    return -1;
}
QAction *actionNamed(ppm::MainWindow &window, const QString &name) {
    for (auto *action : window.findChildren<QAction *>())
        if (action->text() == name)
            return action;
    return nullptr;
}
} // namespace
class WeeklyJourneyUiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        const auto id =
            QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
        QVERIFY(id >= 0);
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    }
    void weeklyJourney_data() {
        QTest::addColumn<QString>("fixturePath");
        QTest::addColumn<QString>("tag");
        QTest::newRow("five") << QFINDTESTDATA(
                                     "fixtures/native-history/weekly-update-ux-2026-11-17/weekly-prepared.ppm")
                              << QString("five");
        QTest::newRow("pilot") << QFINDTESTDATA("../docs/validation/weekly-pm-pilot-2026-11-18/pilot-start.ppm")
                               << QString("pilot");
    }
    void weeklyJourney() {
        QFETCH(QString, fixturePath);
        QFETCH(QString, tag);
        QTemporaryDir folder;
        const auto path = folder.filePath("review.ppm");
        QVERIFY(!fixturePath.isEmpty() && QFile::copy(fixturePath, path));
        ppm::JsonProjectStore store;
        ppm::Document doc(store);
        try {
            doc.open(path.toStdString());
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
        const auto original = *doc.project();
        const auto disk = bytes(path);
        const auto count = original.tasks.size();
        const int priorReported = count == 5 ? 2 : 12;
        const auto partsId = original.tasks[1].id, assemblyId = original.tasks[2].id;
        const auto installationId = original.tasks[3].id;
        ppm::MainWindow window(doc);
        window.resize(1562, 932);
        window.show();
        auto *table = taskTable(window);
        auto *model = dynamic_cast<ppm::TaskTableModel *>(table->model());
        auto *gantt = dynamic_cast<ppm::GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
        auto *weekly = window.findChild<QAction *>("weeklyUpdate");
        int step = 0;
        QTimer tick;
        tick.setInterval(35);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, [] { qFatal("weekly integrated UI workflow timeout"); });
        connect(&tick, &QTimer::timeout, &window, [&] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!modal)
                return;
            const auto name = modal->objectName();
            if (name == "weeklyUpdateDialog") {
                if (step == 0) {
                    step = 1;
                    QTimer::singleShot(0, modal,
                                       [modal] { modal->findChild<QPushButton *>("weeklySetStatus")->click(); });
                } else if (step == 2) {
                    QVERIFY(modal->findChild<QLabel *>("weeklySummary")
                                ->text()
                                .contains("이전 주 보고 " + QString::number(priorReported)));
                    auto *tasks = modal->findChild<QTableWidget *>("weeklyTasks");
                    const auto row = rowFor(tasks, partsId);
                    QVERIFY(row >= 0);
                    tasks->setCurrentCell(row, 0);
                    step = 3;
                    QTimer::singleShot(0, modal,
                                       [modal] { modal->findChild<QPushButton *>("weeklyEditProgress")->click(); });
                } else if (step == 6) {
                    QVERIFY(modal->findChild<QLabel *>("weeklySummary")->text().contains("현재 상태일 보고 2"));
                    QCOMPARE(doc.project()->progress.size(), original.progress.size() + 2);
                    capture(*modal, tag + "-01-submitted-cancelled");
                    step = 7;
                    modal->reject();
                }
            } else if (name == "statusDialog" && step == 1) {
                modal->findChild<QDateTimeEdit *>("statusAt")->setDate(QDate(2026, 11, 16));
                step = 2;
                modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } else if (name == "progressDialog") {
                auto *buttons = modal->findChild<QDialogButtonBox *>();
                const auto taskId = modal->property("progressTaskId").toString().toStdString();
                if (step == 3 && taskId == partsId) {
                    modal->findChild<QLineEdit *>("actualMinutes")->setText("2d");
                    modal->findChild<QLineEdit *>("remainingMinutes")->setText("2d");
                    modal->findChild<QLineEdit *>("progressReporter")->setText("통합 검증 PM");
                    modal->findChild<QLineEdit *>("progressReason")->setText("통합 자동 검사: 실제2d 잔여2d 확인");
                    step = 4;
                    buttons->button(QDialogButtonBox::Save)->click();
                } else if (step == 4 && taskId == assemblyId) {
                    QVERIFY(modal->findChild<QLineEdit *>("actualStart")->text().isEmpty());
                    modal->findChild<QLineEdit *>("actualMinutes")->setText("0d");
                    modal->findChild<QLineEdit *>("remainingMinutes")->setText("2d");
                    modal->findChild<QLineEdit *>("progressReporter")->setText("통합 검증 PM");
                    modal->findChild<QLineEdit *>("progressReason")->setText("통합 자동 검사: 미착수 확인");
                    step = 5;
                    buttons->button(QDialogButtonBox::Save)->click();
                } else if (step == 5 && taskId == installationId) {
                    modal->findChild<QLineEdit *>("remainingMinutes")->setText("3d");
                    step = 6;
                    buttons->button(QDialogButtonBox::Cancel)->click();
                }
            }
        });
        watchdog.start(20000);
        tick.start();
        weekly->trigger();
        tick.stop();
        watchdog.stop();
        QCOMPARE(step, 7);
        QVERIFY(!doc.calculation() && doc.dirty() && bytes(path) == disk);
        QCOMPARE(doc.project()->statusAt, std::optional<std::string>("2026-11-16 09:00"));
        const auto &reports = doc.project()->progress;
        QVERIFY(std::equal(original.progress.begin(), original.progress.end(), reports.begin()));
        QCOMPARE(reports[original.progress.size()].taskId, partsId);
        QCOMPARE(reports.back().taskId, assemblyId);
        QVERIFY(!reports.back().actualStart && reports.back().actualDurationMinutes == 0 &&
                reports.back().remainingMinutes == 960);
        QTimer::singleShot(40, &window, [] {
            auto *modal = QApplication::activeModalWidget();
            QVERIFY(modal);
            modal->findChild<QPushButton *>("weeklyRecalculate")->click();
        });
        weekly->trigger();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == ppm::CalculationStatus::Valid);
        QCOMPARE(*doc.calculation()->projectFinish, std::string("2026-11-20 18:00"));
        QVERIFY(doc.dirty() && bytes(path) == disk);
        window.findChild<QComboBox *>("taskColumnPreset")->setCurrentIndex(2);
        window.findChild<QLineEdit *>("taskSearch")->setText(QString::fromStdString(original.tasks[4].name));
        QTRY_COMPARE(model->rowCount(), 1);
        table->selectRow(0);
        auto *compare = actionNamed(window, "계획 대비 비교");
        QVERIFY(compare);
        step = 0;
        disconnect(&tick, nullptr, &window, nullptr);
        connect(&tick, &QTimer::timeout, &window, [&] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!modal)
                return;
            if (modal->objectName() == "comparisonDialog") {
                auto *outcomes = modal->findChild<QTableWidget *>("comparisonOutcomes");
                if (step == 0 && outcomes->rowCount() == 4) {
                    QCOMPARE(outcomes->item(0, 1)->text(), QString("2026-11-17 18:00"));
                    QCOMPARE(outcomes->item(2, 1)->text(), QString("2026-11-20 18:00"));
                    step = 1;
                    QTimer::singleShot(0, modal,
                                       [modal] { modal->findChild<QPushButton *>("comparisonResources")->click(); });
                } else if (step == 3 && outcomes->rowCount() == 4) {
                    QCOMPARE(outcomes->item(3, 1)->text(), QString("2026-11-24 18:00"));
                    step = 4;
                    modal->reject();
                }
            } else if (auto *resources = dynamic_cast<ppm::ResourceComparisonDialog *>(modal)) {
                auto *adopt = resources->findChild<QPushButton *>("adoptLeveling");
                auto *outcomes = resources->findChild<QTableWidget *>("levelingOutcomes");
                if (step == 1 && adopt->isEnabled() && outcomes->rowCount() == 4) {
                    QCOMPARE(outcomes->item(3, 1)->text(), QString("2026-11-24 18:00"));
                    QCOMPARE(outcomes->item(1, 5)->text(), count == 5 ? QString("16.00h") : QString("96.00h"));
                    QCOMPARE(outcomes->item(3, 5)->text(), QString("0.00h"));
                    resources->findChild<QLineEdit *>("levelingReason")
                        ->setText("통합 자동 검사: 중복 배정 해소안 채택");
                    step = 2;
                    adopt->click();
                } else if (step == 2 && doc.project()->remainingLeveling && adopt->isEnabled()) {
                    step = 3;
                    resources->reject();
                }
            }
        });
        watchdog.start(20000);
        tick.start();
        compare->trigger();
        tick.stop();
        watchdog.stop();
        QCOMPARE(step, 4);
        QCOMPARE(model->data(model->index(0, model->columnForKey("finish"))).toString(), QString("2026-11-24 18:00"));
        QCOMPARE(model->data(model->index(0, model->columnForKey("$finishVariance"))).toString(),
                 QString("+5d (+40h)"));
        QCOMPARE(gantt->displayedFinish(), QString("2026-11-24 18:00"));
        window.findChild<QAction *>("focusGanttFinish")->trigger();
        QVERIFY(gantt->finishVisible());
        capture(window, tag + "-02-adopted-main-before-save");
        QVERIFY(doc.dirty() && bytes(path) == disk);
        window.findChild<QToolButton *>("workflowSave")->click();
        QTRY_VERIFY(!doc.dirty());
        const auto saved = *doc.project();
        const auto savedDisk = bytes(path);
        QVERIFY(saved.tasks == original.tasks && saved.baselines == original.baselines &&
                saved.assignments == original.assignments);
        step = 0;
        disconnect(&tick, nullptr, &window, nullptr);
        const auto pdfPath = folder.filePath("meeting.pdf");
        connect(&tick, &QTimer::timeout, &window, [&] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!modal)
                return;
            if (modal->objectName() == "schedulePrintDialog") {
                auto *exportPdf = modal->findChild<QPushButton *>("exportSchedulePdf");
                if (step == 0 && exportPdf->isEnabled()) {
                    QVERIFY(modal->findChild<QLabel *>("schedulePrintMessage")
                                ->text()
                                .startsWith(QString::number(count) + " 작업"));
                    QCOMPARE(modal->findChild<QComboBox *>("printScope")->currentIndex(), 2);
                    step = 1;
                    modal->findChild<QComboBox *>("printPreset")->setCurrentIndex(1);
                } else if (step == 1 && exportPdf->isEnabled()) {
                    QCOMPARE(modal->findChild<QDateEdit *>("printFrom")->date(), QDate(2026, 11, 23));
                    QCOMPARE(modal->findChild<QDateEdit *>("printTo")->date(), QDate(2026, 11, 29));
                    capture(*modal, tag + "-03-meeting-next-week-preview");
                    step = 2;
                    QTimer::singleShot(0, exportPdf, &QPushButton::click);
                } else if (step == 3 &&
                           modal->findChild<QLabel *>("schedulePrintMessage")->text() == "출력을 완료했습니다.") {
                    step = 4;
                    modal->reject();
                }
            } else if (auto *chooser = qobject_cast<QFileDialog *>(modal); chooser && step == 2) {
                chooser->setDirectory(folder.path());
                auto *fileName = chooser->findChild<QLineEdit *>("fileNameEdit");
                QVERIFY(fileName);
                fileName->setText("meeting.pdf");
                QCOMPARE(chooser->selectedFiles(), QStringList{pdfPath});
                step = 3;
                QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
            }
        });
        watchdog.start(25000);
        tick.start();
        window.findChild<QAction *>("schedulePrintPreview")->trigger();
        tick.stop();
        watchdog.stop();
        QCOMPARE(step, 4);
        QVERIFY(bytes(pdfPath).startsWith("%PDF"));
        QVERIFY(*doc.project() == saved && bytes(path) == savedDisk && !doc.dirty());
        window.close();
        doc.open(path.toStdString());
        QVERIFY(!doc.calculation() && *doc.project() == saved && !doc.dirty());
        {
            ppm::MainWindow reopened(doc);
            reopened.resize(1562, 932);
            reopened.show();
            QVERIFY(reopened.findChild<QLabel *>("scheduleFreshnessState")->text().contains("계산 필요"));
            reopened.findChild<QToolButton *>("workflowCalculate")->click();
            QTRY_VERIFY(doc.calculation() && doc.calculation()->status == ppm::CalculationStatus::Valid);
            QCOMPARE(*doc.calculation()->projectFinish, std::string("2026-11-24 18:00"));
            capture(reopened, tag + "-04-reopened-calculated");
            QVERIFY(*doc.project() == saved && bytes(path) == savedDisk && !doc.dirty());
            reopened.close();
        }
        const auto audit = qEnvironmentVariable("PPM_JOURNEY_AUDIT_DIR");
        if (!audit.isEmpty()) {
            QVERIFY(QFile::copy(path, QDir(audit).filePath(tag + "-final.ppm")));
            QVERIFY(QFile::copy(pdfPath, QDir(audit).filePath(tag + "-meeting.pdf")));
        }
    }
};
QTEST_MAIN(WeeklyJourneyUiTests)
#include "WeeklyJourneyUiTests.moc"
