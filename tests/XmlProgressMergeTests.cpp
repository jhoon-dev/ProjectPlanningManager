#include "XmlProgressFixture.h"
#include "application/Document.h"
#include "domain/FinancialPeriods.h"
#include "domain/LedgerIdentity.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "calculation/Reports.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlProgressDialog.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
template <class F> void rejects(F function, const char *message) {
    try {
        function();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error(message);
}
Project target(const ProjectXmlExchange &source) {
    auto p = source.project;
    p.documentId = "native-merge-target";
    p.revision = 5;
    p.statusAt = "2026-11-12 12:00";
    for (auto &t : p.tasks)
        t.id += "-native";
    for (auto &a : p.assignments)
        a.taskId += "-native";
    ProgressEntry e;
    e.id = "existing-native-report";
    e.taskId = p.tasks[1].id;
    e.statusAt = "2026-11-12 11:45";
    e.recordedAt = "2026-11-12T03:00:00Z";
    e.reporter = "현장";
    e.reason = "보존할 기존 보고";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = 4;
    e.planDurationMinutes = 240;
    e.actualStart = "2026-11-12 11:00";
    e.actualDurationMinutes = 30;
    e.remainingMinutes = 210;
    p.progress.push_back(e);
    validate(p);
    return p;
}
std::vector<XmlProgressMapping> mappings(const ProjectXmlExchange &source, const Project &p) {
    std::vector<XmlProgressMapping> result;
    for (std::size_t i = 0; i < 4; ++i)
        result.push_back({source.progressCandidates[i].xmlUid, p.tasks[i].id});
    return result;
}
std::shared_ptr<Document::PreparedEdit> prepare(Document &doc, std::vector<ProgressEntry> entries,
                                                std::shared_ptr<CalculationControl> control = {}) {
    return Document::prepareEdit(doc.makeEditRequest(ProgressMergeEdit{std::move(entries)}), std::move(control));
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
void contract() {
    const auto source = importProjectXml(xmlProgressFixture(), "merge-source.xml");
    const auto original = target(source);
    const auto options = xmlProgressOptions(source);
    const auto mapping = mappings(source, original);
    const auto rows = prepareXmlProgressMerge(original, source, options, mapping);
    check(prepareXmlProgressMerge(original, source, options, {mapping.front()}).size() == 1,
          "unselected conflicts keep current report");
    check(rows.size() == 4 && rows[1].taskId == original.tasks[1].id &&
              rows[1].previousEntryId == original.progress[0].id && rows[1].remainingMinutes == 180,
          "explicit UID mapping and conflict correction");
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto future = original;
    auto futureEntry = future.progress.front();
    futureEntry.id = "future-native-report";
    futureEntry.previousEntryId = future.progress.front().id;
    futureEntry.statusAt = "2026-11-13 12:00";
    futureEntry.recordedRevision = future.revision;
    future.progress.push_back(futureEntry);
    const auto futurePath = tmp.filePath("future.ppm").toStdString();
    store.save(futurePath, future, {});
    {
        Document futureDoc(store);
        futureDoc.open(futurePath);
        auto proposal = prepare(futureDoc, prepareXmlProgressMerge(future, source, options, mapping));
        check(futureDoc.applyPreparedEdit(*proposal) == EditState::Applied &&
                  futureDoc.project()->progress[1] == futureEntry &&
                  futureDoc.project()->progress[3].previousEntryId == futureEntry.id &&
                  progressAsOf(*futureDoc.project(), futureEntry.taskId)->actualDurationMinutes == 60,
              "future report preserved in full chain and current as-of correction selected");
    }
    auto workDriven = original;
    workDriven.tasks.front().taskType = TaskType::FixedWork;
    workDriven.tasks.front().workUnitMinutes = 12000;
    workDriven.tasks.front().workDriven = true;
    validate(workDriven);
    check(!prepareXmlProgressMerge(workDriven, source, options, {mapping.front()}).front().remainingWorkUnitMinutes,
          "completed work task preserves missing remaining work without inventing zero");
    workDriven.tasks[1].taskType = TaskType::FixedWork;
    workDriven.tasks[1].workUnitMinutes = 12000;
    workDriven.tasks[1].workDriven = true;
    rejects([&] { prepareXmlProgressMerge(workDriven, source, options, mapping); },
            "ongoing work task without explicit remaining work accepted");
    auto noStatus = original;
    noStatus.statusAt.reset();
    rejects([&] { prepareXmlProgressMerge(noStatus, source, options, mapping); }, "native status required");
    const auto path = tmp.filePath("current.ppm").toStdString();
    store.save(path, original, {});
    Document doc(store);
    doc.open(path);
    auto prepared = prepare(doc, rows);
    check(*doc.project() == original && prepared->state() == EditState::Ready, "worker preserves live document");
    check(doc.applyPreparedEdit(*prepared) == EditState::Applied, "atomic batch apply");
    const auto merged = *doc.project();
    check(merged.progress.size() == 5 && merged.progress.front() == original.progress.front() &&
              merged.revision == original.revision + 1 && !doc.canUndo() && doc.dirty() &&
              static_cast<const PlanInput &>(merged) == static_cast<const PlanInput &>(original) &&
              merged.statusAt == original.statusAt && merged.costEntries.empty() && merged.periodActuals.empty(),
          "one revision, immutable history, unchanged plan/status and no inferred work/cost");
    for (std::size_t i = 1; i < merged.progress.size(); ++i)
        check(merged.progress[i].recordedRevision == merged.revision &&
                  merged.progress[i].sourceDocumentId == original.documentId,
              "batch metadata refers to target revision/document");
    check(store.load(path).project == original, "apply does not overwrite source file");
    check(doc.applyPreparedEdit(*prepared) == EditState::Stale, "single-use proposal");
    auto duplicate = prepareXmlProgressMerge(merged, source, options, mapping);
    check(duplicate.empty(), "same source UID/status is idempotent");
    auto noChange = prepare(doc, duplicate);
    check(doc.applyPreparedEdit(*noChange) == EditState::NoChange && *doc.project() == merged,
          "retry does not add reports or revision");
    ProgressEntry correction = merged.progress[2];
    correction.reason = "후속 현장 정정";
    correction.actualDurationMinutes = 50;
    doc.recordProgress(correction);
    const auto corrected = *doc.project();
    check(prepareXmlProgressMerge(corrected, source, options, mapping).empty(),
          "retry cannot override a later native correction");
    auto remapped = mapping;
    remapped[0].taskId = original.tasks[8].id;
    rejects([&] { prepareXmlProgressMerge(corrected, source, options, remapped); }, "cannot redirect adopted UID");
    auto migrated = migrateLedgerIdentities(corrected, {{"task", mapping[0].taskId, "renamed-native-id"}});
    remapped = mapping;
    remapped[0].taskId = "renamed-native-id";
    check(prepareXmlProgressMerge(migrated, source, options, remapped).empty(), "idempotency survives ID migration");
    for (int kind = 0; kind < 7; ++kind) {
        auto bad = mapping;
        auto o = options;
        auto xml = source;
        if (kind == 0)
            bad[1].taskId = bad[0].taskId;
        if (kind == 1)
            bad[1].xmlUid = bad[0].xmlUid;
        if (kind == 2)
            bad[0].taskId = "absent";
        if (kind == 3)
            bad[0].xmlUid = source.progressCandidates[4].xmlUid;
        if (kind == 4)
            o.statusAt = "2026-11-12 13:00";
        if (kind == 5)
            xml.xml += " ";
        if (kind == 6)
            o.confirmed = false;
        rejects([&] { prepareXmlProgressMerge(original, xml, o, bad); }, "invalid mapping/source/status accepted");
    }
    auto changedXml = source.xml;
    changedXml.replace("<ActualDuration>PT1H0M0S</ActualDuration>", "<ActualDuration>PT0H50M0S</ActualDuration>");
    const auto update = importProjectXml(changedXml, "updated.xml");
    auto updateRows = prepareXmlProgressMerge(corrected, update, options, mapping);
    auto partial = updateRows;
    partial.back().remainingMinutes = -1;
    auto failed = prepare(doc, partial);
    check(doc.applyPreparedEdit(*failed) == EditState::Failed && *doc.project() == corrected,
          "invalid final row cannot partially commit earlier rows");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto cancelled = prepare(doc, rows, control);
    check(doc.applyPreparedEdit(*cancelled) == EditState::Cancelled && *doc.project() == corrected,
          "cancelled worker preserves existing ledger");
    auto stale = prepare(doc, updateRows);
    doc.renameTask(0, "보존할 사용자 수정");
    const auto edited = *doc.project();
    check(doc.applyPreparedEdit(*stale) == EditState::Stale && *doc.project() == edited,
          "stale proposal preserves intervening edit");
    auto fresh = prepare(doc, prepareXmlProgressMerge(edited, update, options, mapping));
    check(doc.applyPreparedEdit(*fresh) == EditState::Applied &&
              doc.project()->progress[7].previousEntryId == corrected.progress.back().id,
          "new source appends to latest correction chain");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T03:05:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery checkpoint");
    doc.save();
    {
        Document readOnly(store);
        readOnly.open(path, true);
        rejects([&] { readOnly.makeEditRequest(ProgressMergeEdit{rows}); }, "read-only merge accepted");
    }
    check(store.load(path).project == *doc.project() &&
              json::decodeProject(json::encodeProject(*doc.project()), nativeSchemaFor(*doc.project())) ==
                  *doc.project(),
          "native serialization and file roundtrip");
    ReportOptions reportOptions;
    reportOptions.reportId = "xml-merge";
    reportOptions.generatedAt = options.recordedAt;
    const auto report = buildReport(*doc.project(), reportOptions);
    const auto audit = std::find_if(report.tables.begin(), report.tables.end(),
                                    [](const auto &t) { return t.id == "xmlProgressAudit"; });
    check(audit != report.tables.end() && audit->rows.size() == doc.project()->progress.size(),
          "audit includes original native, imported and correction rows");
    // Re-open an independent target to exercise financial locking without corrupting the first ledger.
    Document locked(store);
    const auto lockedPath = tmp.filePath("locked.ppm").toStdString();
    store.save(lockedPath, original, {});
    locked.open(lockedPath);
    locked.captureBaseline("승인", "PM", "기간 잠금", options.recordedAt);
    FinancialPeriodSubmission period;
    period.period.name = "관리기간";
    period.period.baselineId = locked.project()->baselines.front().id;
    period.period.fromDate = period.period.toDate = "2026-11-12";
    period.reporter = "PM";
    period.reason = "마감";
    period.recordedAt = options.recordedAt;
    period.confirmed = true;
    const auto id = locked.recordFinancialPeriod(period);
    period.period = financialPeriod(*locked.project(), id).period;
    period.action = "close";
    locked.setStatusAt("2026-11-13 00:00");
    locked.recordFinancialPeriod(period);
    locked.setStatusAt(options.statusAt);
    const auto frozen = *locked.project();
    auto lockedRows = prepareXmlProgressMerge(frozen, source, options, mapping);
    auto rejected = prepare(locked, lockedRows);
    check(locked.applyPreparedEdit(*rejected) == EditState::Failed && *locked.project() == frozen,
          "closed financial period rejects whole merge");
    const auto out = QDir::currentPath() + "/xml-progress-merge-qa";
    QDir().mkpath(out);
    write(out + "/original.xml", source.xml);
    write(out + "/before.json", QJsonDocument(json::encodeProject(original)).toJson());
    write(out + "/merged.json", QJsonDocument(json::encodeProject(merged)).toJson());
    write(out + "/corrected.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    for (const auto *name : {"source.ppm", "audit.xlsx", "audit.pdf"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), *doc.project(), {});
    exportXlsx(report, out + "/audit.xlsx");
    exportPdf(report, out + "/audit.pdf");
    std::cout << "PASS XML native merge: mapping/conflict, atomic append, retry/correction, stale/cancel, lock, "
                 "save/recovery/audit\n";
}
void ui() {
    const auto source = importProjectXml(xmlProgressFixture(), "merge.xml");
    const auto original = target(source);
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document doc(store);
    doc.open(path);
    MainWindow window(doc);
    window.show();
    check(window.findChild<QAction *>("mergeProjectXmlProgress"), "merge menu action");
    QTimer timer;
    timer.setInterval(20);
    bool checked = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlProgressMergeDialog")
            return;
        timer.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlProgressMergeRows");
        check(table && table->rowCount() == 9, "all source rows visible");
        for (int i = 0; i < 4; ++i) {
            check(table->item(i, 0)->checkState() == Qt::Unchecked, "default keep current");
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(i, 2));
            check(combo->currentData().toString().isEmpty(), "no guessed mappings");
            combo->setCurrentIndex(i + 1);
            table->item(i, 0)->setCheckState(Qt::Checked);
        }
        check(table->item(1, 3)->text().contains("210") && table->item(1, 4)->text().contains("180"),
              "current/incoming conflict comparison");
        dialog->resize(800, 650);
        QDir().mkpath(QDir::currentPath() + "/xml-progress-merge-qa");
        dialog->grab().save(QDir::currentPath() + "/xml-progress-merge-qa/merge-conflict-small.png");
        dialog->findChild<QLineEdit *>("xmlMergeReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlMergeReason")->setText("진척 대사 확인");
        checked = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    check(reviewXmlProgressMerge(&window, doc, source) && checked && doc.project()->progress.size() == 5 &&
              doc.project()->progress.front() == original.progress.front(),
          "UI selected batch applied");
    const auto merged = *doc.project();
    window.resize(800, 650);
    QTimer::singleShot(30, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            dialog->resize(800, 650);
            QDir().mkpath(QDir::currentPath() + "/xml-progress-merge-qa");
            dialog->grab().save(QDir::currentPath() + "/xml-progress-merge-qa/merge-small.png");
            dialog->reject();
        }
    });
    check(!reviewXmlProgressMerge(&window, doc, source) && *doc.project() == merged, "UI cancel preserves current");
    bool stale = false;
    QTimer::singleShot(30, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "review modal");
        doc.renameTask(0, "검토 중 사용자 변경");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        stale = !dialog->findChild<QLabel *>("xmlMergeError")->text().isEmpty();
        dialog->reject();
    });
    check(!reviewXmlProgressMerge(&window, doc, source) && stale &&
              doc.project()->tasks[0].name == "검토 중 사용자 변경" && doc.project()->progress == merged.progress,
          "UI stale review preserves edits and ledger");
    std::cout << "PASS XML merge UI: manual mapping, conflict preview, explicit selection, apply, cancel/stale\n";
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
