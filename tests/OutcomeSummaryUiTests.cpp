#include "persistence/JsonProjectStore.h"
#include "ui/ComparisonDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/OutcomeSummary.h"
#include "ui/EditorUi.h"
#include "ui/MainWindow.h"
#include "ui/GanttView.h"
#include "ui/TaskTableModel.h"
#include "ui/UiTranslations.h"
#include "TaskTableTest.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QMessageBox>
#include <QLocale>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

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
    void initTestCase() {
        jobs::installUiTranslations(*qApp);
    }
    void standardButtonsAreKoreanWithoutExternalTranslations() {
        const auto locale = QLocale();
        const auto restore = qScopeGuard([&] { QLocale::setDefault(locale); });
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        jobs::installUiTranslations(*qApp);
        QDialogButtonBox buttons(QDialogButtonBox::Close | QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                 QDialogButtonBox::Ok | QDialogButtonBox::Apply);
        QCOMPARE(buttons.button(QDialogButtonBox::Close)->text(), QString("닫기"));
        QCOMPARE(buttons.button(QDialogButtonBox::Save)->text(), QString("저장"));
        QCOMPARE(buttons.button(QDialogButtonBox::Cancel)->text(), QString("취소"));
        QCOMPARE(buttons.button(QDialogButtonBox::Ok)->text(), QString("확인"));
        QCOMPARE(buttons.button(QDialogButtonBox::Apply)->text(), QString("적용"));
        QSignalSpy rejected(&buttons, &QDialogButtonBox::rejected);
        buttons.button(QDialogButtonBox::Close)->click();
        QCOMPARE(rejected.count(), 1);
        QMessageBox message(QMessageBox::Question, "확인", "저장하시겠습니까?",
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Discard);
        QVERIFY(message.button(QMessageBox::Yes)->text().contains("예"));
        QVERIFY(message.button(QMessageBox::No)->text().contains("아니요"));
        QCOMPARE(message.button(QMessageBox::Discard)->text(), QString("저장 안 함"));
        QCOMPARE(QCoreApplication::translate("QCocoaTheme", "Don't Save"), QString("저장 안 함"));
        QCOMPARE(QCoreApplication::translate("OtherContext", "Close"), QString("Close"));
    }
    void inputMenusFileDialogAndMessageDetailsAreKorean() {
        const auto locale = QLocale();
        const auto restore = qScopeGuard([&] { QLocale::setDefault(locale); });
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        const auto menuText = [](const QMenu &menu) {
            QStringList labels;
            for (const auto *action : menu.actions())
                labels << action->text().remove('&').section('\t', 0, 0);
            return labels.join('\n');
        };
        QLineEdit input("설비 설치");
        input.selectAll();
        const std::unique_ptr<QMenu> inputMenu(input.createStandardContextMenu());
        QPlainTextEdit memo("주간 확인");
        memo.selectAll();
        const std::unique_ptr<QMenu> memoMenu(memo.createStandardContextMenu());
        for (const auto *menu : {inputMenu.get(), memoMenu.get()}) {
            const auto labels = menuText(*menu);
            for (const auto *label : {"실행 취소", "잘라내기", "복사", "붙여넣기", "모두 선택"})
                QVERIFY2(labels.contains(QString::fromUtf8(label)), qPrintable(labels));
        }
        QTemporaryDir folder;
        QFileDialog files(nullptr, "프로젝트 열기", folder.path(), "PPM (*.ppm)");
        files.setOption(QFileDialog::DontUseNativeDialog);
        files.show();
        QVERIFY(files.labelText(QFileDialog::FileName).contains("파일 이름"));
        QVERIFY(files.labelText(QFileDialog::FileType).contains("파일 형식"));
        QVERIFY(files.labelText(QFileDialog::Accept).contains("열기"));
        const auto capture = [](QWidget &widget, const char *name) {
            if (const auto path = qEnvironmentVariable("JOBS_OUTCOME_AUDIT_DIR"); !path.isEmpty()) {
                QVERIFY(QDir().mkpath(path));
                QTest::qWait(80);
                QVERIFY(widget.grab().save(QDir(path).filePath(QString::fromLatin1(name) + ".png")));
            }
        };
        capture(files, "korean-file-dialog");
        files.close();
        QMessageBox message(QMessageBox::Warning, "검토 필요", "일정 진단을 확인하세요.", QMessageBox::Close);
        message.setDetailedText("자원 과부하 진단 내용");
        message.show();
        QAbstractButton *details = nullptr;
        for (auto *button : message.findChildren<QAbstractButton *>())
            if (button->text() == "자세한 정보 표시...")
                details = button;
        QVERIFY(details);
        details->click();
        QCOMPARE(details->text(), QString("자세한 정보 숨기기..."));
        capture(message, "korean-message-details");
        message.close();
        input.resize(320, 40);
        input.show();
        inputMenu->popup(input.mapToGlobal(input.rect().bottomLeft()));
        capture(*inputMenu, "korean-input-menu");
        inputMenu->close();
    }
    void comparisonUsesFrozenAndCurrentTaskCalendars() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("달력별 비교", "2026-11-09", {"설치", "분 단위", "미정"});
        jobs::TaskCalendar calendar;
        calendar.name = "현장 달력";
        calendar.periods = {{540, 720}, {780, 1020}};
        const auto id = doc.putCalendar(calendar);
        doc.setTaskCalendar(0, id);
        doc.setDuration(0, 840);
        doc.setDuration(1, 1);
        doc.setDuration(2, 0);
        const auto baseline = doc.captureBaseline("7시간 승인", "PM", "승인", "2026-11-17T00:00:00Z");
        calendar = doc.project()->calendars[0];
        calendar.periods = {{540, 720}, {780, 960}};
        doc.putCalendar(calendar);
        doc.setDuration(0, 1080);
        doc.setStatusAt("2026-11-09 16:00");
        jobs::ProgressEntry entry;
        entry.taskId = doc.project()->tasks[0].id;
        entry.actualStart = "2026-11-09 09:00";
        entry.actualDurationMinutes = 360;
        entry.remainingMinutes = 720;
        entry.reporter = "PM";
        entry.reason = "현장 확인";
        entry.recordedAt = "2026-11-17T00:00:00Z";
        doc.recordProgress(entry);
        jobs::ComparisonDialog dialog(doc, baseline);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>("comparisonTable");
        QTRY_COMPARE(table->rowCount(), 3);
        QCOMPARE(cell(table, 0, 3), QString("+6h (+6h)"));
        QCOMPARE(cell(table, 0, 6), QString("1d"));
        QCOMPARE(cell(table, 0, 7), QString("2d"));
        QCOMPARE(cell(table, 1, 7), QString("1m"));
        QCOMPARE(cell(table, 1, 6), QString("—"));
        QVERIFY(table->item(0, 3)->toolTip().contains("1d=7h"));
        QVERIFY(table->item(0, 6)->toolTip().contains("1d=6h"));
        table->setCurrentCell(0, 0);
        QVERIFY(dialog.findChild<QPlainTextEdit *>("comparisonDetails")->toPlainText().contains("2d → 3d"));
        if (const auto folder = qEnvironmentVariable("JOBS_OUTCOME_AUDIT_DIR"); !folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QTest::qWait(80);
            QVERIFY(dialog.grab().save(QDir(folder).filePath("comparison-calendars.png")));
        }
        doc.setDuration(0, 360);
        dialog.findChild<QPushButton *>("refreshComparison")->click();
        QTRY_COMPARE(table->rowCount(), 3);
        QCOMPARE(cell(table, 0, 3), QString("−8h (−8h)"));
        dialog.findChild<QComboBox *>("comparisonBaseline")->setCurrentIndex(0);
        QTRY_VERIFY(table->rowCount() == 3 && cell(table, 0, 3) == "—");
        QCOMPARE(cell(table, 0, 6), QString("1d"));
    }
    void nestedAdoptionAndReleaseRefreshMainViews() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        fixture(doc);
        doc.applyCalculation(jobs::calculate(*doc.project()));
        const auto path = folder.filePath("nested.ppm").toStdString();
        doc.saveAs(path);
        const auto disk = bytes(path);
        const auto original = *doc.project();
        jobs::MainWindow window(doc);
        window.show();
        auto *table = taskTable(window);
        auto *model = dynamic_cast<jobs::TaskTableModel *>(table->model());
        auto *gantt = dynamic_cast<jobs::GanttView *>(window.findChild<QGraphicsView *>("ganttView"));
        window.findChild<QLineEdit *>("taskSearch")->setText("설치");
        QTRY_COMPARE(model->rowCount(), 1);
        table->selectRow(0);
        window.findChild<QComboBox *>("taskColumnPreset")->setCurrentIndex(2);
        QAction *open = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "계획 대비 비교")
                open = action;
        QVERIFY(open);
        for (const bool release : {false, true}) {
            int step = 0;
            QTimer tick;
            tick.setInterval(30);
            QTimer watchdog;
            watchdog.setSingleShot(true);
            connect(&watchdog, &QTimer::timeout, [] { qFatal("nested comparison workflow timeout"); });
            connect(&tick, &QTimer::timeout, &window, [&] {
                auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!modal)
                    return;
                if (modal->objectName() == "comparisonDialog") {
                    auto *outcomes = modal->findChild<QTableWidget *>("comparisonOutcomes");
                    if (step == 0 && outcomes->rowCount() == 4) {
                        step = 1;
                        QTimer::singleShot(
                            0, modal, [modal] { modal->findChild<QPushButton *>("comparisonResources")->click(); });
                    } else if (step == 3 && outcomes->rowCount() == 4) {
                        step = 4;
                        modal->reject();
                    }
                } else if (auto *resources = dynamic_cast<jobs::ResourceComparisonDialog *>(modal)) {
                    auto *adopt = resources->findChild<QPushButton *>("adoptLeveling");
                    if (step == 1 && adopt->isEnabled()) {
                        resources->findChild<QLineEdit *>("levelingReason")->setText("통합 경로 확인");
                        step = 2;
                        resources->findChild<QPushButton *>(release ? "clearLeveling" : "adoptLeveling")->click();
                    } else if (step == 2 && adopt->isEnabled() && bool(doc.project()->remainingLeveling) != release) {
                        step = 3;
                        resources->reject();
                    }
                }
            });
            watchdog.start(15000);
            tick.start();
            open->trigger();
            tick.stop();
            watchdog.stop();
            QCOMPARE(step, 4);
            const auto expected = release ? QString("2026-11-02 18:00") : QString("2026-11-03 18:00");
            QCOMPARE(QString::fromStdString(*doc.calculation()->projectFinish), expected);
            QCOMPARE(model->data(model->index(0, model->columnForKey("finish"))).toString(), expected);
            QCOMPARE(gantt->displayedFinish(), expected);
            const auto context = window.findChild<QLabel *>("navigationScope")->toolTip();
            QVERIFY(context.contains("표시 1/2개") && context.contains("선택 1개"));
            QVERIFY(context.contains("전체 완료 " + expected) && context.contains("간트 표시 완료 " + expected));
            QCOMPARE(window.findChild<QLineEdit *>("taskSearch")->text(), QString("설치"));
            QCOMPARE(window.findChild<QComboBox *>("taskColumnPreset")->currentIndex(), 2);
            QCOMPARE(model->data(model->index(0, model->columnForKey("$finishVariance"))).toString(),
                     release ? QString("0d (0h)") : QString("+1d (+8h)"));
        }
        QVERIFY(doc.project()->tasks == original.tasks && doc.project()->baselines == original.baselines);
        QVERIFY(doc.dirty() && bytes(path) == disk);
        doc.save();
        const auto saved = *doc.project();
        doc.open(path);
        QVERIFY(*doc.project() == saved && !doc.dirty() && !doc.calculation());
    }
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
