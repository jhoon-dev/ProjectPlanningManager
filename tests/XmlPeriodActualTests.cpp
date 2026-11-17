#include "XmlPeriodActualFixture.h"
#include "application/Document.h"
#include "domain/PeriodActuals.h"
#include "domain/Classification.h"
#include "domain/FinancialPeriods.h"
#include "domain/LedgerIdentity.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "calculation/Reports.h"
#include "calculation/Usage.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlPeriodActualDialog.h"
#include <QAction>
#include <QApplication>
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
#include <QRegularExpression>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f, const char *s) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error(s);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
auto prepared(Document &d, std::vector<PeriodActualSubmission> batches,
              std::shared_ptr<CalculationControl> control = {}) {
    return Document::prepareEdit(d.makeEditRequest(PeriodActualBatchEdit{std::move(batches), *d.project()->statusAt}),
                                 std::move(control));
}
const PeriodActualEntry &row(const Project &p, const std::string &task, const std::string &resource,
                             const std::string &date) {
    return *periodActualsAsOf(p, *p.statusAt).at({task, resource, date});
}
void verify(const Project &p) {
    validate(p);
    const auto latest = periodActualsAsOf(p, *p.statusAt);
    check(latest.size() == 5, "five dated assignment observations");
    check(row(p, "TN-A", "NR-1", "2026-11-10").workUnitMinutes == 9001 &&
              row(p, "TN-A", "NR-1", "2026-11-10").actualMinor == 1250,
          "exact 0.6s work unit and USD cents");
    check(row(p, "TN-A", "NR-1", "2026-11-11").workUnitMinutes == 0 &&
              row(p, "TN-A", "NR-1", "2026-11-11").actualMinor == 0,
          "explicit zero preserved");
    check(row(p, "TN-A", "NR-1", "2026-11-12").actualMinor == 500 &&
              row(p, "TN-A", "NR-1", "2026-11-12").workUnitMinutes == 3000,
          "missing XML cost preserves native observation");
    check(row(p, "TN-B", "NR-1", "2026-11-10").workUnitMinutes == 1500,
          "missing XML work preserves native observation");
    const auto balance = actualCostAllocations(p, *p.statusAt);
    check(costAsOf(p, "TN-A", *p.statusAt)->physicalBasisPoints == 2500, "existing physical progress preserved");
    check(balance.at("TN-A").cumulative == 10000 && balance.at("TN-A").allocated == 3775 &&
              balance.at("TN-A").unallocated == 6225 && balance.at("TN-B").cumulative == 4000 &&
              balance.at("TN-B").allocated == 1000,
          "cumulative AC not added to dated allocation");
    std::int64_t work = 0;
    for (const auto &[key, e] : latest) {
        (void)key;
        work += e->workUnitMinutes.value_or(0);
    }
    check(work == 25501 && p.progress.empty(), "no scalar/overtime/duration/rate inference or fabricated progress");
    UsageOptions u;
    u.from = "2026-11-10";
    u.to = "2026-11-12";
    const auto usage = buildUsage(p, u);
    std::int64_t reportedWork = 0, reportedCost = 0;
    for (const auto &leaf : usage.leaves)
        for (const auto &v : leaf.reportedActual) {
            reportedWork += v.labor.value_or(0) + v.equipment.value_or(0);
            reportedCost += v.cost.value_or(0);
        }
    check(reportedWork == 25501 && reportedCost == 4775, "usage reports measured work and dated cost");
}
void contract() {
    const auto source = importProjectXml(xmlPeriodFixture(), "period.xml");
    const auto original = xmlPeriodTarget();
    const auto options = xmlPeriodOptions();
    const auto mapping = xmlPeriodMappings(source);
    check(mapping.size() == 5 && source.project.periodActuals.empty() && source.project.costEntries.empty() &&
              !source.project.statusAt,
          "plan import remains non-adopting");
    check(std::count_if(source.periodCandidates.begin(), source.periodCandidates.end(),
                        [](const auto &r) { return !r.issue.empty(); }) == 5,
          "invalid multi-day/unit/duplicate/precision candidates retained");
    const auto batches = prepareXmlPeriodMerge(original, source, options, mapping);
    check(batches.size() == 3 && batches.front().entries.size() == 3, "one provenance batch per source assignment");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    {
        auto future = original;
        auto c = future.costEntries.front();
        c.id = "future-ac";
        c.previousEntryId = future.costEntries.front().id;
        c.statusAt = "2026-11-14 18:00";
        c.recordedRevision = 5;
        c.actualMinor = 9000;
        future.costEntries.push_back(c);
        auto e = future.periodActuals.front();
        e.id = "future-period";
        e.previousEntryId = future.periodActuals.front().id;
        e.statusAt = c.statusAt;
        e.recordedRevision = 5;
        e.costEntryId = c.id;
        e.workUnitMinutes = 1000;
        e.actualMinor = 700;
        future.periodActuals.push_back(e);
        const auto futurePath = tmp.filePath("future.ppm").toStdString();
        store.save(futurePath, future, {});
        Document fd(store);
        fd.open(futurePath);
        auto fp = prepared(fd, prepareXmlPeriodMerge(future, source, options, mapping));
        check(fd.applyPreparedEdit(*fp) == EditState::Applied && fd.project()->periodActuals[2] == e &&
                  row(*fd.project(), "TN-A", "NR-1", "2026-11-12").previousEntryId == e.id,
              "future reports preserved and full append chain linked");
        verify(*fd.project());
    }
    Document d(store);
    d.open(path);
    auto proposal = prepared(d, batches);
    check(*d.project() == original && proposal->state() == EditState::Ready, "isolated worker proposal");
    check(d.applyPreparedEdit(*proposal) == EditState::Applied, "atomic period batch commit");
    const auto merged = *d.project();
    verify(merged);
    check(merged.revision == 6 && merged.periodActuals.size() == 7 && merged.costEntries.size() == 4 && !d.canUndo() &&
              static_cast<const PlanInput &>(merged) == static_cast<const PlanInput &>(original) &&
              merged.statusAt == original.statusAt &&
              std::equal(original.periodActuals.begin(), original.periodActuals.end(), merged.periodActuals.begin()) &&
              merged.costEntries.front() == original.costEntries.front(),
          "one revision and immutable original plan/ledgers");
    check(store.load(path).project == original, "source file not auto-overwritten");
    for (std::size_t i = 2; i < merged.periodActuals.size(); ++i)
        check(merged.periodActuals[i].recordedRevision == 6 &&
                  merged.periodActuals[i].reason.starts_with("[XML-PERIOD sha256=") &&
                  merged.periodActuals[i].resourceType ==
                      (merged.periodActuals[i].resourceId == "NR-1" ? "equipment" : "labor"),
              "source provenance and native resource kind");
    check(d.applyPreparedEdit(*proposal) == EditState::Stale, "proposal single-use");
    auto repeated = prepareXmlPeriodMerge(merged, source, options, mapping);
    check(repeated.empty(), "duplicate source no facts");
    auto noop = prepared(d, repeated);
    check(d.applyPreparedEdit(*noop) == EditState::NoChange && *d.project() == merged, "duplicate no revision");
    PeriodActualSubmission correction;
    correction.reporter = "현장";
    correction.reason = "후속 정정";
    correction.recordedAt = "2026-11-12T09:01:00Z";
    auto correctedRow = row(merged, "TN-A", "NR-1", "2026-11-10");
    correctedRow.workUnitMinutes = 7500;
    correctedRow.actualMinor = 1300;
    correction.entries = {correctedRow};
    correction.totals = {{"TN-A", 10000}};
    d.recordPeriodActuals(correction);
    const auto corrected = *d.project();
    check(prepareXmlPeriodMerge(corrected, source, options, mapping).empty(),
          "old XML cannot overwrite later correction");
    auto remapped = mapping;
    remapped.front().resourceId = "NR-2";
    rejects([&] { prepareXmlPeriodMerge(corrected, source, options, remapped); },
            "redirected source assignment accepted");
    auto migrated = migrateLedgerIdentities(corrected, {{"task", "TN-A", "NEW-A"}, {"resource", "NR-1", "NEW-R"}});
    remapped = mapping;
    for (auto &m : remapped) {
        if (m.taskId == "TN-A")
            m.taskId = "NEW-A";
        if (m.resourceId == "NR-1")
            m.resourceId = "NEW-R";
    }
    check(prepareXmlPeriodMerge(migrated, source, options, remapped).empty(), "idempotency survives ID migration");
    for (int kind = 0; kind < 9; ++kind) {
        auto xml = source;
        auto m = mapping;
        auto o = options;
        if (kind == 0)
            m.push_back(m.front());
        if (kind == 1)
            m.front().resourceId = "absent";
        if (kind == 2)
            o.statusAt = "2026-11-12 19:00";
        if (kind == 3)
            o.confirmed = false;
        if (kind == 4)
            xml.xml += " ";
        if (kind == 5)
            xml.periodCandidates.front().workUnitMinutes = 1;
        if (kind == 6)
            o.reason = "  ";
        if (kind == 7)
            m = {{"3/2026-11-20", "TN-B", "NR-1"}};
        if (kind == 8)
            m = {{"3/2026-11-15", "TN-B", "NR-1"}};
        rejects([&] { prepareXmlPeriodMerge(original, xml, o, m); }, "invalid source/metadata/mapping/period accepted");
    }
    rejects([&] { prepareXmlPeriodMerge(xmlPeriodTarget("KRW"), source, options, mapping); },
            "currency implicitly converted");
    auto unknownCurrency = source.xml;
    unknownCurrency.replace("<CurrencyCode>USD</CurrencyCode>", "");
    auto unknown = importProjectXml(unknownCurrency, "missing-currency.xml");
    rejects([&] { prepareXmlPeriodMerge(original, unknown, options, mapping); }, "missing currency inferred for money");
    const auto workOnly = prepareXmlPeriodMerge(original, unknown, options, {{"1/2026-11-12", "TN-A", "NR-1"}});
    check(workOnly.front().entries.front().actualMinor == 500 && workOnly.front().totals.front().actualMinor == 5000,
          "currency-free work preserves explicitly reported native cost and AC");
    auto undated = QString::fromUtf8(source.xml);
    undated.remove(QRegularExpression("<TimephasedData>[\\s\\S]*?</TimephasedData>"));
    const auto undatedSource = importProjectXml(undated.toUtf8(), "undated.xml");
    check(!undatedSource.periodCandidates.empty() &&
              std::all_of(undatedSource.periodCandidates.begin(), undatedSource.periodCandidates.end(),
                          [](const auto &r) { return !r.issue.empty(); }),
          "scalar actual totals never assigned invented dates");
    auto exact = source.xml;
    exact.replace("<Value>1250.00</Value>", "<Value>1250.0000</Value>");
    const auto exactSource = importProjectXml(exact, "trailing-zero.xml");
    check(prepareXmlPeriodMerge(original, exactSource, options, xmlPeriodMappings(exactSource))
                  .front()
                  .entries.front()
                  .actualMinor == 1250,
          "exact trailing decimal zeros accepted without rounding");
    auto changed = source.xml;
    changed.replace("<Value>1250.00</Value>", "<Value>1375.00</Value>");
    const auto changedSource = importProjectXml(changed, "updated.xml");
    auto updated = prepareXmlPeriodMerge(corrected, changedSource, options, mapping);
    auto partial = updated;
    partial.back().entries.back().actualMinor = -1;
    auto invalid = prepared(d, partial);
    check(d.applyPreparedEdit(*invalid) == EditState::Failed && *d.project() == corrected,
          "bad last assignment cannot partially commit");
    auto insufficient = changed;
    insufficient.replace("<ActualCost>10000.00</ActualCost>", "<ActualCost>100.00</ActualCost>");
    const auto lowSource = importProjectXml(insufficient, "insufficient-ac.xml");
    auto low = prepared(d, prepareXmlPeriodMerge(corrected, lowSource, options, mapping));
    check(d.applyPreparedEdit(*low) == EditState::Failed && *d.project() == corrected,
          "cumulative AC below all dated allocations rejected atomically");
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    auto cancelled = prepared(d, updated, cancel);
    check(d.applyPreparedEdit(*cancelled) == EditState::Cancelled && *d.project() == corrected,
          "cancelled worker no facts");
    auto stale = prepared(d, updated);
    d.renameTask(0, "보존할 새 사용자 변경");
    const auto edited = *d.project();
    check(d.applyPreparedEdit(*stale) == EditState::Stale && *d.project() == edited,
          "stale proposal preserves user edits");
    auto fresh = prepared(d, prepareXmlPeriodMerge(edited, changedSource, options, mapping));
    check(d.applyPreparedEdit(*fresh) == EditState::Applied, "new source explicit correction");
    check(row(*d.project(), "TN-A", "NR-1", "2026-11-10").previousEntryId == corrected.periodActuals.back().id,
          "latest correction chain preserved");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-12T09:02:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid,
          "period adoption recovery");
    d.save();
    check(store.load(path).project == *d.project() && nativeSchemaFor(*d.project()) == 11 &&
              json::decodeProject(json::encodeProject(*d.project()), 11) == *d.project(),
          "native schema11 file/JSON roundtrip");
    Document locked(store);
    const auto lockPath = tmp.filePath("locked.ppm").toStdString();
    store.save(lockPath, original, {});
    locked.open(lockPath);
    locked.captureBaseline("승인", "PM", "기간 마감", options.recordedAt);
    FinancialPeriodSubmission close;
    close.period.name = "관리기간";
    close.period.baselineId = locked.project()->baselines.front().id;
    close.period.fromDate = "2026-11-10";
    close.period.toDate = "2026-11-12";
    close.reporter = "PM";
    close.reason = "마감";
    close.recordedAt = options.recordedAt;
    close.confirmed = true;
    const auto periodId = locked.recordFinancialPeriod(close);
    close.period = financialPeriod(*locked.project(), periodId).period;
    close.action = "close";
    locked.setStatusAt("2026-11-13 00:00");
    locked.recordFinancialPeriod(close);
    locked.setStatusAt(options.statusAt);
    const auto frozen = *locked.project();
    auto rejected = prepared(locked, prepareXmlPeriodMerge(frozen, source, options, mapping));
    check(locked.applyPreparedEdit(*rejected) == EditState::Failed && *locked.project() == frozen,
          "closed period protects all assignments");
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.makeEditRequest(PeriodActualBatchEdit{batches, options.statusAt}); }, "read-only accepted");
    const auto out = QDir::currentPath() + "/xml-period-actual-qa";
    QDir().mkpath(out);
    save(out + "/original.xml", source.xml);
    save(out + "/before.json", QJsonDocument(json::encodeProject(original)).toJson());
    save(out + "/merged.json", QJsonDocument(json::encodeProject(merged)).toJson());
    save(out + "/corrected.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    for (const auto *file : {"source.ppm", "audit.xlsx", "audit.pdf"})
        QFile::remove(out + '/' + file);
    store.save((out + "/source.ppm").toStdString(), *d.project(), {});
    ReportOptions ro;
    ro.reportId = "xml-period";
    ro.generatedAt = options.recordedAt;
    const auto report = buildReport(*d.project(), ro);
    exportXlsx(report, out + "/audit.xlsx");
    exportPdf(report, out + "/audit.pdf");
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "KRW", "2026-11-12", "명시 환율"};
    fx.rate.numerator = 1400;
    fx.rate.denominator = 1;
    fx.reporter = "PM";
    fx.reason = "출처 원장 보존";
    fx.recordedAt = "2026-11-12T09:03:00Z";
    fx.confirmed = true;
    const auto sourceReason = d.project()->periodActuals[2].reason;
    d.convertCurrency(fx);
    check(d.project()->currency == "KRW" && d.project()->periodActuals[2].reason == sourceReason &&
              prepareXmlPeriodMerge(*d.project(), changedSource, options, mapping).empty(),
          "currency conversion preserves origin and duplicate protection");
    std::cout << "PASS XML assignment periods: exact work/money, explicit zero/null, AC reconciliation, atomic append, "
                 "retry/correction, mapping/ID migration, locks/stale/cancel, schema11 recovery/report\n";
}
void ui() {
    const auto source = importProjectXml(xmlPeriodFixture(), "period.xml");
    const auto original = xmlPeriodTarget();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document d(store);
    d.open(path);
    MainWindow window(d);
    window.show();
    check(window.findChild<QAction *>("mergeProjectXmlActuals"), "period menu action");
    QTimer timer;
    timer.setInterval(20);
    bool reviewed = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlPeriodActualDialog")
            return;
        timer.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlPeriodRows");
        check(table, "period table");
        for (std::size_t i = 0; i < source.periodCandidates.size(); ++i) {
            const auto &r = source.periodCandidates[i];
            check(table->item(int(i), 0)->checkState() == Qt::Unchecked, "default retain current");
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(int(i), 3));
            check(combo->currentData().toInt() == -1, "no guessed assignment");
            if (!r.issue.empty() || r.finish > "2026-11-12 18:00")
                continue;
            combo->setCurrentIndex(r.assignmentUid == "1" ? 1 : r.assignmentUid == "2" ? 2 : 3);
            table->item(int(i), 0)->setCheckState(Qt::Checked);
            if (r.key == "1/2026-11-12")
                check(table->item(int(i), 4)->text().contains("2000u") &&
                          table->item(int(i), 5)->text().contains("3000u"),
                      "existing/incoming work comparison");
        }
        dialog->findChild<QLineEdit *>("xmlPeriodReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlPeriodReason")->setText("배정·날짜 대사 완료");
        dialog->resize(800, 650);
        QDir().mkpath(QDir::currentPath() + "/xml-period-actual-qa");
        dialog->grab().save(QDir::currentPath() + "/xml-period-actual-qa/period-small.png");
        reviewed = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    check(reviewXmlPeriodActuals(&window, d, source) && reviewed, "UI adoption");
    verify(*d.project());
    const auto merged = *d.project();
    QTimer::singleShot(30, [] {
        if (auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dlg->reject();
    });
    check(!reviewXmlPeriodActuals(&window, d, source) && *d.project() == merged, "cancel retains ledger");
    bool stale = false;
    QTimer::singleShot(30, [&] {
        auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dlg, "stale modal");
        d.renameTask(0, "검토 중 새 편집");
        dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        stale = !dlg->findChild<QLabel *>("xmlPeriodError")->text().isEmpty();
        dlg->reject();
    });
    check(!reviewXmlPeriodActuals(&window, d, source) && stale && d.project()->periodActuals == merged.periodActuals,
          "stale review preserves ledger");
    std::cout << "PASS XML period UI: manual assignment mapping, exact daily/AC comparison, explicit selection, small "
                 "layout, native apply/cancel/stale\n";
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
