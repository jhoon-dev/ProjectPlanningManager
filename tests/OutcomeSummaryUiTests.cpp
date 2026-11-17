#include "persistence/JsonProjectStore.h"
#include "ui/ComparisonDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/OutcomeSummary.h"
#include "ui/EditorUi.h"
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

namespace {
std::string fixture(jobs::Document &doc) {
    doc.create("완료일 비교", "2026-11-02", {"조립", "설치"});
    doc.setDuration(0, 480);
    doc.setDuration(1, 480);
    jobs::Resource resource;
    resource.name = "공유 기술자";
    resource.maxUnits = 100;
    const auto id = doc.putResource(resource);
    const auto tasks = doc.project()->tasks;
    for (const auto &task : tasks)
        doc.setAssignments(task.id, {{task.id, id, 100}});
    const auto baseline = doc.captureBaseline("승인 계획", "PM", "승인", "2026-11-17T00:00:00Z");
    doc.setStatusAt("2026-11-02 09:00");
    return baseline;
}
QString cell(QTableWidget *table, int row, int column) {
    return table->item(row, column)->text();
}
QByteArray bytes(const std::string &path) {
    QFile f(QString::fromStdString(path));
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
} // namespace
class OutcomeSummaryUiTests : public QObject {
    Q_OBJECT
private slots:
    void candidateAdoptionAndReleaseStayDistinct() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        const auto baseline = fixture(doc);
        const auto path = folder.filePath("review.ppm").toStdString();
        doc.saveAs(path);
        const auto disk = bytes(path);
        const auto original = *doc.project();
        jobs::ResourceComparisonDialog dialog(doc, nullptr, baseline);
        jobs::prepareEditorDialog(dialog);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("levelingOutcomes");
        auto *state = dialog.findChild<QLabel *>("levelingAdoptionState");
        auto *adopt = dialog.findChild<QPushButton *>("adoptLeveling");
        QTRY_VERIFY(adopt->isEnabled());
        QCOMPARE(table->rowCount(), 4);
        QCOMPARE(cell(table, 0, 1), QString("2026-11-02 18:00"));
        QCOMPARE(cell(table, 1, 1), cell(table, 2, 1));
        QCOMPARE(cell(table, 3, 1), QString("2026-11-03 18:00"));
        QCOMPARE(cell(table, 3, 2), QString("+1d (+8h)"));
        QCOMPARE(cell(table, 1, 5), QString("8.00h"));
        QCOMPARE(cell(table, 3, 5), QString("0.00h"));
        QVERIFY(state->text().contains("채택 없음") && state->text().contains("보고 범위 0/2"));
        table->setCurrentCell(1, 0);
        QCOMPARE(dialog.findChild<QTabWidget *>("resourceTabs")->currentWidget()->objectName(), QString("loadBefore"));
        table->setCurrentCell(3, 0);
        QCOMPARE(dialog.findChild<QTabWidget *>("resourceTabs")->currentWidget()->objectName(), QString("loadAfter"));
        table->setCurrentCell(2, 0);
        QCOMPARE(dialog.findChild<QTabWidget *>("resourceTabs")->currentWidget()->objectName(), QString("loadCurrent"));
        QVERIFY(*doc.project() == original && !doc.dirty() && bytes(path) == disk);
        adopt->click();
        QTRY_VERIFY(!dialog.findChild<QPushButton *>("cancelLeveling")->isEnabled());
        QVERIFY(!doc.project()->remainingLeveling && state->isVisible());
        QCOMPARE(state->text(), dialog.findChild<QLabel *>("levelingSummary")->text());
        dialog.findChild<QPushButton *>("runLeveling")->click();
        QTRY_VERIFY(adopt->isEnabled());
        dialog.findChild<QLineEdit *>("levelingReason")->setText("공유 기술자 중복 해소");
        adopt->click();
        QTRY_VERIFY(doc.project()->remainingLeveling && adopt->isEnabled() && table->rowCount() == 4);
        QCOMPARE(cell(table, 2, 1), cell(table, 3, 1));
        QCOMPARE(cell(table, 2, 2), QString("+1d (+8h)"));
        QVERIFY(state->text().contains("잔여 예측 자원 조정 채택"));
        QVERIFY(doc.project()->tasks == original.tasks && doc.project()->baselines == original.baselines);
        jobs::ComparisonDialog comparison(doc, baseline);
        jobs::prepareEditorDialog(comparison);
        comparison.show();
        auto *summary = comparison.findChild<QTableWidget *>("comparisonOutcomes");
        QTRY_COMPARE(summary->rowCount(), 4);
        QCOMPARE(cell(summary, 2, 1), QString("2026-11-02 18:00"));
        QCOMPARE(cell(summary, 3, 1), QString("2026-11-03 18:00"));
        QCOMPARE(cell(summary, 3, 3), QString("+1d (+8h)"));
        QVERIFY(comparison.findChild<QLabel *>("comparisonAdoption")->text().contains("채택"));
        auto *tasks = comparison.findChild<QTableWidget *>("comparisonTable");
        tasks->setCurrentCell(1, 0);
        QVERIFY(comparison.findChild<QPlainTextEdit *>("comparisonDetails")
                    ->toPlainText()
                    .contains(original.tasks[1].id.c_str()));
        comparison.close();
        dialog.findChild<QPushButton *>("clearLeveling")->click();
        QTRY_VERIFY(!doc.project()->remainingLeveling && adopt->isEnabled() && table->rowCount() == 4);
        QCOMPARE(cell(table, 2, 1), cell(table, 1, 1));
        QVERIFY(state->text().contains("채택 없음"));
        doc.setDuration(0, 960);
        QTRY_COMPARE(table->rowCount(), 0);
        QVERIFY(!adopt->isEnabled() && state->text().contains("오래"));
        dialog.close();
    }
    void baselineCalendarAndMissingValues() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("미정", "2026-11-02", {"기간 미정"});
        jobs::ComparisonDialog dialog(doc, "");
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("comparisonOutcomes");
        QTRY_COMPARE(table->rowCount(), 4);
        QCOMPARE(cell(table, 0, 1), QString("—"));
        QCOMPARE(cell(table, 3, 1), QString("—"));
        QCOMPARE(cell(table, 3, 2), QString("—"));
        QVERIFY(cell(table, 3, 4).contains("불완전"));
        QVERIFY(dialog.findChild<QLabel *>("comparisonAdoption")->text().contains("상태일 미설정"));
        {
            jobs::ResourceComparisonDialog resources(doc);
            resources.show();
            auto *outcomes = resources.findChild<QTableWidget *>("levelingOutcomes");
            QTRY_COMPARE(outcomes->rowCount(), 4);
            QCOMPARE(cell(outcomes, 1, 1), QString("—"));
            QVERIFY(cell(outcomes, 1, 5).contains("미정"));
            QVERIFY(cell(outcomes, 2, 5).contains("미정"));
        }
        auto input = static_cast<const jobs::PlanInput &>(*doc.project());
        input.holidays = {"2026-11-03"};
        QCOMPARE(jobs::outcomeDelta(&input, "2026-11-02 18:00", "2026-11-04 18:00"), QString("+1d (+8h)"));
        QCOMPARE(jobs::outcomeDelta(&input, "2026-11-04 18:00", "2026-11-02 18:00"), QString("−1d (−8h)"));
        doc.setDuration(0, 480);
        QTRY_COMPARE(table->rowCount(), 0);
        QVERIFY(dialog.findChild<QLabel *>("comparisonAdoption")->text().contains("오래"));
        dialog.close();
    }
    void differentStoredInputsAreNotAPriorityOnlyComparison() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        const auto baseline = fixture(doc);
        const auto scenario = doc.saveScenario("이전 입력", "task-order", 260, "2026-11-17T00:00:00Z",
                                               doc.project()->documentId, doc.project()->revision);
        doc.setDuration(0, 960);
        jobs::ResourceComparisonDialog dialog(doc, nullptr, baseline);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("levelingOutcomes");
        QTRY_COMPARE(table->rowCount(), 4);
        auto *other = dialog.findChild<QComboBox *>("levelingOther");
        other->setCurrentIndex(other->findData(QString::fromStdString(scenario)));
        QTRY_COMPARE(table->rowCount(), 5);
        QVERIFY(cell(table, 4, 4).contains("A/B 입력 다름"));
        QCOMPARE(cell(table, 4, 3), QString("—"));
        table->setCurrentCell(4, 0);
        QCOMPARE(dialog.findChild<QTabWidget *>("resourceTabs")->currentWidget()->objectName(), QString("loadOther"));
        dialog.findChild<QComboBox *>("levelingSource")->setCurrentIndex(1);
        QTRY_VERIFY(table->rowCount() == 5 &&
                    dialog.findChild<QLabel *>("levelingAdoptionState")->text().contains("직접 채택 불가"));
        QVERIFY(!dialog.findChild<QPushButton *>("adoptLeveling")->isEnabled());
        QCOMPARE(cell(table, 2, 3), QString("—"));
        dialog.close();
    }
};
QTEST_MAIN(OutcomeSummaryUiTests)
#include "OutcomeSummaryUiTests.moc"
