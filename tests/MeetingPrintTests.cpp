#include "persistence/JsonProjectStore.h"
#include "reporting/SchedulePrint.h"
#include "ui/SchedulePrintDialog.h"
#include "ui/EditorUi.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

namespace {
QByteArray bytes(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
jobs::SchedulePrintSource source(jobs::Document &doc) {
    jobs::SchedulePrintSource s;
    s.project = doc.snapshot();
    s.session = doc.sessionGeneration();
    s.unsaved = !doc.revisionSaved();
    s.scope = "검색 · 조립만 표시";
    s.generatedAt = "2026-11-18T00:00:00Z";
    s.printId = "meeting-print-test";
    s.baselineId = QString::fromStdString(doc.project()->baselines.front().id);
    s.columns = {{"number", "번호", 36},
                 {"name", "작업", 140},
                 {"finish", "상세 완료", 100},
                 {"predecessors", "관계", 100},
                 {"field:test", "보존할 상세 열", 100}};
    int number = 0;
    for (const auto &t : doc.project()->tasks)
        s.rows.push_back(
            {t.id,
             ++number,
             {QString::number(number), QString::fromStdString(t.name), "STALE", "관계 원문", "상세 원문"}});
    s.selected.insert(s.rows[2].taskId);
    return s;
}
void openFixture(jobs::Document &doc, const QTemporaryDir &folder) {
    const auto input = QFINDTESTDATA("fixtures/native-history/outcome-summary-2026-11-17/review.ppm");
    QVERIFY(!input.isEmpty());
    QVERIFY(QFile::copy(input, folder.filePath("review.ppm")));
    doc.open(folder.filePath("review.ppm").toStdString());
}
jobs::SchedulePrintOptions meeting() {
    jobs::SchedulePrintOptions o;
    o.font = QApplication::font();
    o.meetingSummary = true;
    o.tablePercent = 65;
    o.nameWidth = 110;
    return o;
}
} // namespace
class MeetingPrintTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const auto id =
            QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
        QVERIFY(id >= 0);
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
#endif
    }
    void compactFullAndNextWeekKeepFactsAndPageCount() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        openFixture(doc, folder);
        const auto before = *doc.project();
        auto s = source(doc);
        auto o = meeting();
        const auto layout = jobs::buildSchedulePrint(s, o);
        QCOMPARE(layout.taskCount, std::size_t(5));
        QVERIFY(layout.pages.size() >= 2 && layout.pages.size() <= 3);
        QCOMPARE(layout.pages[0].columns.size(), std::size_t(6));
        QVERIFY(!layout.pages[0].provenance && layout.pages[1].provenance);
        QVERIFY(layout.meetingContext.contains("전체 보고 3/5") && layout.meetingContext.contains("최초 계획"));
        QVERIFY(layout.meetingResult.contains("2026-11-24 18:00") && layout.meetingResult.contains("+5d (+40h)"));
        QCOMPARE(layout.rows[0].cells[5], QString("완료 보고"));
        QCOMPARE(layout.rows[3].cells[5], QString("미보고 · 미정"));
        QCOMPARE(layout.rows[4].cells[3], QString("2026-11-17 18:00"));
        QCOMPARE(layout.rows[4].cells[4], QString("+5d (+40h)"));
        QVERIFY(layout.rows[0].cells[2] != "STALE");
        const auto full = folder.filePath("full.pdf");
        QCOMPARE(jobs::exportSchedulePdf(layout, full), int(layout.pages.size()));
        QVERIFY(bytes(full).startsWith("%PDF"));
        o.from = QDate(2026, 11, 23);
        o.to = QDate(2026, 11, 29);
        const auto week = jobs::buildSchedulePrint(s, o);
        QCOMPARE(week.taskCount, std::size_t(5));
        QCOMPARE(week.pages.size(), layout.pages.size());
        QCOMPARE(week.pages[0].from, o.from);
        QCOMPARE(week.pages[0].to, o.to);
        QVERIFY(*doc.project() == before && !doc.dirty());
    }
    void missingBaselineInvalidScheduleAndHolidayVariance() {
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        doc.create("미정", "2026-11-02", {"기간 미정"});
        jobs::SchedulePrintSource s;
        s.project = doc.snapshot();
        s.generatedAt = "2026-11-18T00:00:00Z";
        s.printId = "missing";
        s.columns = {{"number", "번호", 36}, {"name", "작업", 110}};
        s.rows = {{doc.project()->tasks[0].id, 1, {"1", "기간 미정"}}};
        const auto missing = jobs::buildSchedulePrint(s, meeting());
        QCOMPARE(missing.rows[0].cells[2], QString("미정"));
        QCOMPARE(missing.rows[0].cells[3], QString("—"));
        QCOMPARE(missing.rows[0].cells[4], QString("—"));
        QCOMPARE(missing.rows[0].cells[5], QString("상태일 없음"));
        QVERIFY(missing.meetingResult.contains("완료 미정"));
        doc.setDuration(0, 480);
        const auto id = doc.captureBaseline("휴일 기준", "PM", "검사", "2026-11-18T00:00:00Z");
        auto p = *doc.project();
        p.holidays = {"2026-11-03"};
        p.tasks[0].durationMinutes = 960;
        // Use the same holiday calendar in the frozen baseline and current project.
        p.baselines[0].input.holidays = p.holidays;
        s.project = std::make_shared<const jobs::Project>(p);
        s.baselineId = QString::fromStdString(id);
        const auto holiday = jobs::buildSchedulePrint(s, meeting());
        QCOMPARE(holiday.rows[0].cells[2], QString("2026-11-04 18:00"));
        QCOMPARE(holiday.rows[0].cells[4], QString("+1d (+8h)"));
    }
    void presetsExplicitScopeAndDocumentStayIndependent() {
        QTemporaryDir folder;
        jobs::JsonProjectStore store;
        jobs::Document doc(store);
        openFixture(doc, folder);
        const auto before = *doc.project();
        const auto disk = bytes(folder.filePath("review.ppm"));
        auto s = source(doc);
        s.rows = {s.rows[2]};
        jobs::SchedulePrintDialog d(doc, s);
        jobs::prepareEditorDialog(d);
        d.show();
        auto *pdf = d.findChild<QPushButton *>("exportSchedulePdf");
        auto *preset = d.findChild<QComboBox *>("printPreset");
        auto *scope = d.findChild<QComboBox *>("printScope");
        QTRY_VERIFY(pdf->isEnabled());
        QCOMPARE(scope->currentIndex(), 2);
        QVERIFY(d.findChild<QLabel *>("schedulePrintMessage")->text().startsWith("5 작업"));
        QVERIFY(d.findChild<QSpinBox *>("printPage")->maximum() <= 3);
        QVERIFY(d.findChild<QLabel *>("printContext")->text().contains("화면 필터 제외"));
        scope->setCurrentIndex(1);
        QVERIFY(!pdf->isEnabled());
        d.findChild<QPushButton *>("prepareSchedulePrint")->click();
        QTRY_VERIFY(pdf->isEnabled());
        QVERIFY(d.findChild<QLabel *>("schedulePrintMessage")->text().startsWith("1 작업"));
        preset->setCurrentIndex(1);
        QTRY_VERIFY(pdf->isEnabled());
        QCOMPARE(d.findChild<QDateEdit *>("printFrom")->date(), QDate(2026, 11, 23));
        QCOMPARE(d.findChild<QDateEdit *>("printTo")->date(), QDate(2026, 11, 29));
        QVERIFY(d.findChild<QLabel *>("schedulePrintMessage")->text().startsWith("5 작업"));
        QVERIFY(d.findChild<QSpinBox *>("printPage")->maximum() <= 3);
        preset->setCurrentIndex(2);
        QTRY_VERIFY(pdf->isEnabled());
        QCOMPARE(scope->count(), 2);
        QVERIFY(d.findChild<QLabel *>("schedulePrintMessage")->text().startsWith("1 작업"));
        QVERIFY(*doc.project() == before && !doc.dirty() && bytes(folder.filePath("review.ppm")) == disk);
    }
};
QTEST_MAIN(MeetingPrintTests)
#include "MeetingPrintTests.moc"
