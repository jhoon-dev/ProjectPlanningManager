#include "ExternalEvmFixture.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/JsonRecoveryStore.h"
#include "domain/Classification.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <algorithm>
#include <future>
#include <iostream>
using namespace jobs;
namespace {
void check(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected external EVM rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
QJsonObject resultJson(const ProjectReport &r) {
    QJsonArray tables;
    for (const auto &t : r.tables) {
        QJsonArray rows, columns;
        for (const auto &c : t.columns)
            columns.append(QString::fromStdString(c.title));
        for (const auto &row : t.rows) {
            QJsonArray cells;
            for (const auto &c : row) {
                if (const auto *n = std::get_if<std::int64_t>(&c.value))
                    cells.append(qint64(*n));
                else if (const auto *n = std::get_if<double>(&c.value))
                    cells.append(*n);
                else if (const auto *s = std::get_if<std::string>(&c.value))
                    cells.append(QString::fromStdString(*s));
                else
                    cells.append(QJsonValue::Null);
            }
            rows.append(cells);
        }
        tables.append(QJsonObject{{"id", QString::fromStdString(t.id)}, {"columns", columns}, {"rows", rows}});
    }
    return {{"tables", tables}};
}
std::shared_ptr<Document::PreparedEdit>
prepare(Document &d, ExternalEvmSubmission s,
        std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    auto request = d.makeEditRequest(ExternalEvmEdit{std::move(s)});
    return std::async(std::launch::async, [request = std::move(request),
                                           c]() mutable { return Document::prepareEdit(std::move(request), c); })
        .get();
}
void sourceSame(const Project &a, const Project &b) {
    auto x = a, y = b;
    x.externalEvmAdoptions.clear();
    y.externalEvmAdoptions.clear();
    x.revision = y.revision = 1;
    check(x == y, "adoption changed native inputs/journals/original source");
}
void contracts() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = (temp.path() + "/source.ppm").toStdString();
    const auto p = external_evm_fixture::make(store, path);
    Document d(store);
    d.open(path);
    const auto baseline = p.baselines.back().id;
    auto native = analyzeCosts(p, baseline);
    check(native.evm.bac == 10000 && native.evm.ev == 3725 && native.evm.ac == 12280, "native fixture budget/actual");
    const auto originalHash = xerFinancialProjectionHash(p);
    auto s = external_evm_fixture::submission(p);
    check(s.rows.size() == 2, "source EV periods");
    auto edit = prepare(d, s);
    check(edit->state() == EditState::Ready && *d.project() == p, "preview changed source or failed");
    check(d.applyPreparedEdit(*edit) == EditState::Applied && !d.canUndo() && d.dirty(),
          "adoption not immutable/dirty");
    sourceSame(p, *d.project());
    check(xerFinancialProjectionHash(*d.project()) == originalHash, "adoption invalidated XER financial projection");
    auto adopted = analyzeCosts(*d.project(), baseline);
    check(adopted.evm.bac == 10000 && adopted.evm.ev == 2000 && adopted.evm.pv == 2500 && adopted.evm.ac == 12280 &&
              adopted.evm.cv == -10280 && adopted.evm.sv == -500 && adopted.evm.eac == 20280 && adopted.evm.etc == 8000,
          "adopted cumulative values");
    check(adopted.evm.spi == 0.8 && std::abs(*adopted.evm.cpi - 2000.0 / 12280) < 1e-12, "adopted ratios");
    const auto id = d.project()->externalEvmAdoptions.back().id;
    auto curve = nativeEvmCurves(*d.project(), baseline, "budget-remaining-v1",
                                 {{"2026-10-19 00:00", false, true},
                                  {"2026-10-20 12:00", true, true},
                                  {"2026-10-27 00:00", false, true},
                                  {"2026-10-28 00:00", false, true},
                                  {"2026-10-28 18:00", true, true}},
                                 std::vector<std::string>{"xer-task-20"});
    check(curve[0].points[0].value.ev == 0 && !curve[0].points[1].value.ev && !curve[0].points[1].value.pv &&
              curve[0].points[2].value.ev == 1200 && curve[0].points[3].value.ev == 1200 &&
              curve[0].points[4].value.ev == 2000 && curve[0].points[4].evReportId == id,
          "known boundaries/interior/explicit gap anchors");
    auto o = external_evm_fixture::options(*d.project());
    auto report = buildPeriodEvmReport(*d.project(), o, "month");
    validateReportExport(report);
    const auto &row = report.tables[0].rows[0];
    check(std::get<std::int64_t>(row[8].value) == 2500 && std::get<std::int64_t>(row[11].value) == 2000 &&
              std::get<std::int64_t>(row[14].value) == 12280 && report.tables.size() == 6,
          "native period report/audit");
    auto daily = buildPeriodEvmReport(*d.project(), o, "day");
    check(std::holds_alternative<std::monostate>(daily.tables[0].rows[1][11].value) &&
              std::get<std::int64_t>(daily.tables[0].rows.back()[11].value) == 800,
          "daily source not prorated");
    auto generic = buildReport(*d.project(), o);
    validateReportExport(generic);
    check(std::any_of(generic.tables.begin(), generic.tables.end(),
                      [](const auto &t) { return t.id == "externalEvmPeriods"; }),
          "generic report missing audit");
    // Old native ledger/first archive and snapshots survive persistence/copies/recovery and later plan undo.
    const auto body = json::encodeProject(*d.project());
    check(nativeSchemaFor(*d.project()) == 29 && json::decodeProject(body, 29) == *d.project(), "schema29 roundtrip");
    rejects([&] { json::decodeProject(body, 28); });
    for (int bad = 0; bad < 10; ++bad) {
        auto v = body;
        auto entries = v["externalEvmAdoptions"].toArray();
        auto a = entries[0].toObject();
        if (bad == 0)
            a["sourceSha256"] = QString(64, 'a');
        if (bad == 1)
            a["financialRevisionId"] = "missing";
        if (bad == 2)
            a["confirmed"] = false;
        if (bad == 3)
            a["recordedRevision"] = 0;
        if (bad == 4)
            a["useEv"] = "yes";
        if (bad == 5)
            a["baselineId"] = "missing";
        if (bad == 6)
            a["policy"] = "future";
        if (bad == 7)
            a["nativeStatusAt"] = "2026-10-01 00:00";
        if (bad >= 8) {
            auto rows = a["rows"].toArray();
            auto r = rows[0].toObject();
            r[bad == 8 ? "periodIndex" : "openingEv"] = bad == 8 ? QJsonValue(99999) : QJsonValue(-1);
            rows[0] = r;
            a["rows"] = rows;
        }
        entries[0] = a;
        v["externalEvmAdoptions"] = entries;
        rejects([&] { json::decodeProject(v, 29); });
    }
    auto finalPath = (temp.path() + "/adopted.ppm").toStdString();
    d.saveAs(finalPath);
    check(store.load(finalPath).project == *d.project(), "save/reopen");
    Document copy(store);
    copy.open(finalPath, true);
    copy.saveAs((temp.path() + "/copy.ppm").toStdString());
    check(copy.project()->externalEvmAdoptions == d.project()->externalEvmAdoptions &&
              analyzeCosts(*copy.project(), baseline).evm.ev == 2000,
          "independent copy lost adoption");
    d.renameTask(0, "회복 이름");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-08T05:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema29 recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document recovered(store);
    recovered.recover(claim->snapshot);
    check(recovered.project()->externalEvmAdoptions == d.project()->externalEvmAdoptions &&
              analyzeCosts(*recovered.project(), baseline).evm.pv == 2500,
          "recovery values");
    d.undo();
    check(d.project()->externalEvmAdoptions.size() == 1 && analyzeCosts(*d.project(), baseline).evm.ev == 2000,
          "plan undo erased adoption");
    QDir().mkpath("external-xer-evm-qa");
    for (const auto &name : {"adopted.ppm", "report.xlsx", "report.pdf", "generic.xlsx"})
        QFile::remove(QString("external-xer-evm-qa/") + name);
    write("external-xer-evm-qa/source.xer", external_evm_fixture::bytes());
    write("external-xer-evm-qa/before.json", QJsonDocument(json::encodeProject(p)).toJson());
    write("external-xer-evm-qa/adopted.json", QJsonDocument(body).toJson());
    write("external-xer-evm-qa/monthly.json", QJsonDocument(resultJson(report)).toJson());
    write("external-xer-evm-qa/daily.json", QJsonDocument(resultJson(daily)).toJson());
    write("external-xer-evm-qa/weekly.json",
          QJsonDocument(resultJson(buildPeriodEvmReport(json::decodeProject(body, 29), o, "week"))).toJson());
    store.save("external-xer-evm-qa/adopted.ppm", json::decodeProject(body, 29), {});
    exportXlsx(report, "external-xer-evm-qa/report.xlsx", ReportFamily::Costs);
    exportXlsx(generic, "external-xer-evm-qa/generic.xlsx", ReportFamily::Costs);
    PdfOptions pdf;
    pdf.a3 = true;
    pdf.font = QApplication::font();
    check(exportPdf(report, "external-xer-evm-qa/report.pdf", ReportFamily::Costs, pdf) > 0, "PDF export");
    // Re-adoption replaces a task's complete choice; unselected PV returns to the native curve.
    auto correction = external_evm_fixture::submission(*d.project());
    correction.usePv = false;
    correction.rows = {correction.rows.back()};
    correction.rows[0].openingEv = 10;
    correction.rows[0].openingPv.reset();
    d.recordExternalEvm(correction);
    auto corrected = analyzeCosts(*d.project(), baseline);
    check(corrected.evm.ev == 810 && corrected.evm.pv == native.evm.pv && corrected.evm.ac == 12280,
          "EV-only replacement");
    auto withdraw = externalEvmDraft(*d.project(), baseline);
    withdraw.taskIds = {"xer-task-20"};
    withdraw.withdrawn = true;
    withdraw.useEv = withdraw.usePv = false;
    withdraw.confirmed = true;
    withdraw.reporter = "PM";
    withdraw.reason = "native 복원";
    withdraw.recordedAt = "2026-11-08T06:00:00Z";
    d.recordExternalEvm(withdraw);
    auto restored = analyzeCosts(*d.project(), baseline);
    check(restored.evm.ev == native.evm.ev && restored.evm.pv == native.evm.pv && restored.evm.ac == native.evm.ac &&
              d.project()->externalEvmAdoptions.size() == 3,
          "withdraw did not restore native");
    rejects([&] { d.recordExternalEvm(withdraw); });
    rejects([&] { d.setCurrency("KRW"); });
    // Data and authorization guards.
    for (int kind = 0; kind < 9; ++kind) {
        auto bad = external_evm_fixture::submission(*d.project());
        if (kind == 0)
            bad.confirmed = false;
        if (kind == 1)
            bad.rows[0].openingEv.reset();
        if (kind == 2)
            bad.taskIds.push_back("xer-task-20");
        if (kind == 3)
            bad.rows.push_back(bad.rows[0]);
        if (kind == 4)
            bad.rows[0].periodIndex = 99999;
        if (kind == 5)
            bad.sourceProjectionSha256 = std::string(64, 'f');
        if (kind == 6)
            bad.rows[0].openingEv = maxMoney;
        if (kind == 7)
            bad.baselineId = "missing";
        if (kind == 8)
            bad.taskIds = {"missing"};
        const auto before = *d.project();
        rejects([&] { d.recordExternalEvm(bad); });
        check(*d.project() == before, "invalid proposal changed source");
    }
    auto pending = prepare(d, external_evm_fixture::submission(*d.project()));
    d.renameTask(0, "stale");
    check(d.applyPreparedEdit(*pending) == EditState::Stale, "stale worker applied");
    pending = prepare(d, external_evm_fixture::submission(*d.project()));
    d.setWriteAuthorization([] { return true; });
    check(d.applyPreparedEdit(*pending) == EditState::Stale, "session worker applied");
    bool allowed = true;
    d.setWriteAuthorization([&] { return allowed; });
    pending = prepare(d, external_evm_fixture::submission(*d.project()));
    allowed = false;
    check(d.applyPreparedEdit(*pending) == EditState::Stale, "revoked worker applied");
    allowed = true;
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    check(prepare(d, external_evm_fixture::submission(*d.project()), control)->state() == EditState::Cancelled,
          "cancel ignored");
    Document readonly(store);
    readonly.open(finalPath, true);
    rejects([&] { readonly.recordExternalEvm(external_evm_fixture::submission(*readonly.project())); });
}
void policiesAndSourceVersions() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = (temp.path() + "/source.ppm").toStdString();
    auto initial = external_evm_fixture::make(store, path);
    Document d(store);
    d.open(path);
    const auto first = initial.baselines.back().id;
    d.recordExternalEvm(external_evm_fixture::submission(*d.project()));
    d.setTaskCost({"xer-task-20", 1000, "uniform", "physical"});
    d.setTaskCost({"xer-task-30", 20000, "uniform", "physical"});
    const auto second = d.captureBaseline("second", "PM", "different basis", "2026-11-08T05:00:00Z");
    check(analyzeCosts(*d.project(), second).evm.ev == 373, "adoption leaked to a different baseline");
    d.recordExternalEvm(external_evm_fixture::submission(*d.project()));
    auto mismatch = analyzeCosts(*d.project(), second);
    check(mismatch.evm.bac == 21000 && mismatch.evm.ev == 2000 && !mismatch.evm.eac && !mismatch.evm.etc,
          "task BAC mismatch was masked by aggregate BAC");
    auto report = buildPeriodEvmReport(*d.project(), external_evm_fixture::options(*d.project()));
    check(std::holds_alternative<std::monostate>(report.tables[0].rows[0][19].value), "period aggregate EAC mismatch");
    check(analyzeCosts(*d.project(), first).evm.ev == 2000, "new baseline revalued first adoption");
    auto pv = external_evm_fixture::submission(*d.project());
    pv.useEv = false;
    for (auto &r : pv.rows)
        r.openingEv.reset();
    d.recordExternalEvm(pv);
    check(analyzeCosts(*d.project(), second).evm.ev == 373 && analyzeCosts(*d.project(), second).evm.pv == 22500,
          "PV-only selection");
    auto variant = initial;
    for (auto i = variant.xerFinancials->periods.rbegin(); i != variant.xerFinancials->periods.rend(); ++i)
        if (i->bcws) {
            i->bcws.reset();
            break;
        }
    auto s = external_evm_fixture::submission(variant);
    auto adoption = prepareExternalEvmAdoption(variant, s, "missing-pv");
    variant.externalEvmAdoptions.push_back(adoption);
    ++variant.revision;
    auto missing = analyzeCosts(variant, first);
    check(missing.evm.ev == 2000 && !missing.evm.pv && missing.evm.ac == 12280,
          "missing external PV filled with native");
    variant = initial;
    for (auto i = variant.xerFinancials->periods.rbegin(); i != variant.xerFinancials->periods.rend(); ++i)
        if (i->bcws) {
            i->bcws = 0;
            break;
        }
    adoption = prepareExternalEvmAdoption(variant, external_evm_fixture::submission(variant), "zero-pv");
    variant.externalEvmAdoptions.push_back(adoption);
    ++variant.revision;
    check(analyzeCosts(variant, first).evm.pv == 1000, "external explicit zero treated as missing");
    variant = initial;
    for (auto &r : variant.xerFinancials->periods)
        if (r.resourceId.empty() && r.bcwp && r.start.substr(0, 10) == "2026-10-19")
            r.finish = "2026-10-27 23:59";
    s = external_evm_fixture::submission(variant);
    s.rows.back().openingEv = 1300;
    rejects([&] { prepareExternalEvmAdoption(variant, s, "boundary-conflict"); });
    auto beforeClose = *d.project();
    beforeClose.statusAt = "2026-10-28 17:59";
    check(!analyzeCosts(beforeClose, second).evm.pv, "source close leaked into interior minute");
    d.setStatusAt("2026-10-27 18:00");
    rejects([&] { d.recordExternalEvm(external_evm_fixture::submission(*d.project())); });
    auto w = externalEvmDraft(*d.project(), second);
    w.taskIds = {"xer-task-20"};
    w.withdrawn = true;
    w.useEv = w.usePv = false;
    w.confirmed = true;
    w.reporter = "PM";
    w.reason = "restore at earlier native status";
    w.recordedAt = "2026-11-08T06:00:00Z";
    d.recordExternalEvm(w);
    check(d.project()->externalEvmAdoptions.back().withdrawn, "withdrawal at prior status");
    d.setStatusAt("2026-10-28 18:00");
    const auto facts = *d.project();
    d.recordXerFinancialMerge({{}, "PM", "after EV adoption", "2026-11-08T06:00:00Z"}, xerFinancialProjectionHash);
    check(analyzeCosts(*d.project(), first).evm.ev == 2000, "financial merge lost historical adoption reference");
    auto latest = external_evm_fixture::submission(*d.project());
    latest.baselineId = first;
    d.recordExternalEvm(latest);
    XerRelinkAudit audit;
    audit.incoming = *activeXerFinancials(*d.project());
    audit.incoming.sourceName = "later-source.xer";
    audit.incoming.sourceSha256 = std::string(64, 'c');
    for (auto i = audit.incoming.periods.rbegin(); i != audit.incoming.periods.rend(); ++i)
        if (i->bcwp) {
            *i->bcwp += 100;
            break;
        }
    audit.projectName = "later source";
    audit.sourceStatusAt = audit.incoming.statusAt;
    audit.confirmed = true;
    audit.incomingLinks = suggestXerLinks(audit.incoming, *d.project());
    audit.previousLinks = suggestXerLinks(*activeXerFinancials(*d.project()), *d.project());
    d.recordXerRelink({audit, {{}, "PM", "new source after adoption", "2026-11-08T06:00:00Z"}},
                      xerFinancialProjectionHash);
    check(analyzeCosts(*d.project(), first).evm.ev == 2000, "new XER silently changed adopted EV");
    latest = external_evm_fixture::submission(*d.project());
    latest.baselineId = first;
    d.recordExternalEvm(latest);
    check(analyzeCosts(*d.project(), first).evm.ev == 2100 && d.project()->costEntries == facts.costEntries &&
              d.project()->periodActuals == facts.periodActuals,
          "explicit source re-adoption or native journal preservation");
    auto body = json::encodeProject(*d.project());
    check(json::decodeProject(body, 29) == *d.project(), "mixed XER/adoption history roundtrip");
    variant = initial;
    variant.progress.clear();
    variant.costEntries.clear();
    variant.periodActuals.clear();
    variant.stepReports.clear();
    adoption =
        prepareExternalEvmAdoption(variant, external_evm_fixture::submission(variant), "guard-without-native-facts");
    variant.externalEvmAdoptions.push_back(adoption);
    ++variant.revision;
    check(Document::previewRemoveTasks(variant, {"xer-task-20"}).protectedTaskIds ==
              std::vector<std::string>{"xer-task-20"},
          "adoption-only task deletion not protected");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        contracts();
        policiesAndSourceVersions();
        std::cout << "PASS: XER EV/PV native adoption/withdrawal, explicit anchors and gaps, cumulative/period/report "
                     "parity, original/native facts unchanged, schema29/strict corruption/copy/recovery/undo, "
                     "invalid/stale/session/permission/cancel and XLSX/PDF\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
