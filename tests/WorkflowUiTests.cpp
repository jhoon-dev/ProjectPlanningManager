#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/WeeklyUpdateDialog.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace {
QByteArray bytes(const std::string &path) {
    QFile file(QString::fromStdString(path));
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}
} // namespace
class WorkflowUiTests : public QObject {
    Q_OBJECT
private slots:
    void savingAndCalculationAreIndependent() {
        QTemporaryDir dir;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        {
            jobs::MainWindow empty(doc);
            empty.show();
            QVERIFY(!empty.findChild<QWidget *>("workflowStatus")->isVisible());
        }
        doc.create("주간 검증", "2026-11-09", {"작업"});
        doc.setDuration(0, 960);
        jobs::MainWindow window(doc);
        window.show();
        auto *panel = window.findChild<QWidget *>("workflowStatus");
        auto *file = window.findChild<QLabel *>("fileSaveState");
        auto *schedule = window.findChild<QLabel *>("scheduleFreshnessState");
        auto *hint = window.findChild<QLabel *>("workflowNextStep");
        auto *save = window.findChild<QToolButton *>("workflowSave");
        auto *calculate = window.findChild<QToolButton *>("workflowCalculate");
        QTRY_VERIFY(panel->isVisible());
        QVERIFY(file->text().contains("처음 저장 필요"));
        const auto path = dir.filePath("weekly.ppm").toStdString();
        doc.saveAs(path);
        QTRY_VERIFY(file->text().contains("저장됨"));
        QVERIFY(schedule->text().contains("계산 필요"));
        const auto disk = bytes(path);
        calculate->click();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Valid);
        QTRY_VERIFY(schedule->text().contains("계산 완료"));
        QVERIFY(!doc.dirty() && bytes(path) == disk);
        doc.captureBaseline("최초 계획", "PM", "승인", "2026-11-17T00:00:00Z");
        QTRY_VERIFY(file->text().contains("미저장"));
        QTRY_VERIFY(schedule->text().contains("재계산 필요"));
        const auto stale = doc.calculationGeneration();
        save->click();
        QTRY_VERIFY(file->text().contains("저장됨"));
        QCOMPARE(doc.calculationGeneration(), stale);
        QCOMPARE(doc.calculation()->status, jobs::CalculationStatus::Stale);
        doc.setStatusAt("2026-11-13 09:00");
        jobs::ProgressEntry e;
        e.taskId = doc.project()->tasks[0].id;
        e.actualStart = "2026-11-09 09:00";
        e.actualDurationMinutes = 480;
        e.remainingMinutes = 480;
        e.reporter = "PM";
        e.reason = "주간 확인";
        e.recordedAt = "2026-11-17T00:00:00Z";
        doc.recordProgress(e);
        const auto beforeCalculation = bytes(path);
        calculate->click();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Valid &&
                    doc.calculation()->isForecast);
        QTRY_VERIFY(schedule->text().contains("재예측 완료"));
        QVERIFY(file->text().contains("미저장") && hint->text().contains("파일을 저장"));
        QVERIFY(doc.dirty() && bytes(path) == beforeCalculation);
        save->click();
        QTRY_VERIFY(!doc.dirty());
        QVERIFY(schedule->text().contains("재예측 완료"));
        const auto saved = bytes(path);
        doc.open(path);
        QTRY_VERIFY(schedule->text().contains("계산 필요"));
        QVERIFY(file->text().contains("저장됨") && !doc.calculation());
        QVERIFY(bytes(path) == saved && doc.project()->baselines.size() == 1 && doc.project()->progress.size() == 1);
        doc.open(path, true);
        QTRY_VERIFY(file->text().contains("읽기 전용"));
        QCOMPARE(save->text(), QString("사본 저장…"));
        window.close();
    }
    void weeklyFinishIsExplicitAndDoesNotSave() {
        QTemporaryDir dir;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("연결", "2026-11-09", {"작업"});
        doc.setDuration(0, 480);
        doc.setStatusAt("2026-11-13 09:00");
        const auto path = dir.filePath("weekly.ppm").toStdString();
        doc.saveAs(path);
        const auto disk = bytes(path);
        jobs::MainWindow window(doc);
        window.show();
        auto *open = window.findChild<QAction *>("weeklyUpdate");
        QTimer::singleShot(50, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->reject();
        });
        open->trigger();
        QVERIFY(!doc.calculation());
        QTimer::singleShot(50, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QPushButton *>("weeklyRecalculate")->click();
        });
        open->trigger();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Valid);
        QVERIFY(!doc.dirty() && bytes(path) == disk);
        doc.setDuration(0, 960);
        QTimer::singleShot(50, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QPushButton *>("weeklyRecalculate")->click();
        });
        open->trigger();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Valid);
        QVERIFY(doc.dirty() && bytes(path) == disk);
        doc.save();
        window.close();
    }
    void incompleteAndCancelledAreNotCompletion() {
        QTemporaryDir dir;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("미완료", "2026-11-09", {"기간 없음"});
        const auto path = dir.filePath("incomplete.ppm").toStdString();
        doc.saveAs(path);
        jobs::MainWindow window(doc);
        window.show();
        auto *schedule = window.findChild<QLabel *>("scheduleFreshnessState");
        window.findChild<QToolButton *>("workflowCalculate")->click();
        QTRY_VERIFY(doc.calculation() && doc.calculation()->status == jobs::CalculationStatus::Incomplete);
        QTRY_VERIFY(schedule->text().contains("입력 불완전"));
        QVERIFY(window.findChild<QLabel *>("workflowNextStep")->text().contains("진단 보기"));
        auto result = *doc.calculation();
        result.status = jobs::CalculationStatus::Cancelled;
        doc.applyCalculation(result);
        doc.save();
        QTRY_VERIFY(schedule->text().contains("취소됨"));
        QVERIFY(!doc.calculation()->projectFinish);
        result.status = jobs::CalculationStatus::Conflict;
        doc.applyCalculation(result);
        doc.save();
        QTRY_VERIFY(schedule->text().contains("계산 충돌"));
        QVERIFY(window.findChild<QLabel *>("workflowNextStep")->text().contains("진단 보기"));
        window.close();
    }
};
QTEST_MAIN(WorkflowUiTests)
#include "WorkflowUiTests.moc"
