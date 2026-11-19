#include "XmlWorkProgressFixture.h"
#include "application/Document.h"
#include "calculation/Effort.h"
#include "calculation/Reports.h"
#include "domain/FinancialPeriods.h"
#include "domain/Classification.h"
#include "domain/LedgerIdentity.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlProgressDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error(message);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write evidence");
}
auto prepared(Document &d, std::vector<ProgressEntry> entries, std::shared_ptr<CalculationControl> c = {}) {
    return Document::prepareEdit(d.makeEditRequest(ProgressMergeEdit{std::move(entries)}), std::move(c));
}
const TaskSchedule &taskRow(const ScheduleResult &s, const std::string &id) {
    const auto i = std::find_if(s.tasks.begin(), s.tasks.end(), [&](const auto &r) { return r.taskId == id; });
    check(i != s.tasks.end(), "schedule task missing");
    return *i;
}
QJsonObject projection(const Project &p) {
    const auto forecast = calculate(p);
    check(forecast.status == CalculationStatus::Valid, "valid native work forecast");
    const auto &task = p.tasks.front();
    const auto &row = taskRow(forecast, task.id);
    TaskExecution execution(p, pendingTask(p, task), nullptr, true);
    const auto spans = execution.spans(row);
    WorkCalendar clock(p);
    QJsonArray values;
    std::int64_t total = 0;
    for (const auto &s : spans) {
        total += (s.finish - s.start) * s.units;
        values.append(QJsonObject{{"resourceId", QString::fromStdString(s.resourceId)},
                                  {"start", QString::fromStdString(clock.boundary(s.start, false))},
                                  {"finish", QString::fromStdString(clock.boundary(s.finish, true))},
                                  {"units", s.units}});
    }
    check(total == *progressAsOf(p, task.id)->remainingWorkUnitMinutes,
          "forecast consumes only explicit remaining work");
    return {{"remainingWork", double(total)},
            {"start", QString::fromStdString(row.start)},
            {"remainingStart", QString::fromStdString(*row.remainingStart)},
            {"finish", QString::fromStdString(row.finish)},
            {"duration", double(execution.duration(row))},
            {"spans", values}};
}
void contract() {
    const auto bytes = xmlWorkProgressFixture();
    const auto source = importProjectXml(bytes, "work-progress.xml");
    const auto original = xmlWorkProgressTarget();
    const auto options = xmlWorkProgressOptions();
    const auto mappings = xmlWorkProgressMappings();
    check(source.progressCandidates[0].remainingWorkUnitMinutes == 3501 &&
              source.progressCandidates[1].remainingWorkUnitMinutes == 0 &&
              source.progressCandidates[2].issue.empty() && !source.progressCandidates[2].remainingWorkIssue.empty(),
          "exact work, known zero, independent work validation");
    const auto rows = prepareXmlProgressMerge(original, source, options, mappings);
    check(rows.size() == 4 && rows[0].remainingWorkUnitMinutes == 3501 && !rows[0].planDurationMinutes &&
              rows[0].remainingMinutes == 123 && rows[1].remainingWorkUnitMinutes == 0 &&
              !rows[2].remainingWorkUnitMinutes && rows[3].remainingWorkUnitMinutes == 0,
          "work targets carry explicit work; duration targets keep original contract");
    check(original == xmlWorkProgressTarget() && source.project.progress.empty(),
          "prepare is immutable and default import remains plan only");
    auto tampered = source;
    tampered.progressCandidates[0].remainingWorkUnitMinutes = 123;
    rejects([&] { prepareXmlProgressMerge(original, tampered, options, mappings); }, "candidate work tamper accepted");
    for (const QByteArray &value : {QByteArray(), QByteArray("PT0H0M0S"), QByteArray("PT0H0M1S"), QByteArray("-PT1H"),
                                    QByteArray("PT83333334H"), QByteArray("NA")}) {
        auto bad = bytes;
        bad.replace("<RemainingWork>PT0H35M0.6S</RemainingWork>",
                    value.isEmpty() ? QByteArray() : "<RemainingWork>" + value + "</RemainingWork>");
        const auto invalid = importProjectXml(bad, "bad-work.xml");
        rejects([&] { prepareXmlProgressMerge(original, invalid, options, mappings); },
                "missing/zero/inexact/negative/overflow work accepted");
    }
    auto completedNoWork = bytes;
    completedNoWork.replace(
        "<ActualFinish>2026-11-13T09:40:00</ActualFinish><ActualDuration>PT0H40M0S</"
        "ActualDuration><RemainingDuration>PT0H0M0S</RemainingDuration><RemainingWork>PT0H0M0S</RemainingWork>",
        "<ActualFinish>2026-11-13T09:40:00</ActualFinish><ActualDuration>PT0H40M0S</"
        "ActualDuration><RemainingDuration>PT0H0M0S</RemainingDuration>");
    check(!prepareXmlProgressMerge(original, importProjectXml(completedNoWork, "complete.xml"), options, {{"2", "NB"}})
               .front()
               .remainingWorkUnitMinutes,
          "completed task does not invent remaining work zero");
    auto contradiction = completedNoWork;
    contradiction.replace("<ActualFinish>2026-11-13T09:40:00</ActualFinish>",
                          "<ActualFinish>2026-11-13T09:40:00</ActualFinish><RemainingWork>PT0H1M0S</RemainingWork>");
    rejects(
        [&] {
            prepareXmlProgressMerge(original, importProjectXml(contradiction, "conflict.xml"), options, {{"2", "NB"}});
        },
        "completed work conflict accepted");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document d(store);
    d.open(path);
    auto proposal = prepared(d, rows);
    check(d.applyPreparedEdit(*proposal) == EditState::Applied, "work batch applies atomically");
    const auto merged = *d.project();
    const auto *a = progressAsOf(merged, "NA"), *b = progressAsOf(merged, "NB");
    check(merged.revision == 6 && merged.progress.size() == 6 && merged.progress[0] == original.progress[0] &&
              merged.progress[1] == original.progress[1] && a->previousEntryId == original.progress[1].id,
          "one revision with earlier and future reports preserved");
    check(static_cast<const PlanInput &>(merged) == static_cast<const PlanInput &>(original) &&
              merged.periodActuals == original.periodActuals && merged.costEntries == original.costEntries &&
              merged.remainingWork == original.remainingWork && store.load(path).project == original,
          "plans, actual ledgers, dated targets and original file unchanged");
    check(a->planDurationMinutes == 94 && b->planDurationMinutes == 87 && a->remainingMinutes == 123 &&
              a->remainingWorkUnitMinutes == 3501 && progressAsOf(merged, "NC")->planDurationMinutes == 45 &&
              progressAsOf(merged, "NZ")->planDurationMinutes == 0,
          "work plan snapshots use calculated duration, not nominal or source duration");
    const auto mainProjection = projection(merged);
    check(mainProjection["duration"] == 37 && mainProjection["finish"] == "2026-11-17 09:37" &&
              mainProjection["duration"].toInteger() != a->remainingMinutes,
          "report duration preserved while forecast derives from work/calendars/curve");
    check(taskRow(calculate(merged), "NB").completedActual &&
              executionSpans(merged, pendingTask(merged, merged.tasks[1]), taskRow(calculate(merged), "NB")).empty(),
          "completed work task has no future demand");
    check(prepareXmlProgressMerge(merged, source, options, mappings).empty(), "same source idempotent");
    ProgressEntry correction = *a;
    correction.remainingWorkUnitMinutes = 3601;
    correction.reason = "후속 수동 정정";
    d.recordProgress(correction);
    const auto corrected = *d.project();
    check(prepareXmlProgressMerge(corrected, source, options, mappings).empty() &&
              progressAsOf(corrected, "NA")->remainingWorkUnitMinutes == 3601,
          "retry preserves manual correction");
    auto changed = bytes;
    changed.replace("PT0H35M0.6S", "PT0H36M0.6S");
    const auto update = importProjectXml(changed, "updated.xml");
    auto updated = prepareXmlProgressMerge(corrected, update, options, mappings);
    auto failed = updated;
    failed.back().remainingWorkUnitMinutes.reset();
    auto invalid = prepared(d, failed);
    check(d.applyPreparedEdit(*invalid) == EditState::Failed && *d.project() == corrected,
          "direct batch guard rejects final work row without partial commit");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto cancelled = prepared(d, updated, control);
    check(d.applyPreparedEdit(*cancelled) == EditState::Cancelled && *d.project() == corrected,
          "cancelled worker preserves all state");
    auto stale = prepared(d, updated);
    d.renameTask(0, "보존할 편집");
    const auto edited = *d.project();
    check(d.applyPreparedEdit(*stale) == EditState::Stale && *d.project() == edited,
          "stale proposal preserves intervening edits");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot(options.recordedAt));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "work progress recovery");
    d.save();
    check(store.load(path).project == *d.project() && nativeSchemaFor(merged) == 24,
          "existing work-curve schema24 roundtrip");
    auto plain = merged;
    plain.assignments.front().periods.clear();
    check(nativeSchemaFor(plain) == 11, "work progress alone adds no schema requirement");
    {
        Document readonly(store);
        readonly.open(path, true);
        rejects([&] { readonly.makeEditRequest(ProgressMergeEdit{updated}); }, "read-only work merge accepted");
    }
    const auto migrated = migrateLedgerIdentities(merged, {{"task", "NA", "renamed-A"}});
    auto remapped = mappings;
    remapped.front().taskId = "renamed-A";
    check(prepareXmlProgressMerge(migrated, source, options, remapped).empty() &&
              progressAsOf(migrated, "renamed-A")->remainingWorkUnitMinutes == 3501,
          "ID migration preserves work and source retry protection");
    const auto lockPath = tmp.filePath("locked.ppm").toStdString();
    store.save(lockPath, original, {});
    Document locked(store);
    locked.open(lockPath);
    locked.captureBaseline("승인", "PM", "기간 잠금", options.recordedAt);
    FinancialPeriodSubmission close;
    close.period.name = "마감 기간";
    close.period.baselineId = locked.project()->baselines.front().id;
    close.period.fromDate = "2026-11-13";
    close.period.toDate = "2026-11-16";
    close.reporter = "PM";
    close.reason = "마감";
    close.recordedAt = options.recordedAt;
    close.confirmed = true;
    const auto periodId = locked.recordFinancialPeriod(close);
    close.period = financialPeriod(*locked.project(), periodId).period;
    close.action = "close";
    locked.setStatusAt("2026-11-17 00:00");
    locked.recordFinancialPeriod(close);
    locked.setStatusAt(options.statusAt);
    const auto frozen = *locked.project();
    auto frozenEdit = prepared(locked, prepareXmlProgressMerge(frozen, source, options, mappings));
    check(locked.applyPreparedEdit(*frozenEdit) == EditState::Failed && *locked.project() == frozen,
          "closed period blocks entire work progress merge");
    const auto qa = QDir::currentPath() + "/xml-work-progress-qa";
    QDir().mkpath(qa);
    save(qa + "/original.xml", bytes);
    save(qa + "/before.json", QJsonDocument(json::encodeProject(original)).toJson());
    save(qa + "/merged.json", QJsonDocument(json::encodeProject(merged)).toJson());
    save(qa + "/corrected.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    save(qa + "/forecast.json", QJsonDocument(mainProjection).toJson());
    QJsonArray cases;
    Document variations(store);
    const auto variantPath = tmp.filePath("cases.ppm").toStdString();
    store.save(variantPath, merged, {});
    variations.open(variantPath);
    for (const std::int64_t quantity : {1LL, 99LL, 100LL, 3501LL, 4999LL, 5000LL, 5001LL, 9001LL}) {
        auto e = *progressAsOf(*variations.project(), "NA");
        e.reason = "공수 수량 독립 검증";
        e.remainingWorkUnitMinutes = quantity;
        variations.recordProgress(e);
        cases.append(projection(*variations.project()));
    }
    auto dated = merged;
    dated.remainingWork = {{"NA", "NR1", *dated.statusAt, {{"2026-11-17", 1000}}}};
    validate(dated);
    auto datedRows = prepareXmlProgressMerge(dated, update, options, {{"1", "NA"}});
    const auto datedPath = tmp.filePath("dated.ppm").toStdString();
    store.save(datedPath, dated, {});
    Document datedDoc(store);
    datedDoc.open(datedPath);
    auto datedEdit = prepared(datedDoc, datedRows);
    check(datedDoc.applyPreparedEdit(*datedEdit) == EditState::Applied &&
              datedDoc.project()->remainingWork == dated.remainingWork,
          "dated remaining targets retained");
    save(qa + "/dated.json", QJsonDocument(json::encodeProject(*datedDoc.project())).toJson());
    save(qa + "/dated-forecast.json", QJsonDocument(projection(*datedDoc.project())).toJson());
    save(qa + "/cases.json", QJsonDocument(cases).toJson());
    QFile::remove(qa + "/source.ppm");
    store.save((qa + "/source.ppm").toStdString(), merged, {});
    ReportOptions ro;
    ro.reportId = "xml-work-progress";
    ro.generatedAt = options.recordedAt;
    const auto report = buildReport(merged, ro);
    const auto audit = std::find_if(report.tables.begin(), report.tables.end(),
                                    [](const auto &t) { return t.id == "xmlProgressAudit"; });
    check(audit != report.tables.end() && audit->columns.size() == 14 && audit->rows.size() == 6,
          "work progress audit includes plan snapshot and exact remaining work");
    for (const auto *name : {"audit.xlsx", "audit.pdf"})
        QFile::remove(qa + '/' + name);
    exportXlsx(report, qa + "/audit.xlsx");
    exportPdf(report, qa + "/audit.pdf");
    std::cout << "PASS work-driven XML duration merge: explicit exact remaining work, zero/unknown/completion, plan "
                 "snapshots, heterogeneous calendars/curve/final minute, preserved plans/actuals/future/corrections, "
                 "idempotency/cancel/stale/atomic/schema24/recovery/reports\n";
}
void ui() {
    const auto source = importProjectXml(xmlWorkProgressFixture(), "work-progress.xml");
    const auto original = xmlWorkProgressTarget();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document d(store);
    d.open(path);
    MainWindow window(d);
    window.show();
    bool checked = false;
    QTimer timer;
    timer.setInterval(20);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlProgressMergeDialog")
            return;
        timer.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlProgressMergeRows");
        check(table && table->rowCount() == 5, "work review rows");
        for (int i = 0; i < 4; ++i) {
            check(table->item(i, 0)->checkState() == Qt::Unchecked, "work not selected by default");
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(i, 2));
            check(combo->currentData().toString().isEmpty(), "no guessed work mapping");
            combo->setCurrentIndex(i + 1);
            table->item(i, 0)->setCheckState(Qt::Checked);
        }
        check(table->item(0, 4)->text().contains("3501u") && table->item(0, 3)->text().contains("공수 기준") &&
                  table->item(0, 5)->text().contains("잔여 공수로 예측") &&
                  table->item(2, 5)->text().contains("선택 시"),
              "exact incoming work and mapped task basis visible");
        dialog->findChild<QLineEdit *>("xmlMergeReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlMergeReason")->setText("공수 기준 기간 대사");
        dialog->resize(800, 680);
        QDir().mkpath(QDir::currentPath() + "/xml-work-progress-qa");
        dialog->grab().save(QDir::currentPath() + "/xml-work-progress-qa/work-merge-small.png");
        table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());
        dialog->grab().save(QDir::currentPath() + "/xml-work-progress-qa/work-merge-values-small.png");
        checked = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    check(reviewXmlProgressMerge(&window, d, source) && checked, "work UI applies");
    check(progressAsOf(*d.project(), "NA")->remainingWorkUnitMinutes == 3501 && store.load(path).project == original,
          "UI records work and preserves original file");
    const auto merged = *d.project();
    QTimer::singleShot(30, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    check(!reviewXmlProgressMerge(&window, d, source) && *d.project() == merged, "work UI cancel");
    auto bad = source.xml;
    bad.replace("<RemainingWork>PT0H35M0.6S</RemainingWork>", "");
    const auto missing = importProjectXml(bad, "missing.xml");
    QTimer rejectTimer;
    rejectTimer.setInterval(20);
    bool rejected = false;
    QObject::connect(&rejectTimer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlProgressMergeDialog")
            return;
        rejectTimer.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlProgressMergeRows");
        qobject_cast<QComboBox *>(table->cellWidget(0, 2))->setCurrentIndex(1);
        table->item(0, 0)->setCheckState(Qt::Checked);
        check(table->item(0, 5)->text().contains("명시 잔여 공수"), "missing work reason visible");
        dialog->findChild<QLineEdit *>("xmlMergeReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlMergeReason")->setText("미보고 차단");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        check(!dialog->findChild<QLabel *>("xmlMergeError")->text().isEmpty(), "worker missing work error visible");
        rejected = true;
        dialog->reject();
    });
    rejectTimer.start();
    check(!reviewXmlProgressMerge(&window, d, missing) && rejected && *d.project() == merged,
          "missing work UI cannot partially apply");
    std::cout << "PASS work-driven XML UI: explicit mapping/selection, duration vs work, exact quantities, missing "
                 "work guard, native apply/cancel, 800px review\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Korean font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (const auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &s : a->diagnostic().arguments)
                std::cerr << ' ' << s;
        return 1;
    }
}
