#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/XerFinancialMerge.h"
#include "domain/Classification.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "ui/XerFinancialMergeDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <set>
using namespace ppm;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "expected rejection");
}
void write(const QString &path, const QByteArray &b) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write QA artifact");
}
QByteArray envelope(const QJsonObject &o, int version) {
    return QJsonDocument(QJsonObject{{"format", "PPM"},
                                     {"schemaVersion", version},
                                     {"payload", o},
                                     {"sha256", QString::fromLatin1(QCryptographicHash::hash(
                                                                        QJsonDocument(o).toJson(QJsonDocument::Compact),
                                                                        QCryptographicHash::Sha256)
                                                                        .toHex())}})
        .toJson();
}
PeriodActualEntry row(const Project &p, const char *date, bool direct, std::optional<std::int64_t> work,
                      std::optional<std::int64_t> cost) {
    PeriodActualEntry e;
    e.taskId = p.tasks[0].id;
    e.resourceId = direct ? "" : p.resources[0].id;
    e.date = date;
    e.workUnitMinutes = work;
    e.actualMinor = cost;
    return e;
}
void report(Document &d, std::vector<PeriodActualEntry> entries, std::int64_t ac = 12280) {
    PeriodActualSubmission s{std::move(entries), {}, "현장 원장", "날짜별 관측 정정", "2026-11-07T07:00:00Z"};
    std::set<std::string> tasks;
    for (const auto &e : s.entries)
        tasks.insert(e.taskId);
    for (const auto &t : tasks)
        s.totals.push_back({t, ac});
    d.recordPeriodActuals(std::move(s));
}
XerFinancialMergeSubmission mergeSubmission(XerFinancialMergeOptions options = {}) {
    return {std::move(options), "병합 작성자", "원본과 현재 관측 원장 대조", "2026-11-07T08:00:00Z"};
}
std::shared_ptr<Document::PreparedEdit>
prepare(Document &d, XerFinancialMergeOptions options = {},
        std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(
        d.makeEditRequest(XerFinancialMergeEdit{mergeSubmission(std::move(options)), xerFinancialProjectionHash}),
        std::move(c));
}
void unchanged(const Project &a, const Project &b) {
    check(a.costEntries == b.costEntries && a.periodActuals == b.periodActuals && a.progress == b.progress &&
              a.baselines == b.baselines && a.xerFinancials == b.xerFinancials && a.statusAt == b.statusAt &&
              static_cast<const PlanInput &>(a) == static_cast<const PlanInput &>(b),
          "merge changed native facts/input/source");
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "merge UI timeout");
}
void ui(Document &doc, bool conflict, bool small, bool commit) {
    const auto before = *doc.project();
    XerFinancialMergeDialog d(doc);
    d.show();
    QApplication::processEvents();
    if (small)
        d.resize(800, 600);
    auto *analyze = d.findChild<QPushButton *>("xerMergeAnalyze");
    auto *submit = d.findChild<QPushButton *>("xerMergeSubmit");
    auto *issues = d.findChild<QTableView *>("xerMergeIssues");
    auto *tabs = d.findChild<QTabWidget *>("xerMergeTabs");
    check(analyze && submit && issues && tabs && analyze->isEnabled() && !submit->isEnabled(), "merge UI controls");
    d.findChild<QLineEdit *>("xerMergeReporter")->setText("UI 검증자");
    d.findChild<QLineEdit *>("xerMergeReason")->setText("날짜 명세와 원본 집계의 병합");
    analyze->click();
    wait([&] { return analyze->isEnabled(); });
    if (conflict) {
        check(!submit->isEnabled(), "ambiguous period silently merged");
        check(issues->model()->data(issues->model()->index(0, 3)).toString().contains("12000u") &&
                  issues->model()->data(issues->model()->index(0, 4)).toString().contains("1200u"),
              "source/current review amounts absent");
        tabs->setCurrentIndex(0);
        d.grab().save("xer-merge-qa/conflict.png");
        bool selected = false;
        for (int i = 0; i < issues->model()->rowCount(); ++i) {
            auto index = issues->model()->index(i, 0);
            if (issues->model()->flags(index) & Qt::ItemIsUserCheckable) {
                check(issues->model()->setData(index, Qt::Checked, Qt::CheckStateRole), "resolve period checkbox");
                selected = true;
                break;
            }
        }
        check(selected, "period conflict not actionable");
        analyze->click();
        wait([&] { return analyze->isEnabled(); });
    }
    if (!submit->isEnabled())
        std::cerr << d.findChild<QLabel *>("xerMergeMessage")->text().toStdString() << '\n';
    check(submit->isEnabled() && *doc.project() == before, "preview failed or mutated document");
    d.findChild<QScrollArea *>("editorScrollArea")->verticalScrollBar()->setValue(0);
    d.grab().save(small      ? "xer-merge-qa/merge-small.png"
                  : conflict ? "xer-merge-qa/resolved.png"
                             : "xer-merge-qa/merge.png");
    tabs->setCurrentIndex(3);
    d.grab().save("xer-merge-qa/ledger.png");
    d.findChild<QLineEdit *>("xerMergeReason")->setText("확인한 병합 사유");
    check(!submit->isEnabled(), "edited preview still submit-able");
    analyze->click();
    wait([&] { return analyze->isEnabled(); });
    if (commit) {
        submit->click();
        check(!doc.project()->xerFinancialRevisions.empty() &&
                  doc.project()->xerFinancialRevisions.back().merge.has_value(),
              "UI merge not published");
        unchanged(before, *doc.project());
    } else {
        d.reject();
        check(*doc.project() == before, "close published staged merge");
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] { std::_Exit(3); });
    watchdog.start(150000);
    try {
        QDir().mkpath("xer-merge-qa");
        const auto bytes = readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE));
        const auto p = importPrimaveraXer(bytes, "financial-periods.xer").project;
        QTemporaryDir temp;
        JsonProjectStore store;
        int sequence = 0;
        auto document = [&](const Project &input) {
            const auto path = (temp.path() + QString("/project-%1.ppm").arg(++sequence)).toStdString();
            store.save(path, input, {});
            auto d = std::make_unique<Document>(store);
            d->open(path);
            return d;
        };
        auto d = document(p);
        report(*d, {row(p, "2026-10-27", false, 1800, 500), row(p, "2026-10-27", true, {}, 700)});
        d->renameTask(0, "병합된 설계");
        auto resource = d->project()->resources[0];
        resource.name = "현장 설계자";
        d->putResource(resource);
        d->setStatusAt("2026-10-29 18:00");
        const auto before = *d->project();
        const auto plan = calculatePlan(before).tasks, forecast = calculate(before).tasks;
        const auto proposed = mergeXerFinancials(before);
        check(proposed.ready() && proposed.entryIds.size() == 4 && proposed.financials.tasks[0].actualMinor == 12280,
              "disjoint native union");
        check(!xerFinancialsCurrent(before), "modified source unexpectedly current");
        auto prepared = prepare(*d);
        if (prepared->failure())
            std::cerr << prepared->error() << '\n';
        check(prepared->state() == EditState::Ready && *d->project() == before, "merge preparation atomicity");
        check(d->applyPreparedEdit(*prepared) == EditState::Applied, "merge application");
        unchanged(before, *d->project());
        check(nativeSchemaFor(*d->project()) == 27 && xerFinancialsCurrent(*d->project()) && !d->canUndo(),
              "schema27/current/immutable");
        check(calculatePlan(*d->project()).tasks == plan && calculate(*d->project()).tasks == forecast,
              "merge changed schedule");
        const auto &r = d->project()->xerFinancialRevisions.back();
        check(r.merge && r.financials.statusAt == "2026-10-29 18:00" &&
                  r.financials.resources[0].name == "현장 설계자" && r.financials.assignments[0].regularMinor == 9012,
              "source metadata or regular/OT altered");
        const auto balance = actualCostAllocations(*d->project(), *d->project()->statusAt).at(p.tasks[0].id);
        check(balance.cumulative == 12280 && balance.allocated == 4534 && balance.unallocated == 7746,
              "native AC changed or double counted");
        d->recordXerFinancialMerge(mergeSubmission(), xerFinancialProjectionHash);
        unchanged(before, *d->project());
        check(d->project()->xerFinancialRevisions.size() == 2, "repeat merge missing audit");
        d->save();
        check(store.load(d->path()).project == *d->project(), "schema27 save/load");
        auto body = json::encodeProject(*d->project());
        check(decodeProjectFile(envelope(body, 27)) == *d->project(), "schema27 envelope");
        rejects([&] { decodeProjectFile(envelope(body, 26)); });
        check(decodeProjectFile(envelope(body, 30)) == *d->project(), "legacy data in schema30 envelope");
        check(decodeProjectFile(envelope(body, 52)) == json::decodeProject(body, 52), "supported schema52");
        rejects([&] { decodeProjectFile(envelope(body, maximumNativeSchema + 1)); });
        for (int kind = 0; kind < 6; ++kind) {
            auto bad = body;
            auto revisions = bad["xerFinancialRevisions"].toArray();
            auto rev = revisions[0].toObject();
            auto merge = rev["merge"].toObject();
            if (kind == 0)
                merge["baseProjectionSha256"] = QString(64, '0');
            if (kind == 1)
                merge["policy"] = "unknown";
            if (kind == 2)
                merge["replacedPeriods"] = QJsonArray{99999};
            if (kind == 3)
                rev["ownedEntryIds"] = QJsonArray{"missing"};
            if (kind == 4) {
                auto financials = rev["financials"].toObject();
                auto tasks = financials["tasks"].toArray();
                auto task = tasks[0].toObject();
                task["actualMinor"] = 12281;
                tasks[0] = task;
                financials["tasks"] = tasks;
                rev["financials"] = financials;
            }
            if (kind == 5) {
                auto owned = rev["ownedEntryIds"].toArray();
                auto allocations = rev["allocations"].toArray();
                owned.removeAt(0);
                allocations.removeAt(0);
                rev["ownedEntryIds"] = owned;
                rev["allocations"] = allocations;
            }
            rev["merge"] = merge;
            revisions[0] = rev;
            bad["xerFinancialRevisions"] = revisions;
            rejects([&] { decodeProjectFile(envelope(bad, 27)); });
        }
        d->renameTask(0, "복구할 이름");
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        auto session = recovery.newSession();
        session->checkpoint(d->recoverySnapshot("2026-11-07T08:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema27 recovery scan");
        auto claim = recovery.claim(scan.candidates[0]);
        Document recovered(store);
        recovered.recover(claim->snapshot);
        check(recovered.project()->xerFinancialRevisions == d->project()->xerFinancialRevisions,
              "recovery lost merge audit");
        d->undo();
        check(xerFinancialsCurrent(*d->project()), "undo did not preserve binding");
        d->saveAs((temp.path() + "/copy.ppm").toStdString());
        check(xerFinancialsCurrent(*d->project()), "copy changed financial binding");
        const auto out = exportPrimaveraXer(*d->project());
        const auto back = importPrimaveraXer(out.xer, "merged.xer").project;
        check(back.xerFinancials->assignments[0].regularWork == 19500 &&
                  costAsOf(back, back.tasks[0].id, *back.statusAt)->actualMinor == 12280 &&
                  actualCostAllocations(back, *back.statusAt).at(back.tasks[0].id).allocated == 4534,
              "merged XER projection");
        write("xer-merge-qa/source.xer", bytes);
        write("xer-merge-qa/export.xer", out.xer);
        write("xer-merge-qa/export-report.json", QJsonDocument(out.report).toJson());
        write("xer-merge-qa/before.json", QJsonDocument(json::encodeProject(before)).toJson());
        write("xer-merge-qa/merged.json", QJsonDocument(json::encodeProject(*d->project())).toJson());
        write("xer-merge-qa/merged.ppm", envelope(json::encodeProject(*d->project()), 27));
        // Standard aggregate editing remains available after adopting foreign record IDs.
        auto edited = *activeXerFinancials(*d->project());
        for (auto &e : edited.periods)
            if (e.resourceId == p.resources[0].id && e.start.substr(0, 10) == "2026-10-27")
                e.work = 2400;
        d->recordXerFinancials({edited, d->project()->xerFinancialRevisions.back().allocations, "정정자",
                                "병합 후 날짜 정정", "2026-11-07T09:00:00Z"},
                               xerFinancialProjectionHash);
        check(xerFinancialsCurrent(*d->project()) && d->project()->xerFinancialRevisions.back().merge == std::nullopt,
              "post-merge editing failed");
        validate(*d->project());
        // The same original aggregate and new daily value require an explicit replacement choice.
        auto conflict = document(p);
        report(*conflict, {row(p, "2026-10-20", false, 1200, 1000)});
        check(!mergeXerFinancials(*conflict->project()).ready(), "unallocated source overlap silently added");
        const auto failed = *conflict->project();
        rejects([&] { conflict->recordXerFinancialMerge(mergeSubmission(), xerFinancialProjectionHash); });
        check(*conflict->project() == failed, "failed merge changed native facts");
        XerFinancialMergeOptions resolution;
        resolution.replacePeriods = {0};
        const auto resolved = mergeXerFinancials(*conflict->project(), resolution);
        check(resolved.ready(), "period replacement not resolvable");
        conflict->recordXerFinancialMerge(mergeSubmission(resolution), xerFinancialProjectionHash);
        unchanged(failed, *conflict->project());
        // Independent expense detail can overlap the frozen external EV/PV range without duplication.
        auto direct = document(p);
        report(*direct, {row(p, "2026-10-20", true, {}, 200)});
        XerFinancialMergeOptions expenseResolution;
        expenseResolution.replacePeriods = {2};
        check(mergeXerFinancials(*direct->project(), expenseResolution).ready(), "expense replacement");
        direct->recordXerFinancialMerge(mergeSubmission(expenseResolution), xerFinancialProjectionHash);
        auto expenseExport = exportPrimaveraXer(*direct->project());
        auto expenseBack = importPrimaveraXer(expenseExport.xer, "expense.xer").project;
        check(nativeSchemaFor(expenseBack) == 27 &&
                  actualCostAllocations(expenseBack, *expenseBack.statusAt).at(expenseBack.tasks[0].id).allocated ==
                      3534,
              "orthogonal expense/EV summary roundtrip");
        rejects([&] { json::decodeProject(json::encodeProject(expenseBack), 26); });
        write("xer-merge-qa/expense-export.xer", expenseExport.xer);
        write("xer-merge-qa/expense-merged.json", QJsonDocument(json::encodeProject(*direct->project())).toJson());
        auto reduced = document(p);
        CostEntry lower;
        lower.taskId = p.tasks[0].id;
        lower.actualMinor = 10000;
        lower.reporter = "원가 정정자";
        lower.reason = "현재 누계 정정";
        lower.recordedAt = "2026-11-07T07:00:00Z";
        reduced->recordCost(lower);
        check(!mergeXerFinancials(*reduced->project()).ready(), "source costs over current AC accepted");
        auto reducedTotal = p.xerFinancials->assignments[0];
        reducedTotal.regularMinor = 7400;
        XerFinancialMergeOptions reduceOptions;
        reduceOptions.replacePeriods = {0};
        reduceOptions.assignmentOverrides = {reducedTotal};
        check(mergeXerFinancials(*reduced->project(), reduceOptions).ready(),
              "explicit empty-period replacement/AC conflict resolution");
        const auto reducedBefore = *reduced->project();
        reduced->recordXerFinancialMerge(mergeSubmission(reduceOptions), xerFinancialProjectionHash);
        unchanged(reducedBefore, *reduced->project());
        // Already allocated source days use current corrections, preserving older audit links.
        auto allocated = document(p);
        auto allocations = initialXerFinancialAllocations(*p.xerFinancials);
        allocations.push_back({0, {{"2026-10-19", 1}, {"2026-10-20", 1}}});
        allocated->recordXerFinancials(
            {*p.xerFinancials, allocations, "배분자", "기존 기간 배분", "2026-11-07T07:00:00Z"},
            xerFinancialProjectionHash);
        report(*allocated, {row(p, "2026-10-19", false, 5000, 3000)});
        check(mergeXerFinancials(*allocated->project()).ready(), "allocated period correction treated as ambiguity");
        const auto allocatedBefore = *allocated->project();
        allocated->recordXerFinancialMerge(mergeSubmission(), xerFinancialProjectionHash);
        unchanged(allocatedBefore, *allocated->project());
        validate(*allocated->project());
        // Native facts do not identify overtime; require an explicit cumulative split on overflow.
        auto overflow = document(p);
        report(*overflow, {row(p, "2026-10-27", false, 6000, 2000)}, 14280);
        check(!mergeXerFinancials(*overflow->project()).ready(), "overtime classification inferred");
        auto total = p.xerFinancials->assignments[0];
        total.regularWork = 22500;
        total.regularMinor = 10012;
        XerFinancialMergeOptions specified;
        specified.assignmentOverrides = {total};
        check(mergeXerFinancials(*overflow->project(), specified).ready(), "explicit cumulative split rejected");
        overflow->recordXerFinancialMerge(mergeSubmission(specified), xerFinancialProjectionHash);
        // Added identities have no inherited cumulative/OT claim, including explicit zero.
        auto added = document(p);
        added->addTask("추가 작업");
        const auto newTask = added->project()->tasks.back().id;
        added->setDuration(added->project()->tasks.size() - 1, 60);
        added->setAssignments(newTask, {{newTask, p.resources[0].id, 100}});
        auto newRow = row(p, "2026-10-27", false, 6000, 0);
        newRow.taskId = newTask;
        report(*added, {newRow}, 0);
        XerFinancialMergeOptions newTotals;
        newTotals.assignmentOverrides.push_back({newTask, p.resources[0].id, 6000, {}, 0, {}});
        check(mergeXerFinancials(*added->project(), newTotals).ready(), "new identity/zero merge");
        added->recordXerFinancialMerge(mergeSubmission(newTotals), xerFinancialProjectionHash);
        auto withdrawn = document(p);
        report(*withdrawn, {row(p, "2026-10-28", false, {}, {})});
        check(mergeXerFinancials(*withdrawn->project()).ready(), "explicit withdrawal missing");
        withdrawn->recordXerFinancialMerge(mergeSubmission(), xerFinancialProjectionHash);
        check(withdrawn->project()->xerFinancialRevisions.back().ownedEntryIds.size() == 1,
              "withdrawal became reported zero");
        for (int kind = 0; kind < 5; ++kind) {
            auto bad = p;
            if (kind == 0)
                bad.statusAt = "2026-10-27 18:00";
            if (kind == 1)
                bad.currency = "KRW";
            if (kind == 2)
                bad.resources[0].type = "equipment";
            if (kind == 3)
                bad.assignments.erase(bad.assignments.begin());
            if (kind == 4)
                bad.tasks.pop_back();
            check(!mergeXerFinancials(bad).ready(), "unsupported identity/unit/status merged");
        }
        auto missing = p;
        missing.periodActuals.erase(missing.periodActuals.begin());
        check(!mergeXerFinancials(missing).ready(), "missing base row treated as zero");
        auto limited = p;
        for (int i = 0; i < 1001; ++i) {
            auto e = p.periodActuals.front();
            e.id = "limit-" + std::to_string(i);
            e.date = QDate(2020, 1, 1).addDays(i).toString(Qt::ISODate).toStdString();
            e.workUnitMinutes = 1;
            e.actualMinor.reset();
            e.costEntryId.clear();
            limited.periodActuals.push_back(std::move(e));
        }
        check(!mergeXerFinancials(limited).ready(), "merge native row limit ignored");
        for (int kind = 0; kind < 3; ++kind) {
            XerFinancialMergeOptions bad;
            if (kind == 0)
                bad.replacePeriods = {999};
            if (kind == 1)
                bad.replacePeriods = {0, 0};
            if (kind == 2)
                bad.assignmentOverrides = {p.xerFinancials->assignments[0], p.xerFinancials->assignments[0]};
            check(!mergeXerFinancials(p, bad).ready(), "malformed merge choices accepted");
        }
        auto cancelled = std::make_shared<CalculationControl>();
        cancelled->cancel();
        auto cancelResult = prepare(*d, {}, cancelled);
        check(cancelResult->state() == EditState::Cancelled &&
                  d->applyPreparedEdit(*cancelResult) == EditState::Cancelled,
              "cancel applied merge");
        auto pending = prepare(*d);
        d->renameTask(0, "다른 변경");
        check(d->applyPreparedEdit(*pending) == EditState::Stale, "stale preview published");
        d->undo();
        auto reloaded = document(before);
        auto sameSession = prepare(*reloaded);
        reloaded->open(reloaded->path());
        check(reloaded->applyPreparedEdit(*sameSession) == EditState::Stale,
              "same revision new session accepted merge");
        auto unauthorized = prepare(*reloaded);
        reloaded->setWriteAuthorization([] { return false; });
        check(reloaded->applyPreparedEdit(*unauthorized) == EditState::Stale, "revoked authority accepted merge");
        Document ro(store);
        ro.open(d->path(), true);
        rejects([&] { ro.recordXerFinancialMerge(mergeSubmission(), xerFinancialProjectionHash); });
        XerFinancialMergeDialog readOnly(ro);
        check(!readOnly.findChild<QPushButton *>("xerMergeAnalyze")->isEnabled(), "readonly merger enabled");
        auto uiDoc = document(before);
        ui(*uiDoc, false, true, false);
        ui(*uiDoc, false, false, true);
        auto uiConflict = document(failed);
        ui(*uiConflict, true, false, true);
        MainWindow window(*uiDoc);
        auto *action = window.findChild<QAction *>("mergeXerFinancials");
        check(action && action->isEnabled(), "merge menu absent");
        bool seen = false;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == "xerFinancialMergeDialog") {
                seen = true;
                dialog->reject();
            }
        });
        timer.start();
        action->trigger();
        timer.stop();
        check(seen, "menu did not open merge dialog");
        std::cout << "PASS: schema27 original/current ledger merge; disjoint residual union, allocated "
                     "corrections/withdrawals, explicit overlap/OT decisions, immutable native facts and AC, "
                     "identity/status guards, post-merge edit, save/copy/recovery, XER coalescing and native UI\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
