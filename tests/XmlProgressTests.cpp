#include "XmlProgressFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Reports.h"
#include "domain/ProgressSource.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlProgressDialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <algorithm>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read output");
    return f.readAll();
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, message);
}
std::string field(const Project &p, const std::string &name) {
    for (const auto &f : p.customFields)
        if (f.name == name)
            return "field:" + f.id;
    throw std::runtime_error("field missing");
}
void verify(const Project &p) {
    validate(p);
    ScheduleFormulaScope scope(p);
    check(p.progress.size() >= 4 && p.statusAt == "2026-11-12 12:00", "adopted progress count/status");
    check(xmlFormulaValue(p, FieldScope::Task, field(p, "AS"), &p.tasks[0]).value == "2026-11-12T09:00:00",
          "Actual Start from adopted ledger");
    check(xmlFormulaValue(p, FieldScope::Task, field(p, "AF"), &p.tasks[0]).value == "2026-11-12T11:00:00",
          "Actual Finish from adopted ledger");
    check(xmlFormulaValue(p, FieldScope::Task, field(p, "AD"), &p.tasks[0]).value == "120", "explicit Actual Duration");
    check(xmlFormulaValue(p, FieldScope::Task, field(p, "AD"), &p.tasks[2]).value == "0" &&
              xmlFormulaValue(p, FieldScope::Task, field(p, "AD"), &p.tasks[8]).missing,
          "reported zero versus unreported");
    check(xmlFormulaValue(p, FieldScope::Task, field(p, "AW"), &p.tasks[0]).missing && p.periodActuals.empty() &&
              p.costEntries.empty(),
          "no work/cost inference");
    check(p.progress[0].reason.starts_with(xmlProgressSourcePrefix) && p.progress[0].previousEntryId.empty(),
          "provenance and first entry");
}
void contract() {
    const auto bytes = xmlProgressFixture();
    auto plan = importProjectXml(bytes, "progress.xml");
    const auto before = plan.project;
    check(plan.progressCandidates.size() == 9 && plan.progressStatusAt == "2026-11-12 12:00" &&
              plan.project.progress.empty() && !plan.project.statusAt,
          "default import remains plan only");
    check(!plan.progressCandidates[4].issue.empty() && !plan.progressCandidates[6].issue.empty() &&
              !plan.progressCandidates[7].issue.empty() && !plan.progressCandidates[8].present,
          "incomplete/precision/no data candidates");
    const auto options = xmlProgressOptions(plan);
    auto adopted = adoptXmlProgress(plan, options);
    check(plan.project == before && adopted.project.revision == 2 && adopted.project.baselines.empty(),
          "pure preparation, new copy only");
    verify(adopted.project);
    check(adoptXmlProgress(plan, options).project == adopted.project, "deterministic retry without duplicates");
    rejects([&] { adoptXmlProgress(adopted, options); }, "already adopted cannot append again");
    for (int kind = 0; kind < 8; ++kind) {
        auto o = options;
        if (kind == 0)
            o.confirmed = false;
        if (kind == 1)
            o.statusAt.clear();
        if (kind == 2)
            o.taskIds.push_back(o.taskIds[0]);
        if (kind == 3)
            o.taskIds = {"UNKNOWN"};
        if (kind == 4)
            o.taskIds = {plan.progressCandidates[4].taskId};
        if (kind == 5)
            o.taskIds = {plan.progressCandidates[5].taskId};
        if (kind == 6)
            o.taskIds = {plan.progressCandidates[6].taskId};
        if (kind == 7)
            o.taskIds = {plan.progressCandidates[7].taskId};
        rejects([&] { adoptXmlProgress(plan, o); }, "invalid selection/status never partially commits");
        check(plan.project == before, "failed batch preserves input");
    }
    for (int kind = 0; kind < 4; ++kind) {
        auto changed = plan;
        if (kind == 0)
            changed.xml.append(' ');
        if (kind == 1)
            changed.project.tasks[0].name = "Changed";
        if (kind == 2)
            changed.progressCandidates[0].actualMinutes = 1;
        if (kind == 3)
            changed.project.progress = adopted.project.progress;
        rejects([&] { adoptXmlProgress(changed, options); }, "changed source/candidate/native ledger rejected");
    }
    auto unknownStatus = bytes;
    unknownStatus.replace("<StatusDate>2026-11-12T12:00:00</StatusDate>", "<StatusDate>NA</StatusDate>");
    auto missing = importProjectXml(unknownStatus, "missing.xml");
    check(!missing.progressStatusAt && !missing.progressStatusIssue.empty(), "unknown source status is explicit");
    verify(adoptXmlProgress(missing, xmlProgressOptions(missing)).project);
    auto future = options;
    future.statusAt = "2026-11-12 14:00";
    future.taskIds = {plan.progressCandidates[5].taskId};
    check(adoptXmlProgress(plan, future).project.progress.size() == 1, "explicitly reviewed status override");
    auto marker = bytes;
    marker.replace("<ActualStart>2026-11-12T11:30:00</ActualStart>", "<ActualStart>NA</ActualStart>");
    marker.replace("<ActualFinish>2026-11-12T11:30:00</ActualFinish>", "<ActualFinish>NA</ActualFinish>");
    auto zero = importProjectXml(marker, "marker.xml");
    auto zeroAdopted = adoptXmlProgress(zero, xmlProgressOptions(zero));
    check(!zeroAdopted.project.progress[3].actualStart && !zeroAdopted.project.progress[3].actualFinish,
          "zero remaining milestone does not invent completion dates");
    auto percent = bytes;
    percent.replace("<PercentComplete>25</PercentComplete>", "<PercentComplete>99</PercentComplete>");
    auto percentPlan = importProjectXml(percent, "percent.xml");
    check(adoptXmlProgress(percentPlan, xmlProgressOptions(percentPlan)).project.progress[1].actualDurationMinutes ==
              60,
          "percent complete does not manufacture duration");
    auto duplicate = bytes;
    const auto uid = QByteArray::fromStdString(plan.progressCandidates[0].xmlUid);
    duplicate.replace("<UID>" + QByteArray::fromStdString(plan.progressCandidates[1].xmlUid) + "</UID>",
                      "<UID>" + uid + "</UID>");
    rejects([&] { importProjectXml(duplicate, "duplicate.xml"); }, "duplicate XML task UID refused");
    auto noFields = importProjectXml(xmlProgressFixture(false), "plain.xml");
    check(nativeSchemaFor(adoptXmlProgress(noFields, xmlProgressOptions(noFields)).project) == 9,
          "existing progress schema, no new layout");
    check(nativeSchemaFor(adopted.project) == 48 &&
              json::decodeProject(json::encodeProject(adopted.project), 48) == adopted.project,
          "existing formula schema48 roundtrip");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        adoptXmlProgress(plan, options);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation propagated");
    const auto out = QDir::currentPath() + "/xml-progress-qa";
    QDir().mkpath(out);
    save(out + "/original.xml", bytes);
    save(out + "/native.json", QJsonDocument(json::encodeProject(adopted.project)).toJson());
    save(out + "/conversion.json", QJsonDocument(adopted.report).toJson());
    auto exported = exportProjectXml(adopted.project);
    save(out + "/fields.xml", exported.xml);
    save(out + "/export-mapping.json", QJsonDocument(exported.report).toJson());
    ReportOptions ro;
    ro.reportId = "xml-progress";
    ro.generatedAt = "2026-11-12T03:00:00Z";
    for (const auto *n : {"fields.xlsx", "fields.pdf", "source.ppm", "corrected.xlsx"})
        QFile::remove(out + '/' + n);
    const auto report = buildReport(adopted.project, ro);
    exportXlsx(report, out + "/fields.xlsx");
    exportPdf(report, out + "/fields.pdf");
    const auto audit = std::find_if(report.tables.begin(), report.tables.end(),
                                    [](const auto &t) { return t.id == "xmlProgressAudit"; });
    check(audit != report.tables.end() && audit->rows.size() == 4, "source audit report");
    JsonProjectStore store;
    const auto path = (out + "/source.ppm").toStdString();
    store.save(path, adopted.project, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    const auto importedEntries = doc.project()->progress;
    ProgressEntry correction = doc.project()->progress[1];
    correction.reason = "현장 정정";
    correction.reporter = "PM";
    correction.recordedAt = "2026-11-12T03:01:00Z";
    correction.actualDurationMinutes = 50;
    correction.remainingMinutes = 150;
    doc.recordProgress(correction);
    check(doc.project()->progress.size() == 5 &&
              std::equal(importedEntries.begin(), importedEntries.end(), doc.project()->progress.begin()) &&
              doc.project()->progress.back().previousEntryId == importedEntries[1].id && !doc.canUndo(),
          "native correction appends and retains imported history");
    exportXlsx(buildReport(*doc.project(), ro), out + "/corrected.xlsx");
    doc.captureBaseline("채택 후 계획", "PM", "보존", "2026-11-12T03:02:00Z");
    doc.saveScenario("채택 진척", "task-order", 260, "2026-11-12T03:03:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-12", "검증"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "기록 보존";
    fx.recordedAt = "2026-11-12T03:04:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(doc.project()->progress == importedEntries || doc.project()->progress[0] == importedEntries[0],
          "currency conversion preserves provenance");
    auto migrated = migrateLedgerIdentities(*doc.project(), {{"task", importedEntries[0].taskId, "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), nativeSchemaFor(migrated)) == migrated &&
              migrated.progress[0].reason == importedEntries[0].reason,
          "archive/ID roundtrip preserves original UID/hash");
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T03:05:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery");
    auto folder = publishProjectXml(adopted, tmp.filePath("published"));
    check(read(folder + "/original.xml") == bytes &&
              store.load((folder + "/project.ppm").toStdString()).project == adopted.project,
          "atomic package original+native replay");
    rejects([&] { publishProjectXml(adopted, folder); }, "no directory overwrite/re-import duplication");
    auto tampered = adopted;
    tampered.project.progress[0].actualDurationMinutes = 119;
    rejects([&] { publishProjectXml(tampered, tmp.filePath("tampered")); }, "tampered result cannot publish");
    check(!QFileInfo::exists(tmp.filePath("tampered")), "failed publish leaves no destination");
    std::cout << "PASS XML task progress adoption: explicit new copy, raw mapping/source replay, "
                 "missing/zero/status/precision, atomic/retry, native correction/archives/recovery/reports\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto old = xmlActualFixture();
    const auto oldPath = temp.filePath("old.ppm").toStdString();
    store.save(oldPath, old, {});
    Document doc(store);
    doc.open(oldPath);
    MainWindow window(doc);
    window.show();
    const auto input = temp.filePath("incoming.xml");
    save(input, xmlProgressFixture());
    const auto destination = temp.filePath("adopted");
    bool sawPreview = false, sawReview = false, captured = false;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (!modal)
            return;
        if (auto *f = qobject_cast<QFileDialog *>(modal)) {
            f->findChild<QLineEdit *>("fileNameEdit")->setText(input);
            QMetaObject::invokeMethod(f, "accept");
            return;
        }
        if (modal->objectName() == "projectXmlPreview" && !sawPreview) {
            sawPreview = true;
            auto *include = modal->findChild<QCheckBox *>("xmlIncludeProgress");
            check(include && !include->isChecked(), "opt in default");
            include->setChecked(true);
            modal->findChild<QLineEdit *>("xmlDestination")->setText(destination);
            modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return;
        }
        if (modal->objectName() == "xmlProgressDialog" && !sawReview) {
            sawReview = true;
            check(*doc.project() == old, "review leaves current observed ledger untouched");
            auto *rows = modal->findChild<QTableWidget *>("xmlProgressRows");
            check(rows && rows->rowCount() == 9, "mapping rows");
            for (int i = 0; i < 4; ++i)
                rows->item(i, 0)->setCheckState(Qt::Checked);
            check(!(rows->item(4, 0)->flags() & Qt::ItemIsEnabled), "incomplete row disabled");
            modal->findChild<QLineEdit *>("xmlProgressReporter")->setText("PM");
            modal->findChild<QLineEdit *>("xmlProgressReason")->setText("검토 후 채택");
            const auto out = QDir::currentPath() + "/xml-progress-qa";
            QDir().mkpath(out);
            modal->resize(800, 650);
            QApplication::processEvents();
            captured = modal->grab().save(out + "/progress-small.png");
            modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return;
        }
        if (auto *m = qobject_cast<QMessageBox *>(modal)) {
            std::cerr << m->text().toStdString();
            m->reject();
        }
    });
    QTimer::singleShot(30000, [] {
        std::cerr << "XML progress UI timed out";
        std::_Exit(2);
    });
    poll.start();
    window.findChild<QAction *>("importProjectXml")->trigger();
    poll.stop();
    check(sawPreview && sawReview && captured && doc.project()->progress.size() == 4 && !doc.dirty(),
          "menu adoption opened fresh saved copy");
    check(store.load(oldPath).project == old, "previous native ledger file preserved");
    verify(*doc.project());
    auto prepared = importProjectXml(xmlProgressFixture(), "cancel.xml");
    const auto current = *doc.project();
    QTimer::singleShot(30, [] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    check(!reviewXmlProgress(&window, doc, prepared) && *doc.project() == current, "cancel review preserves document");
    bool staleChecked = false;
    QTimer stale;
    stale.setInterval(20);
    QObject::connect(&stale, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "xmlProgressDialog")
            return;
        stale.stop();
        doc.renameTask(0, "보존할 새 사용자 수정");
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        staleChecked = d->findChild<QLabel *>("xmlProgressError")->text().contains("변경");
        d->reject();
    });
    stale.start();
    check(!reviewXmlProgress(&window, doc, prepared) && staleChecked &&
              doc.project()->tasks[0].name == "보존할 새 사용자 수정",
          "stale review refuses and preserves user edit");
    std::cout << "PASS XML progress UI: opt in/mapping/metadata/partial input, worker prepare, new published copy, "
                 "previous ledger/cancel preservation\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
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
        if (auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &s : a->diagnostic().arguments)
                std::cerr << ' ' << s;
        return 1;
    }
}
