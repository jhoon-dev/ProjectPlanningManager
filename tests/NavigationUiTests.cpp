#include "TaskTableTest.h"
#include "persistence/JsonProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/TaskTableModel.h"
#include "ui/GanttView.h"
#include "ui/SchedulePrintDialog.h"
#include <QAction>
#include <QAbstractItemModelTester>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <set>

namespace {
QByteArray bytes(const QString &path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
void capture(QWidget &widget, const char *name) {
    const auto folder = qEnvironmentVariable("JOBS_NAV_AUDIT_DIR");
    if (folder.isEmpty())
        return;
    QVERIFY(QDir().mkpath(folder));
    QTest::qWait(60);
    QVERIFY(widget.grab().save(QDir(folder).filePath(QString::fromLatin1(name) + ".png")));
}
void fixture(jobs::Document &doc, const QTemporaryDir &folder) {
    const auto input = QFINDTESTDATA("../docs/validation/outcome-summary-2026-11-17/review.ppm");
    QVERIFY(!input.isEmpty());
    QVERIFY(QFile::copy(input, folder.filePath("review.ppm")));
    doc.open(folder.filePath("review.ppm").toStdString());
    doc.applyCalculation(jobs::calculate(*doc.project()));
}
} // namespace
class NavigationUiTests : public QObject {
    Q_OBJECT
private slots:
    void weeklyReadOnlyColumnsScopeFinishAndRestoration() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        fixture(doc, folder);
        const auto before = *doc.project();
        const auto disk = bytes(folder.filePath("review.ppm"));
        jobs::MainWindow window(doc);
        window.resize(1562, 932);
        window.show();
        auto *table = taskTable(window);
        auto *model = dynamic_cast<jobs::TaskTableModel *>(table->model());
        QAbstractItemModelTester modelContract(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto *preset = window.findChild<QComboBox *>("taskColumnPreset");
        auto *gantt = dynamic_cast<jobs::GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
        QTRY_COMPARE(model->rowCount(), 5);
        const auto originalColumns = table->horizontalHeader()->saveState();
        preset->setCurrentIndex(2);
        QTRY_COMPARE(model->columnCount(), 17);
        const auto base = model->columnForKey("$baselineFinish"), delta = model->columnForKey("$finishVariance"),
                   report = model->columnForKey("$reportState");
        QCOMPARE(model->data(model->index(4, base)).toString(), QString("2026-11-17 18:00"));
        QCOMPARE(model->data(model->index(4, delta)).toString(), QString("+5d (+40h)"));
        QCOMPARE(model->data(model->index(4, report)).toString(), QString("미보고 · 미정"));
        QCOMPARE(model->data(model->index(2, report)).toString(), QString("미착수 보고"));
        QVERIFY(!(model->flags(model->index(4, report)) & Qt::ItemIsEditable));
        QVERIFY(!model->setData(model->index(4, report), "완료", Qt::EditRole));
        capture(window, "01-weekly-columns");
        table->selectRow(4);
        window.findChild<QLineEdit *>("taskSearch")->setText("시운전");
        QTRY_COMPARE(model->rowCount(), 1);
        auto *scope = window.findChild<QLabel *>("navigationScope");
        QVERIFY(!scope->text().contains('\n'));
        auto *details = window.findChild<QLabel *>("navigationDetails");
        auto *toggle = window.findChild<QToolButton *>("navigationDetailsToggle");
        QVERIFY(!details->isVisible());
        toggle->click();
        QVERIFY(details->isVisible() && details->text() == scope->toolTip());
        toggle->click();
        QVERIFY(!details->isVisible());
        QTRY_VERIFY(scope->text().contains("표시 1/5개") && scope->text().contains("전체 5개") &&
                    scope->toolTip().contains("시운전"));
        QCOMPARE(gantt->displayedFinish(), QString("2026-11-24 18:00"));
        gantt->setDayWidth(64);
        gantt->horizontalScrollBar()->setValue(0);
        QTRY_VERIFY(!gantt->finishVisible());
        QVERIFY(scope->toolTip().contains("화면 밖"));
        capture(window, "02-filtered-finish-outside");
        window.findChild<QAction *>("focusGanttFinish")->trigger();
        QTRY_VERIFY(gantt->finishVisible());
        QVERIFY(scope->toolTip().contains("화면 안"));
        capture(window, "03-finish-visible");
        window.findChild<QAction *>("fitGanttSchedule")->trigger();
        QTRY_VERIFY(gantt->finishVisible());
        QCOMPARE(window.findChild<QLineEdit *>("taskSearch")->text(), QString("시운전"));
        window.findChild<QLineEdit *>("taskSearch")->setText("조립");
        QTRY_COMPARE(model->rowCount(), 1);
        table->selectRow(0);
        QTRY_COMPARE(gantt->displayedFinish(), QString("2026-11-19 18:00"));
        QTRY_VERIFY(scope->toolTip().contains("전체 완료 2026-11-24 18:00") &&
                    scope->toolTip().contains("간트 표시 완료 2026-11-19 18:00"));
        capture(window, "03b-different-displayed-and-project-finish");
        const auto captured =
            jobs::captureSchedulePrint(doc, *table, "표시 1개", QString::fromStdString(before.baselines[0].id));
        for (const auto &column : captured.columns)
            QVERIFY(!column.key.startsWith('$'));
        preset->setCurrentIndex(3);
        capture(window, "04-resource-columns");
        preset->setCurrentIndex(1);
        capture(window, "05-schedule-columns");
        preset->setCurrentIndex(0);
        QTRY_COMPARE(model->columnCount(), 14);
        QCOMPARE(table->horizontalHeader()->saveState(), originalColumns);
        capture(window, "06-original-columns-restored");
        QVERIFY(*doc.project() == before && !doc.dirty() && bytes(folder.filePath("review.ppm")) == disk);
    }
    void resourceGroupsKeepEveryActionAndExistingShortcuts() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("메뉴 확인", "2026-11-02", {"작업"});
        jobs::MainWindow window(doc);
        auto *menu = window.findChild<QMenu *>("resourceMenu");
        QVERIFY(menu);
        std::set<std::string> names;
        int actions = 0;
        const auto collect = [&](QMenu *group) {
            for (auto *action : group->actions())
                if (!action->isSeparator() && !action->menu()) {
                    ++actions;
                    names.insert(action->objectName().toStdString());
                }
        };
        collect(menu);
        for (const auto *id : {"resourcePeriodsMenu", "resourceMaterialsMenu", "resourceAdvancedMenu"}) {
            auto *group = window.findChild<QMenu *>(id);
            QVERIFY(group && menu->actions().contains(group->menuAction()));
            collect(group);
        }
        QCOMPARE(actions, 17);
        QCOMPARE(names.size(), std::size_t(17));
        if (!qEnvironmentVariable("JOBS_NAV_AUDIT_DIR").isEmpty()) {
            window.show();
            menu->popup(window.mapToGlobal(QPoint(20, 40)));
            capture(*menu, "07-resource-menu");
            menu->hide();
            auto *advanced = window.findChild<QMenu *>("resourceAdvancedMenu");
            advanced->popup(window.mapToGlobal(QPoint(20, 40)));
            capture(*advanced, "08-resource-advanced-menu");
            advanced->hide();
        }
        for (const auto *id :
             {"manageResources", "assignResources", "resourceComparison", "levelingDecisions", "remainingAvailability"})
            QVERIFY(menu->actions().contains(window.findChild<QAction *>(id)));
        QCOMPARE(window.findChild<QAction *>("focusGanttSelection")->shortcut(), QKeySequence("Ctrl+Shift+J"));
        QCOMPARE(window.findChild<QAction *>("fitGanttSchedule")->shortcut(), QKeySequence("Ctrl+Shift+F"));
    }
    void baselineMissingAndStaleResultsDoNotInventVariance() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        fixture(doc, folder);
        jobs::MainWindow window(doc);
        window.show();
        auto *model = dynamic_cast<jobs::TaskTableModel *>(taskTable(window)->model());
        window.findChild<QComboBox *>("taskColumnPreset")->setCurrentIndex(2);
        const auto delta = model->columnForKey("$finishVariance"), base = model->columnForKey("$baselineFinish");
        auto *choice = window.findChild<QComboBox *>("baselineChoice");
        choice->setCurrentIndex(0);
        QTRY_COMPARE(model->data(model->index(4, base)).toString(), QString("—"));
        QCOMPARE(model->data(model->index(4, delta)).toString(), QString("—"));
        choice->setCurrentIndex(1);
        QTRY_COMPARE(model->data(model->index(4, delta)).toString(), QString("+5d (+40h)"));
        doc.setDuration(4, 960);
        QTRY_COMPARE(model->data(model->index(4, delta)).toString(), QString("—"));
        QTRY_VERIFY(!window.findChild<QAction *>("focusGanttFinish")->isEnabled());
        // Direct document edits do not repaint the table; use the existing view refresh command.
        window.findChild<QPushButton *>("resetTaskView")->click();
        QCOMPARE(model->data(model->index(4, base)).toString(), QString("2026-11-17 18:00"));
        capture(window, "09-stale-no-variance");
        // Invoke the existing F9 button without depending on foreground focus in offscreen tests.
        window.findChild<QToolButton *>("workflowCalculate")->click();
        QTRY_VERIFY_WITH_TIMEOUT(model->data(model->index(4, delta)).toString() != "—", 15000);
        capture(window, "10-recalculated-variance");
    }
};
QTEST_MAIN(NavigationUiTests)
#include "NavigationUiTests.moc"
