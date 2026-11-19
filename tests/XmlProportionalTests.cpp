#include "XmlProportionalFixture.h"
#include "application/Document.h"
#include "application/PeriodActualEditing.h"
#include "domain/ActualProvenance.h"
#include "domain/Classification.h"
#include "domain/FinancialPeriods.h"
#include "domain/LedgerIdentity.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ResourceCurveExchange.h"
#include "calculation/Reports.h"
#include "calculation/Usage.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include "ui/XmlActualAllocationDialog.h"
#include "ui/XmlPeriodActualDialog.h"
#include <QApplication>
#include <QCheckBox>
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
void save(const QString &path, const QByteArray &b) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write evidence");
}
auto prepared(Document &d, std::vector<PeriodActualSubmission> b, std::shared_ptr<CalculationControl> c = {}) {
    return Document::prepareEdit(d.makeEditRequest(PeriodActualBatchEdit{std::move(b), *d.project()->statusAt}), c);
}
const PeriodActualEntry &day(const Project &p, const char *task, const char *resource, const char *date) {
    return *periodActualsAsOf(p, *p.statusAt).at({task, resource, date});
}
void verify(const Project &p) {
    const auto &a = day(p, "NA", "NR1", "2026-11-13"), &b = day(p, "NA", "NR1", "2026-11-16"),
               &weekend = day(p, "NA", "NR1", "2026-11-14"), &c = day(p, "NB", "NR2", "2026-11-16");
    check(a.workUnitMinutes == 51 && a.actualMinor == 2501 && b.workUnitMinutes == 50 && b.actualMinor == 2500 &&
              c.workUnitMinutes == 3 && c.actualMinor == 1,
          "largest remainders preserve exact work/cost");
    check(weekend.workUnitMinutes == 222 && weekend.actualMinor == 700 && !proportionalActual(weekend) &&
              proportionalWork(a) && proportionalCost(a),
          "zero weight days preserve existing facts; estimated flags explicit");
    const auto balance = actualCostAllocations(p, *p.statusAt);
    check(balance.at("NA").cumulative == 15000 && balance.at("NA").allocated == 5701 &&
              balance.at("NA").unallocated == 9299 && balance.at("NB").allocated == 1,
          "source cumulative versus dated estimates reconciled");
}
void contract() {
    const auto bytes = xmlProportionalFixture();
    const auto source = importProjectXml(bytes, "multi.xml");
    const auto original = xmlProportionalTarget();
    const auto o = xmlProportionalOptions();
    const auto mappings = xmlProportionalMappings();
    const XmlAllocationPolicy working{XmlAllocationBasis::WorkingIntersectionMinutes, true},
        elapsed{XmlAllocationBasis::ElapsedMinutes, true};
    check(source.actualIntervals.size() == 4 &&
              std::all_of(source.actualIntervals.begin(), source.actualIntervals.end(),
                          [](const auto &r) { return r.multipleDays && r.issue.empty(); }) &&
              source.project.periodActuals.empty(),
          "raw intervals retained without default allocation");
    rejects([&] { prepareXmlPeriodMerge(original, source, o, mappings); }, "multi-day silently adopted by daily route");
    rejects([&] { prepareXmlActualAllocation(original, source, o, mappings, {}); },
            "range totals acknowledgement omitted");
    const auto projection = prepareXmlActualAllocation(original, source, o, mappings, working);
    check(projection.batches.size() == 3 && projection.report["days"].toArray().size() == 3 &&
              original == xmlProportionalTarget(),
          "working weights produce three immutable date proposals");
    check(confirmXmlActualAllocation(original, source, o, mappings, working, projection).size() == 3,
          "exact preview replay");
    const auto civil = prepareXmlActualAllocation(original, source, o, mappings, elapsed);
    check(civil.batches.size() == 6, "elapsed minutes include partial edges and weekends");
    const auto adopt = [&](Project p, const XmlActualAllocation &v) {
        int next = 0;
        for (const auto &s : v.batches)
            appendPeriodActuals(p, s, [&] { return "fixture-prorata-" + std::to_string(++next); });
        ++p.revision;
        validate(p);
        return p;
    };
    const auto civilProject = adopt(original, civil);
    check(day(civilProject, "NA", "NR1", "2026-11-13").workUnitMinutes == 18 &&
              day(civilProject, "NA", "NR1", "2026-11-16").workUnitMinutes == 19 &&
              day(civilProject, "NB", "NR2", "2026-11-16").actualMinor == 0,
          "elapsed proportional allocation and explicit derived zero");
    auto tampered = source;
    tampered.actualIntervals.front().workUnitMinutes = 999;
    rejects([&] { prepareXmlActualAllocation(original, tampered, o, mappings, working); },
            "raw interval tamper accepted");
    for (int kind = 0; kind < 3; ++kind) {
        auto p = projection;
        if (kind == 0)
            p.report["basis"] = "other";
        if (kind == 1)
            p.batches.front().entries.front().workUnitMinutes = 0;
        if (kind == 2)
            p.report["days"] = QJsonArray{};
        rejects([&] { confirmXmlActualAllocation(original, source, o, mappings, working, p); },
                "tampered preview accepted");
    }
    auto wrong = mappings;
    wrong.push_back(wrong.front());
    rejects([&] { prepareXmlActualAllocation(original, source, o, wrong, working); }, "duplicate interval selected");
    wrong = mappings;
    wrong[1].resourceId = "NR2";
    rejects([&] { prepareXmlActualAllocation(original, source, o, wrong, working); },
            "one source assignment mapped inconsistently");
    auto afterStatus = original;
    afterStatus.statusAt = "2026-11-16 13:00";
    auto earlier = o;
    earlier.statusAt = *afterStatus.statusAt;
    rejects([&] { prepareXmlActualAllocation(afterStatus, source, earlier, mappings, elapsed); },
            "timed range cropped at status");
    auto buckets = bytes;
    buckets.replace("2026-11-17T00:00:00", "2026-11-18T00:00:00");
    check(prepareXmlActualAllocation(original, importProjectXml(buckets, "buckets.xml"), o,
                                     {{"2/record-1", "NB", "NR2"}}, elapsed)
                  .batches.size() == 3,
          "whole-date buckets allow current date but no future date");
    buckets.replace("2026-11-18T00:00:00", "2026-11-19T00:00:00");
    rejects(
        [&] {
            prepareXmlActualAllocation(original, importProjectXml(buckets, "future.xml"), o,
                                       {{"2/record-1", "NB", "NR2"}}, elapsed);
        },
        "future date bucket accepted");
    auto zeroWeight = bytes;
    zeroWeight.replace("2026-11-15T00:00:00", "2026-11-14T00:00:00");
    zeroWeight.replace("2026-11-17T00:00:00", "2026-11-16T00:00:00");
    rejects(
        [&] {
            prepareXmlActualAllocation(original, importProjectXml(zeroWeight, "weekend.xml"), o,
                                       {{"2/record-1", "NB", "NR2"}}, working);
        },
        "zero-weight range allocated");
    auto overlap = bytes;
    injectAssignment(overlap, 1, xmlPeriodData(1, 2, "2026-11-16", "PT1H0M0S", 2, "2026-11-16T13:00:00"));
    const auto overlapping = importProjectXml(overlap, "overlap.xml");
    rejects([&] { prepareXmlActualAllocation(original, overlapping, o, mappings, working); },
            "overlap with unselected daily interval accepted");
    check(
        std::none_of(overlapping.periodCandidates.begin(), overlapping.periodCandidates.end(),
                     [](const auto &v) { return v.assignmentUid == "1" && v.date == "2026-11-16" && v.issue.empty(); }),
        "overlapping daily source cannot bypass range rejection");
    for (const QByteArray &replacement : {QByteArray("PT0H0M1S"), QByteArray("-PT1H"), QByteArray("PT999999999H")}) {
        auto bad = bytes;
        bad.replace("PT0H1M0.6S", replacement);
        rejects(
            [&] { prepareXmlActualAllocation(original, importProjectXml(bad, "invalid.xml"), o, mappings, working); },
            "inexact/negative/overflow work accepted");
    }
    auto low = bytes;
    low.replace("<ActualCost>15000</ActualCost>", "<ActualCost>100</ActualCost>");
    rejects([&] { prepareXmlActualAllocation(original, importProjectXml(low, "low.xml"), o, mappings, working); },
            "cumulative below all retained/allocated dates accepted");
    auto currency = original;
    currency.currency = "KRW";
    for (auto &e : currency.costEntries)
        e.currency = "KRW";
    for (auto &e : currency.periodActuals)
        e.currency = "KRW";
    rejects([&] { prepareXmlActualAllocation(currency, source, o, mappings, working); },
            "implicit currency conversion");
    auto krwXml = bytes;
    krwXml.replace("<CurrencyCode>USD</CurrencyCode>", "<CurrencyCode>KRW</CurrencyCode>");
    krwXml.replace("<ActualCost>15000</ActualCost>", "<ActualCost>1500000</ActualCost>");
    krwXml.replace("<ActualCost>10000</ActualCost>", "<ActualCost>1000000</ActualCost>");
    krwXml.replace("<Value>5001</Value>", "<Value>500100</Value>");
    krwXml.replace("<Value>1</Value>", "<Value>100</Value>");
    check(prepareXmlActualAllocation(currency, importProjectXml(krwXml, "krw.xml"), o, mappings, working)
                  .batches.size() == 3,
          "KRW wire amounts remain exact native integers");
    auto noCurves = original;
    noCurves.assignments.front().units = 50;
    noCurves.resources.front().rates.front().hourlyMinor = 1;
    check(prepareXmlActualAllocation(noCurves, source, o, mappings, working).report["ranges"] ==
              projection.report["ranges"],
          "weights do not infer rates or assignment units");
    auto holidays = original;
    holidays.calendars.front().holidays = {"2026-11-16"};
    check(prepareXmlActualAllocation(holidays, source, o, {{"1/record-1", "NA", "NR1"}}, working).batches.size() == 1,
          "native holidays change working weights");
    auto clean = original;
    clean.periodActuals.clear();
    clean.costEntries.clear();
    auto maxXml = bytes;
    maxXml.replace("PT0H1M0.6S", curveWorkText(maxWork).toUtf8());
    maxXml.replace("<Value>5001</Value>", "<Value>9000000000000</Value>");
    maxXml.replace("<ActualCost>15000</ActualCost>", "<ActualCost>9000000000000</ActualCost>");
    const auto maxAllocation =
        prepareXmlActualAllocation(clean, importProjectXml(maxXml, "max.xml"), o,
                                   {{"1/record-1", "NA", "NR1"}, {"1/record-2", "NA", "NR1"}}, elapsed);
    std::int64_t totalWork = 0, totalCost = 0;
    for (const auto &s : maxAllocation.batches) {
        totalWork += s.entries.front().workUnitMinutes.value();
        totalCost += s.entries.front().actualMinor.value();
    }
    check(totalWork == maxWork && totalCost == maxMoney, "overflow-safe apportionment at both native limits");
    const auto workOnly = prepareXmlActualAllocation(original, source, o, {{"1/record-1", "NA", "NR1"}}, working);
    const auto preserved = adopt(original, workOnly);
    check(day(preserved, "NA", "NR1", "2026-11-13").actualMinor == 500 &&
              !proportionalCost(day(preserved, "NA", "NR1", "2026-11-13")) &&
              costAsOf(preserved, "NA", *preserved.statusAt)->actualMinor == 20000,
          "missing measures and native cumulative preserved");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document d(store);
    d.open(path);
    auto proposal = prepared(d, projection.batches);
    check(d.applyPreparedEdit(*proposal) == EditState::Applied, "single-revision allocation batch");
    const auto merged = *d.project();
    verify(merged);
    check(merged.revision == 6 && merged.periodActuals.size() == 7 && merged.costEntries.size() == 5 &&
              std::equal(original.periodActuals.begin(), original.periodActuals.end(), merged.periodActuals.begin()) &&
              static_cast<const PlanInput &>(merged) == static_cast<const PlanInput &>(original) &&
              store.load(path).project == original,
          "plans/future/history/original file preserved");
    check(prepareXmlActualAllocation(merged, source, o, mappings, working).batches.empty(),
          "same allocation idempotent");
    rejects([&] { prepareXmlActualAllocation(merged, source, o, mappings, elapsed); },
            "changed basis silently rewrote prior estimate");
    auto changedCalendar = merged;
    changedCalendar.tasks.front().breaks.clear();
    rejects([&] { prepareXmlActualAllocation(changedCalendar, source, o, mappings, working); },
            "changed working weights silently rewrote prior estimate");
    PeriodActualSubmission correction;
    correction.reporter = "PM";
    correction.reason = "후속 실측 정정";
    correction.recordedAt = o.recordedAt;
    auto e = day(merged, "NA", "NR1", "2026-11-13");
    e.workUnitMinutes = 80;
    correction.entries = {e};
    correction.totals = {{"NA", 15000}};
    d.recordPeriodActuals(correction);
    const auto corrected = *d.project();
    check(prepareXmlActualAllocation(corrected, source, o, mappings, working).batches.empty() &&
              day(corrected, "NA", "NR1", "2026-11-13").workUnitMinutes == 80,
          "retry preserves subsequent measured correction");
    const auto provenance = actualDerivations(corrected);
    const auto &correctedDay = day(corrected, "NA", "NR1", "2026-11-13");
    check(provenance.at(correctedDay.id).workSourceId.empty() &&
              provenance.at(correctedDay.id).costSourceId == day(merged, "NA", "NR1", "2026-11-13").id,
          "unchanged cost retains estimate origin when work is corrected");
    const auto correctedExport = exportProjectXmlActuals(corrected);
    bool correctedOrigin = false;
    for (const auto &v : correctedExport.report["actualExport"].toObject()["assignmentDays"].toArray()) {
        const auto r = v.toObject();
        if (r["entryId"].toString().toStdString() == correctedDay.id)
            correctedOrigin =
                !r["proportionalWork"].toBool() && r["proportionalCost"].toBool() &&
                r["workAllocationEntryId"].toString().isEmpty() &&
                r["costAllocationEntryId"].toString().toStdString() == day(merged, "NA", "NR1", "2026-11-13").id;
    }
    check(correctedOrigin, "XML receipt retains unchanged cost's estimate origin after work correction");
    auto remapped = mappings;
    for (auto &m : remapped)
        if (m.taskId == "NA")
            m.taskId = "renamed-A";
    const auto migrated = migrateLedgerIdentities(corrected, {{"task", "NA", "renamed-A"}});
    check(prepareXmlActualAllocation(migrated, source, o, remapped, working).batches.empty(),
          "stable source group survives native ID migration");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto cancelled = prepared(d, projection.batches, control);
    check(d.applyPreparedEdit(*cancelled) == EditState::Cancelled && *d.project() == corrected,
          "cancelled batch unchanged");
    auto stale = prepared(d, projection.batches);
    d.renameTask(0, "보존할 편집");
    const auto edited = *d.project();
    check(d.applyPreparedEdit(*stale) == EditState::Stale && *d.project() == edited, "stale batch unchanged");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot(o.recordedAt));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "allocation recovery");
    d.save();
    check(store.load(path).project == *d.project() && nativeSchemaFor(merged) == 11, "existing schema11 roundtrip");
    const auto audit = tmp.filePath("audit");
    publishXmlActualAllocation(original, source, o, mappings, working, projection, audit);
    check(store.load((audit + "/source.ppm").toStdString()).project == original &&
              readProjectXml(audit + "/original.xml") == bytes,
          "audit preserves source XML and pre-adoption native");
    rejects([&] { publishXmlActualAllocation(original, source, o, mappings, working, projection, audit); },
            "audit overwrote existing folder");
    auto tamperedPreview = projection;
    tamperedPreview.report["basis"] = "other";
    rejects(
        [&] {
            publishXmlActualAllocation(original, source, o, mappings, working, tamperedPreview, tmp.filePath("bad"));
        },
        "tampered audit published");
    check(!QFileInfo::exists(tmp.filePath("bad")), "failed audit leaves no folder");
    bool publicationCancelled = false;
    try {
        EvaluationScope scope(control);
        publishXmlActualAllocation(original, source, o, mappings, working, projection, tmp.filePath("cancelled"));
    } catch (const CalculationCancelled &) {
        publicationCancelled = true;
    }
    check(publicationCancelled && !QFileInfo::exists(tmp.filePath("cancelled")), "cancelled audit leaves no folder");
    const auto lockPath = tmp.filePath("locked.ppm").toStdString();
    store.save(lockPath, original, {});
    Document locked(store);
    locked.open(lockPath);
    locked.captureBaseline("승인", "PM", "마감", o.recordedAt);
    FinancialPeriodSubmission close;
    close.period.name = "마감 기간";
    close.period.baselineId = locked.project()->baselines.front().id;
    close.period.fromDate = "2026-11-13";
    close.period.toDate = "2026-11-16";
    close.reporter = "PM";
    close.reason = "마감";
    close.recordedAt = o.recordedAt;
    close.confirmed = true;
    const auto pid = locked.recordFinancialPeriod(close);
    close.period = financialPeriod(*locked.project(), pid).period;
    close.action = "close";
    locked.recordFinancialPeriod(close);
    rejects([&] { prepareXmlActualAllocation(*locked.project(), source, o, mappings, working); },
            "closed period accepted during preview");
    const auto exported = exportProjectXmlActuals(merged);
    const auto exportedDays = exported.report["actualExport"].toObject()["assignmentDays"].toArray();
    check(std::count_if(exportedDays.begin(), exportedDays.end(),
                        [](const auto &v) { return v.toObject()["proportionalWork"].toBool(); }) == 3,
          "standard XML receipt distinguishes estimates");
    const auto qa = QDir::currentPath() + "/xml-proportional-qa";
    QDir().mkpath(qa);
    save(qa + "/original.xml", bytes);
    save(qa + "/before.json", QJsonDocument(json::encodeProject(original)).toJson());
    save(qa + "/working.json", QJsonDocument(projection.report).toJson());
    save(qa + "/elapsed.json", QJsonDocument(civil.report).toJson());
    save(qa + "/merged.json", QJsonDocument(json::encodeProject(merged)).toJson());
    save(qa + "/corrected.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    save(qa + "/project.xml", exported.xml);
    save(qa + "/conversion.json", QJsonDocument(exported.report).toJson());
    save(qa + "/corrected-conversion.json", QJsonDocument(correctedExport.report).toJson());
    QFile::remove(qa + "/source.ppm");
    store.save((qa + "/source.ppm").toStdString(), merged, {});
    QJsonArray cases;
    for (const auto quantity : {0LL, 1LL, 2LL, 3LL, 99LL, 100LL, 101LL, 10001LL})
        for (const auto policy : {working, elapsed}) {
            auto xml = bytes;
            xml.replace("PT0H1M0.6S", curveWorkText(quantity).toUtf8());
            auto result = prepareXmlActualAllocation(original, importProjectXml(xml, "case.xml"), o,
                                                     {{"1/record-1", "NA", "NR1"}}, policy);
            cases.append(result.report);
        }
    save(qa + "/cases.json", QJsonDocument(cases).toJson());
    ReportOptions ro;
    ro.reportId = "xml-proportional";
    ro.generatedAt = o.recordedAt;
    const auto report = buildReport(merged, ro);
    check(std::any_of(report.tables.begin(), report.tables.end(),
                      [](const auto &t) { return t.id == "periodActualAllocation" && t.rows.size() == 3; }),
          "estimated provenance report present");
    for (const auto *name : {"audit.xlsx", "audit.pdf"})
        QFile::remove(qa + '/' + name);
    exportXlsx(report, qa + "/audit.xlsx");
    exportPdf(report, qa + "/audit.pdf");
    std::cout
        << "PASS multi-day allocation: explicit total/basis consent, exact elapsed/working weights and largest "
           "remainders, limits/zeros/unknowns/units/currency, overlap/future/frozen rejection, preview replay/audit, "
           "one-revision provenance, retry/corrections/ID migration/recovery/schema11/reports/XML estimate flags\n";
}
void ui() {
    const auto source = importProjectXml(xmlProportionalFixture(), "multi.xml");
    const auto original = xmlProportionalTarget();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, original, {});
    Document d(store);
    d.open(path);
    MainWindow window(d);
    window.show();
    bool checked = false;
    QTimer driver;
    driver.setInterval(20);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlAllocationDialog")
            return;
        driver.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlAllocationRanges");
        check(table && table->rowCount() == 4, "multi-day source rows");
        for (int i = 0; i < 4; ++i) {
            check(table->item(i, 0)->checkState() == Qt::Unchecked, "range not selected automatically");
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(i, 5));
            check(combo->currentData().toInt() == -1, "no guessed target assignment");
            combo->setCurrentIndex(i < 2 ? 1 : 2);
            table->item(i, 0)->setCheckState(Qt::Checked);
        }
        auto *basis = dialog->findChild<QComboBox *>("xmlAllocationBasis");
        check(basis->currentData().toInt() == -1, "basis must be selected");
        basis->setCurrentIndex(2);
        auto *consent = dialog->findChild<QCheckBox *>("xmlAllocationConfirm");
        check(!consent->isChecked(), "total/estimate consent not prechecked");
        dialog->findChild<QLineEdit *>("xmlAllocationReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlAllocationReason")->setText("구간 총량/추정 검토");
        auto *preview = dialog->findChild<QPushButton *>("xmlAllocationPreview");
        preview->click();
        check(!dialog->findChild<QLabel *>("xmlAllocationError")->text().isEmpty() &&
                  !dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
              "missing total acknowledgement blocked");
        consent->setChecked(true);
        preview->click();
        auto *daily = dialog->findChild<QTableWidget *>("xmlAllocationDays");
        check(daily->rowCount() == 3 && daily->item(0, 3)->text() == "51u" &&
                  daily->item(0, 7)->text().contains("추정"),
              "integer daily preview and estimate classification");
        dialog->findChild<QLineEdit *>("xmlAllocationReason")->setText("사유 수정 후 재검토");
        check(daily->rowCount() == 0 &&
                  !dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
              "editing invalidates preview");
        preview->click();
        dialog->findChild<QLineEdit *>("xmlAllocationDestination")->setText(tmp.filePath("review"));
        dialog->findChild<QPushButton *>("xmlAllocationExport")->click();
        check(QFileInfo::exists(tmp.filePath("review/allocation.json")) && store.load(path).project == original,
              "audit saved before adoption without modifying native");
        dialog->resize(800, 850);
        QDir().mkpath(QDir::currentPath() + "/xml-proportional-qa");
        dialog->grab().save(QDir::currentPath() + "/xml-proportional-qa/allocation-small.png");
        daily->horizontalScrollBar()->setValue(daily->horizontalScrollBar()->maximum());
        dialog->grab().save(QDir::currentPath() + "/xml-proportional-qa/allocation-values-small.png");
        checked = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QTimer::singleShot(20, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto *button = dialog->findChild<QPushButton *>("xmlPeriodAllocate");
            check(button && button->isEnabled(), "allocation entry from existing XML daily review");
            button->click();
        }
    });
    driver.start();
    check(reviewXmlPeriodActuals(&window, d, source) && checked, "allocation UI applies through original entry");
    verify(*d.project());
    const auto merged = *d.project();
    check(store.load(path).project == original, "original native file unchanged");
    QTimer::singleShot(30, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    check(!reviewXmlActualAllocation(&window, d, source) && *d.project() == merged,
          "cancel allocation preserves native");
    QTimer staleDriver;
    staleDriver.setInterval(20);
    bool staleChecked = false;
    QObject::connect(&staleDriver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlAllocationDialog")
            return;
        staleDriver.stop();
        auto *table = dialog->findChild<QTableWidget *>("xmlAllocationRanges");
        for (int i = 0; i < 4; ++i) {
            qobject_cast<QComboBox *>(table->cellWidget(i, 5))->setCurrentIndex(i < 2 ? 1 : 2);
            table->item(i, 0)->setCheckState(Qt::Checked);
        }
        dialog->findChild<QComboBox *>("xmlAllocationBasis")->setCurrentIndex(2);
        dialog->findChild<QCheckBox *>("xmlAllocationConfirm")->setChecked(true);
        dialog->findChild<QLineEdit *>("xmlAllocationReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlAllocationReason")->setText("이미 반영된 구간 재검토");
        dialog->findChild<QPushButton *>("xmlAllocationPreview")->click();
        check(dialog->findChild<QLabel *>("xmlAllocationSummary")->text().contains("신규 보고 0건"),
              "retry preview identifies no-op");
        d.renameTask(0, "미리보기 후 편집");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        check(!dialog->findChild<QLabel *>("xmlAllocationError")->text().isEmpty(), "stale preview rejected");
        staleChecked = true;
        dialog->reject();
    });
    staleDriver.start();
    check(!reviewXmlActualAllocation(&window, d, source) && staleChecked &&
              d.project()->tasks.front().name == "미리보기 후 편집" &&
              d.project()->periodActuals == merged.periodActuals,
          "stale dialog preserves intervening edits and all facts");
    {
        Document readonly(store);
        readonly.open(path, true);
        rejects([&] { reviewXmlActualAllocation(&window, readonly, source); }, "read-only allocation UI accepted");
    }
    std::cout << "PASS multi-day UI: explicit interval/target/basis/total consent, replayed daily preview, "
                 "invalidation, create-only audit, native apply/cancel/stale, 800px review\n";
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
