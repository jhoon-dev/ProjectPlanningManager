#include "XmlPeriodFragmentFixture.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/PeriodActuals.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ResourceCurveExchange.h"
#include "calculation/Reports.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlPeriodActualDialog.h"
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
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <string_view>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool ok, const char *s) {
    if (!ok)
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
void save(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write evidence");
}
const XmlPeriodCandidate &candidate(const ProjectXmlExchange &source, std::string_view key) {
    const auto it = std::find_if(source.periodCandidates.begin(), source.periodCandidates.end(),
                                 [&](const auto &c) { return c.key == key; });
    check(it != source.periodCandidates.end(), "candidate missing");
    return *it;
}
auto prepare(Document &d, std::vector<PeriodActualSubmission> s, std::shared_ptr<CalculationControl> control = {}) {
    return Document::prepareEdit(d.makeEditRequest(PeriodActualBatchEdit{std::move(s), *d.project()->statusAt}),
                                 std::move(control));
}
void verify(const Project &p) {
    const auto latest = periodActualsAsOf(p, *p.statusAt);
    const auto *a = latest.at({"TN-A", "NR-1", "2026-11-10"});
    check(latest.size() == 5 && a->workUnitMinutes == 9001 && a->actualMinor == 1250,
          "one native date record holds exact fragment sums");
    const auto *zero = latest.at({"TN-A", "NR-1", "2026-11-11"});
    check(zero->workUnitMinutes == 0 && zero->actualMinor == 0, "zero fragments remain known zero");
    check(latest.at({"TN-A", "NR-1", "2026-11-12"})->actualMinor == 500 &&
              latest.at({"TN-B", "NR-1", "2026-11-10"})->workUnitMinutes == 1500,
          "missing measures preserve native facts");
    const auto balance = actualCostAllocations(p, *p.statusAt);
    check(balance.at("TN-A").cumulative == 10000 && balance.at("TN-A").allocated == 3775 &&
              balance.at("TN-A").unallocated == 6225,
          "fragment cost reconciles with task AC");
}
void contract() {
    const auto bytes = xmlPeriodFragmentFixture();
    const auto source = importProjectXml(bytes, "fragments.xml");
    const auto native = xmlPeriodTarget();
    const auto options = xmlPeriodOptions();
    const auto mapping = xmlPeriodMappings(source);
    const auto &c = candidate(source, "1/2026-11-10");
    check(c.issue.empty() && c.workParts == 2 && c.costParts == 2 && c.workUnitMinutes == 9001 &&
              c.actualMinor == 1250 && c.start == "2026-11-10 09:00" && c.finish == "2026-11-10 11:00",
          "adjacent per-measure fragments sum exactly");
    const auto &z = candidate(source, "1/2026-11-11");
    check(z.issue.empty() && z.workParts == 2 && z.costParts == 2 && z.workUnitMinutes == 0 && z.actualMinor == 0,
          "gaps add no work/cost");
    check(candidate(source, "1/2026-11-12").costParts == 0 && !candidate(source, "1/2026-11-12").actualMinor,
          "missing differs from explicit zero fragments");
    check(importProjectXml(xmlPeriodFragmentFixture(true), "reversed.xml").periodCandidates == source.periodCandidates,
          "input order does not change daily projection");
    for (int kind = 0; kind < 10; ++kind) {
        auto bad = bytes;
        if (kind == 0)
            bad.replace("<Start>2026-11-10T10:00:00</Start>", "<Start>2026-11-10T09:30:00</Start>");
        if (kind == 1)
            bad.replace(xmlPeriodFragment(2, "2026-11-10", "10:00", "11:00", "PT0H45M0S"),
                        xmlPeriodFragment(2, "2026-11-10", "09:00", "10:00", "PT0H45M0.6S"));
        if (kind == 2)
            bad.replace(xmlPeriodFragment(6, "2026-11-10", "10:00", "11:00", "750.00"),
                        xmlPeriodFragment(6, "2026-11-10", "09:30", "10:30", "750.00"));
        if (kind == 3)
            bad.replace(xmlPeriodFragment(2, "2026-11-10", "10:00", "11:00", "PT0H45M0S"),
                        xmlPeriodFragment(2, "2026-11-10", "09:15", "09:45", "PT0H45M0S"));
        if (kind == 4)
            bad.replace(xmlPeriodFragment(2, "2026-11-10", "10:00", "11:00", "PT0H45M0S"),
                        xmlPeriodFragment(2, "2026-11-10", "10:00", "10:00", "PT0H0M0S"));
        if (kind == 5)
            bad.replace("<Value>PT0H45M0.6S</Value>", "<Value>PT0H0M1S</Value>");
        if (kind == 6)
            bad.replace("<Value>750.00</Value>", "<Value>750.1</Value>");
        if (kind == 7)
            bad.replace("<Finish>2026-11-10T11:00:00</Finish>", "<Finish>2026-11-11T11:00:00</Finish>");
        if (kind == 8)
            bad.replace("<Finish>2026-11-10T11:00:00</Finish>", "<Finish>malformed</Finish>");
        if (kind == 9)
            bad.replace("<Start>2026-11-10T10:00:00</Start>", "<Start>2026-11-10T10:00:15</Start>");
        const auto invalid = importProjectXml(bad, "invalid.xml");
        check(!candidate(invalid, "1/2026-11-10").issue.empty(), "invalid measure invalidates entire date");
        rejects([&] { prepareXmlPeriodMerge(native, invalid, options, mapping); }, "partial bad date accepted");
    }
    for (const bool moneyOverflow : {false, true}) {
        auto atLimit = bytes;
        if (moneyOverflow) {
            atLimit.replace("<Value>500.00</Value>", "<Value>8999999999999.00</Value>");
            atLimit.replace("<Value>750.00</Value>", "<Value>1.00</Value>");
        } else {
            atLimit.replace("<Value>PT0H45M0.6S</Value>", "<Value>" + curveWorkText(maxWork - 1).toUtf8() + "</Value>");
            atLimit.replace("<Value>PT0H45M0S</Value>", "<Value>PT0H0M0.6S</Value>");
        }
        const auto edge = importProjectXml(atLimit, "limit.xml");
        const auto &edgeRow = candidate(edge, "1/2026-11-10");
        check(edgeRow.issue.empty() &&
                  (moneyOverflow ? edgeRow.actualMinor == maxMoney : edgeRow.workUnitMinutes == maxWork),
              "exact native limit accepted");
        auto overflow = atLimit;
        if (moneyOverflow)
            overflow.replace("<Value>1.00</Value>", "<Value>2.00</Value>");
        else
            overflow.replace("<Value>PT0H0M0.6S</Value>", "<Value>PT0H0M1.2S</Value>");
        const auto excess = importProjectXml(overflow, "overflow.xml");
        const auto &rejected = candidate(excess, "1/2026-11-10");
        check(!rejected.issue.empty() && !rejected.workUnitMinutes && !rejected.actualMinor,
              "overflow leaves no partial daily amount");
        rejects([&] { prepareXmlPeriodMerge(native, excess, options, mapping); }, "over-limit daily sum adopted");
    }
    auto tampered = source;
    tampered.periodCandidates.front().workParts = 20;
    rejects([&] { prepareXmlPeriodMerge(native, tampered, options, mapping); },
            "fragment review metadata tamper accepted");
    auto historical = native;
    historical.statusAt = "2026-11-10 10:30";
    auto historicalOptions = options;
    historicalOptions.statusAt = *historical.statusAt;
    rejects([&] { prepareXmlPeriodMerge(historical, source, historicalOptions, {{"1/2026-11-10", "TN-A", "NR-1"}}); },
            "only completed prefix of a date adopted");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, native, {});
    Document d(store);
    d.open(path);
    const auto batches = prepareXmlPeriodMerge(native, source, options, mapping);
    auto proposal = prepare(d, batches);
    check(d.applyPreparedEdit(*proposal) == EditState::Applied, "fragment batch apply");
    const auto merged = *d.project();
    verify(merged);
    check(merged.revision == 6 && merged.periodActuals.size() == 7 && merged.costEntries.size() == 4 &&
              std::equal(native.periodActuals.begin(), native.periodActuals.end(), merged.periodActuals.begin()) &&
              static_cast<const PlanInput &>(merged) == static_cast<const PlanInput &>(native) &&
              store.load(path).project == native,
          "one revision with original state/file preserved");
    check(prepareXmlPeriodMerge(merged, source, options, mapping).empty(), "same fragment source idempotent");
    PeriodActualSubmission correction;
    correction.reporter = "현장";
    correction.reason = "합산 후 정정";
    correction.recordedAt = "2026-11-12T09:01:00Z";
    auto entry = *periodActualsAsOf(merged, *merged.statusAt).at({"TN-A", "NR-1", "2026-11-10"});
    entry.workUnitMinutes = 7500;
    entry.actualMinor = 1300;
    correction.entries = {entry};
    correction.totals = {{"TN-A", 10000}};
    d.recordPeriodActuals(correction);
    const auto corrected = *d.project();
    check(prepareXmlPeriodMerge(corrected, source, options, mapping).empty(), "retry preserves manual correction");
    auto changed = bytes;
    changed.replace("<Value>500.00</Value>", "<Value>625.00</Value>");
    const auto update = importProjectXml(changed, "updated-fragments.xml");
    auto updated = prepareXmlPeriodMerge(corrected, update, options, mapping);
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    auto cancelled = prepare(d, updated, cancel);
    check(d.applyPreparedEdit(*cancelled) == EditState::Cancelled && *d.project() == corrected,
          "cancelled aggregate has no facts");
    auto low = changed;
    low.replace("<ActualCost>10000.00</ActualCost>", "<ActualCost>3000.00</ActualCost>");
    const auto lowSource = importProjectXml(low, "low.xml");
    auto failed = prepare(d, prepareXmlPeriodMerge(corrected, lowSource, options, mapping));
    check(d.applyPreparedEdit(*failed) == EditState::Failed && *d.project() == corrected,
          "aggregate AC shortfall rejects every date");
    auto stale = prepare(d, updated);
    d.renameTask(0, "보존할 새 편집");
    const auto edited = *d.project();
    check(d.applyPreparedEdit(*stale) == EditState::Stale && *d.project() == edited, "stale aggregate preserves edits");
    auto fresh = prepare(d, prepareXmlPeriodMerge(edited, update, options, mapping));
    check(d.applyPreparedEdit(*fresh) == EditState::Applied, "new source appends new aggregates");
    check(periodActualsAsOf(*d.project(), *d.project()->statusAt).at({"TN-A", "NR-1", "2026-11-10"})->previousEntryId ==
              corrected.periodActuals.back().id,
          "latest correction remains predecessor");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-12T09:02:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "aggregate recovery");
    d.save();
    check(store.load(path).project == *d.project() && nativeSchemaFor(*d.project()) == 11,
          "existing native schema11 roundtrip");
    const auto out = QDir::currentPath() + "/xml-period-fragment-qa";
    QDir().mkpath(out);
    save(out + "/original.xml", bytes);
    save(out + "/updated.xml", changed);
    save(out + "/before.json", QJsonDocument(json::encodeProject(native)).toJson());
    save(out + "/merged.json", QJsonDocument(json::encodeProject(merged)).toJson());
    save(out + "/corrected.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    for (const auto *name : {"source.ppm", "audit.xlsx", "audit.pdf"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), *d.project(), {});
    ReportOptions ro;
    ro.reportId = "xml-period-fragments";
    ro.generatedAt = options.recordedAt;
    const auto report = buildReport(*d.project(), ro);
    exportXlsx(report, out + "/audit.xlsx");
    exportPdf(report, out + "/audit.pdf");
    std::cout << "PASS XML daily fragments: exact sums/zeros/gaps, order independence, cross-measure ranges, "
                 "overlap/overflow rejection, one-revision ledger, retry/correction/cancel/stale, AC, "
                 "schema11/recovery/reports\n";
}
void ui() {
    const auto source = importProjectXml(xmlPeriodFragmentFixture(), "fragments.xml");
    const auto original = xmlPeriodTarget();
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
        if (!dialog || dialog->objectName() != "xmlPeriodActualDialog")
            return;
        timer.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlPeriodRows");
        check(table && table->rowCount() == 5, "daily review rows");
        for (std::size_t i = 0; i < source.periodCandidates.size(); ++i) {
            const auto &r = source.periodCandidates[i];
            check(table->item(int(i), 0)->checkState() == Qt::Unchecked, "aggregates not selected by default");
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(int(i), 3));
            check(combo->currentData().toInt() == -1, "no guessed mapping");
            combo->setCurrentIndex(r.assignmentUid == "1" ? 1 : r.assignmentUid == "2" ? 2 : 3);
            table->item(int(i), 0)->setCheckState(Qt::Checked);
            if (r.key == "1/2026-11-10")
                check(table->item(int(i), 5)->text().contains("9001u · 2구간") &&
                          table->item(int(i), 5)->text().contains("12.5 USD · 2구간"),
                      "both measure counts and exact sums visible");
        }
        dialog->findChild<QLineEdit *>("xmlPeriodReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlPeriodReason")->setText("겹침 없는 구간 합산 확인");
        dialog->resize(800, 650);
        QDir().mkpath(QDir::currentPath() + "/xml-period-fragment-qa");
        dialog->grab().save(QDir::currentPath() + "/xml-period-fragment-qa/fragments-small.png");
        table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());
        dialog->grab().save(QDir::currentPath() + "/xml-period-fragment-qa/fragments-values-small.png");
        checked = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    check(reviewXmlPeriodActuals(&window, d, source) && checked, "aggregate UI applies");
    verify(*d.project());
    const auto merged = *d.project();
    QTimer::singleShot(30, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    check(!reviewXmlPeriodActuals(&window, d, source) && *d.project() == merged, "aggregate review cancel");
    std::cout << "PASS XML fragments UI: explicit day selection, original/native assignment IDs, per-measure "
                 "counts/values, worker apply/cancel, small window\n";
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
