#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/XerFinancialAllocation.h"
#include "domain/Classification.h"
#include "calculation/Costs.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/ProjectJson.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "ui/XerFinancialEditDialog.h"
#include "ui/XerFinancialDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
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
using namespace ppm;
namespace {
void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
QByteArray envelope(const QJsonObject &o, int version) {
    const auto b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonObject{{"format", "PPM"},
                                     {"schemaVersion", version},
                                     {"payload", o},
                                     {"sha256", QString::fromLatin1(
                                                    QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex())}})
        .toJson();
}
XerFinancialSubmission submission(const Project &p) {
    return {*activeXerFinancials(p),
            p.xerFinancialRevisions.empty() ? initialXerFinancialAllocations(*p.xerFinancials)
                                            : p.xerFinancialRevisions.back().allocations,
            "검증 작성자", "원본 확인 후 집계 정정 및 날짜 배분", "2026-11-07T06:00:00Z"};
}
std::shared_ptr<Document::PreparedEdit>
prepare(Document &doc, XerFinancialSubmission s,
        std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(doc.makeEditRequest(XerFinancialEdit{std::move(s), xerFinancialProjectionHash}),
                                 std::move(c));
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "UI preparation timed out");
}
void ui(Document &doc, bool small, bool commit) {
    const auto before = *doc.project();
    XerFinancialEditDialog d(doc);
    d.show();
    QApplication::processEvents();
    if (small)
        d.resize(800, 600);
    auto *tables = d.findChild<QTabWidget *>("xerEditTabs");
    auto *period = d.findChild<QTableView *>("xerEditTable2");
    auto *preview = d.findChild<QPushButton *>("xerEditPreview");
    auto *submit = d.findChild<QPushButton *>("xerEditSubmit");
    auto *days = d.findChild<QPlainTextEdit *>("xerAllocationDays");
    check(tables && period && preview && submit && days, "edit controls absent");
    check(preview->isEnabled() && !submit->isEnabled(), "submit allowed without preview");
    tables->setCurrentIndex(2);
    period->setCurrentIndex(period->model()->index(0, 0));
    days->setPlainText("2026-10-19,1\n2026-10-20,2");
    auto *task = d.findChild<QTableView *>("xerEditTable0");
    auto *assignment = d.findChild<QTableView *>("xerEditTable1");
    check(task->model()->setData(task->model()->index(0, 1), "123.81", Qt::EditRole), "AC editor input");
    check(!assignment->model()->setData(assignment->model()->index(0, 2), "0.000001", Qt::EditRole),
          "inexact UI work accepted");
    check(assignment->model()->setData(assignment->model()->index(0, 2), "19501u", Qt::EditRole),
          "regular work editor");
    check(assignment->model()->setData(assignment->model()->index(0, 4), "90.13", Qt::EditRole), "regular cost editor");
    check(period->model()->setData(period->model()->index(0, 4), "12001u", Qt::EditRole) &&
              period->model()->setData(period->model()->index(0, 5), "70.13", Qt::EditRole),
          "period editor");
    assignment->setCurrentIndex(assignment->model()->index(0, 0));
    d.findChild<QLineEdit *>("xerNewPeriodStart")->setText("2026-10-27");
    d.findChild<QLineEdit *>("xerNewPeriodFinish")->setText("2026-10-27");
    d.findChild<QPushButton *>("xerAddResidualPeriod")->click();
    check(period->model()->rowCount() == 5 && period->model()->data(period->model()->index(4, 4)).toString() == "3000u",
          "UI residual period");
    d.findChild<QPushButton *>("xerEqualDays")->click();
    task->setCurrentIndex(task->model()->index(0, 0));
    d.findChild<QPushButton *>("xerAddDirectPeriod")->click();
    check(period->model()->rowCount() == 6 && period->model()->data(period->model()->index(5, 11)).toString() == "8.00",
          "UI direct residual period");
    d.findChild<QPushButton *>("xerEqualDays")->click();
    d.findChild<QLineEdit *>("xerEditReporter")->setText("UI 작성자");
    d.findChild<QLineEdit *>("xerEditReason")->setText("날짜 명시 배분");
    d.findChild<QScrollArea *>("editorScrollArea")->verticalScrollBar()->setValue(0);
    d.grab().save(small ? "xer-allocation-qa/editor-small.png" : "xer-allocation-qa/editor.png");
    preview->click();
    wait([&] { return preview->isEnabled(); });
    if (!submit->isEnabled())
        std::cerr << d.findChild<QLabel *>("xerEditMessage")->text().toStdString() << '\n';
    check(submit->isEnabled() && *doc.project() == before, "preview mutated document/failed");
    check(d.findChild<QTableWidget *>("xerEditPreviewRows")->rowCount() == 6, "native preview rows wrong");
    d.grab().save("xer-allocation-qa/preview.png");
    d.findChild<QLineEdit *>("xerEditReason")->setText("수정된 사유");
    check(!submit->isEnabled(), "draft change did not invalidate preview");
    preview->click();
    wait([&] { return preview->isEnabled(); });
    if (commit) {
        submit->click();
        check(doc.project()->xerFinancialRevisions.size() == 1, "UI did not commit");
    } else {
        d.reject();
        check(*doc.project() == before, "closing editor published proposal");
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
        QDir().mkpath("xer-allocation-qa");
        const auto source = readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE));
        const auto p = importPrimaveraXer(source, "financial-periods.xer").project;
        auto s = submission(p);
        s.financials.tasks[0].actualMinor = 14001;
        s.financials.assignments[0].regularWork = 22501;
        s.financials.assignments[0].regularMinor = 10013;
        s.financials.periods[0].work = 12001;
        s.financials.periods[0].actualMinor = 7013;
        s.financials.periods[2].laborMinor = 7013;
        XerFinancialPeriod residual;
        residual.taskId = p.tasks[0].id;
        residual.resourceId = p.resources[0].id;
        residual.start = "2026-10-27 00:00";
        residual.finish = "2026-10-27 23:59";
        residual.work = 6000;
        residual.actualMinor = 2000;
        s.financials.periods.push_back(residual);
        s.allocations.push_back({0, {{"2026-10-20", 2}, {"2026-10-19", 1}}});
        s.allocations.push_back({2, {{"2026-10-19", 1}, {"2026-10-20", 1}}});
        s.allocations.push_back({4, {{"2026-10-27", 1}}});
        const auto desired = allocateXerFinancials(s.financials, s.allocations);
        const auto key = [&](const char *date) { return PeriodActualKey{p.tasks[0].id, p.resources[0].id, date}; };
        check(desired.at(key("2026-10-19")).workUnitMinutes == 4000 &&
                  desired.at(key("2026-10-20")).workUnitMinutes == 8001,
              "work largest remainder oracle");
        check(desired.at(key("2026-10-19")).actualMinor == 2338 && desired.at(key("2026-10-20")).actualMinor == 4675,
              "money remainder oracle");
        // Null/zero, shuffled days, bounds and large products are independently specified.
        auto maximum = s.financials;
        maximum.tasks[0].actualMinor = maxMoney;
        maximum.assignments[0].regularMinor = maxMoney - 2034;
        maximum.periods[0].actualMinor = maxMoney - 6000;
        maximum.periods[1].actualMinor = 0;
        maximum.periods[2].expenseMinor = 0;
        maximum.periods[3].expenseMinor = 0;
        maximum.periods[4].actualMinor = 0;
        auto huge = allocateXerFinancials(maximum, {{0, {{"2026-10-19", 1000000}, {"2026-10-20", 999999}}}});
        std::int64_t total = 0;
        for (const auto &[k, e] : huge) {
            (void)k;
            total += *e.actualMinor;
        }
        check(total == maxMoney - 6000, "large weighted amount overflow");
        auto missing = s.financials;
        missing.periods[0].work.reset();
        missing.periods[0].actualMinor = 0;
        const auto zero = allocateXerFinancials(missing, {{0, {{"2026-10-24", 1}}}});
        check(!zero.begin()->second.workUnitMinutes && zero.begin()->second.actualMinor == 0,
              "null/zero or weekend allocation lost");
        auto tied = s.financials;
        tied.periods[0].work = 1;
        tied.periods[0].actualMinor = 1;
        const auto tie = allocateXerFinancials(tied, {{0, {{"2026-10-20", 1}, {"2026-10-19", 1}}}});
        check(tie.at(key("2026-10-19")).workUnitMinutes == 1 && tie.at(key("2026-10-20")).actualMinor == 0,
              "tie must favor civil date, independent of input order");
        for (int kind = 0; kind < 6; ++kind) {
            auto bad = s.allocations;
            if (kind == 0)
                bad.back().periodIndex = 999;
            if (kind == 1)
                bad.back().days[0].weight = 0;
            if (kind == 2)
                bad.back().days[0].date = "2026-10-29";
            if (kind == 3)
                bad.back().days.push_back(bad.back().days.front());
            if (kind == 4)
                bad.push_back(bad.front());
            if (kind == 5)
                bad.back().days.clear();
            rejects([&] { allocateXerFinancials(s.financials, bad); });
        }
        QTemporaryDir temp;
        JsonProjectStore store;
        const auto path = (temp.path() + "/edit.ppm").toStdString();
        store.save(path, p, {});
        Document doc(store);
        doc.open(path);
        const auto plan = calculatePlan(p).tasks, forecast = calculate(p).tasks;
        auto prepared = prepare(doc, s);
        if (prepared->state() != EditState::Ready)
            std::cerr << prepared->error() << '\n';
        check(prepared->state() == EditState::Ready && *doc.project() == p, "preparation atomicity");
        check(doc.applyPreparedEdit(*prepared) == EditState::Applied, "apply financial edit");
        check(nativeSchemaFor(*doc.project()) == 26 && xerFinancialsCurrent(*doc.project()) && !doc.canUndo(),
              "schema26/current/immutable");
        check(doc.project()->xerFinancials == p.xerFinancials && doc.project()->progress == p.progress &&
                  doc.project()->baselines == p.baselines,
              "original/frozen inputs changed");
        check(!analyzeCosts(*doc.project(), "").tasks[0].evm.ev, "external EV leaked after aggregate correction");
        check(calculatePlan(*doc.project()).tasks == plan && calculate(*doc.project()).tasks == forecast,
              "allocation changed schedule");
        auto balance = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at(p.tasks[0].id);
        check(balance.cumulative == 14001 && balance.allocated == 12581 && balance.unallocated == 1420,
              "14001=12581+1420");
        const auto first = *doc.project();
        doc.recordXerFinancials(submission(first), xerFinancialProjectionHash);
        check(actualCostAllocations(*doc.project(), *doc.project()->statusAt).at(p.tasks[0].id).allocated == 12581,
              "repeat submission doubled AC");
        check(doc.project()->xerFinancialRevisions.front() == first.xerFinancialRevisions.front(),
              "earlier aggregate correction mutated");
        auto move = submission(*doc.project());
        for (auto &a : move.allocations)
            if (a.periodIndex == 0)
                a.days = {{"2026-10-21", 1}};
        doc.recordXerFinancials(move, xerFinancialProjectionHash);
        const auto rows = periodActualsAsOf(*doc.project(), *doc.project()->statusAt);
        check(!rows.at(key("2026-10-19"))->workUnitMinutes && !rows.at(key("2026-10-20"))->actualMinor &&
                  rows.at(key("2026-10-21"))->workUnitMinutes == 12001,
              "rescheduling did not withdraw former dates");
        check(doc.project()->periodActuals.front() == p.periodActuals.front(), "native original overwritten");
        const auto stable = *doc.project();
        auto bad = submission(stable);
        bad.financials.tasks[0].actualMinor = 1;
        rejects([&] { doc.recordXerFinancials(bad, xerFinancialProjectionHash); });
        check(*doc.project() == stable, "invalid edit partially applied");
        // Even an explicitly rebound source cannot claim a foreign native key.
        const auto conflictPath = (temp.path() + "/conflict.ppm").toStdString();
        store.save(conflictPath, p, {});
        Document conflictSource(store);
        conflictSource.open(conflictPath);
        PeriodActualSubmission foreign;
        foreign.reporter = "다른 입력";
        foreign.reason = "별도 관측";
        foreign.recordedAt = "2026-11-07T06:00:00Z";
        PeriodActualEntry foreignRow;
        foreignRow.taskId = p.tasks[0].id;
        foreignRow.resourceId = p.resources[0].id;
        foreignRow.date = "2026-10-19";
        foreignRow.workUnitMinutes = 1;
        foreign.entries.push_back(foreignRow);
        conflictSource.recordPeriodActuals(foreign);
        auto rebound = *conflictSource.project();
        rebound.xerFinancials->projectionSha256 = xerFinancialProjectionHash(rebound);
        const auto reboundPath = (temp.path() + "/rebound.ppm").toStdString();
        store.save(reboundPath, rebound, {});
        Document collision(store);
        collision.open(reboundPath);
        auto collisionInput = submission(rebound);
        collisionInput.allocations.push_back({0, {{"2026-10-19", 1}}});
        rejects([&] { collision.recordXerFinancials(collisionInput, xerFinancialProjectionHash); });
        check(*collision.project() == rebound, "foreign record was overwritten");
        bad = submission(stable);
        bad.reason = "";
        rejects([&] { doc.recordXerFinancials(bad, xerFinancialProjectionHash); });
        bad = submission(stable);
        bad.financials.sourceSha256 = std::string(64, '1');
        rejects([&] { doc.recordXerFinancials(bad, xerFinancialProjectionHash); });
        auto canceled = std::make_shared<CalculationControl>();
        canceled->cancel();
        auto canceledEdit = prepare(doc, submission(stable), canceled);
        check(canceledEdit->state() == EditState::Cancelled &&
                  doc.applyPreparedEdit(*canceledEdit) == EditState::Cancelled,
              "cancel published facts");
        auto stale = prepare(doc, submission(stable));
        doc.renameTask(0, "변경");
        check(!xerFinancialsCurrent(*doc.project()) && doc.applyPreparedEdit(*stale) == EditState::Stale,
              "stale worker accepted");
        rejects([&] { doc.recordXerFinancials(submission(stable), xerFinancialProjectionHash); });
        doc.undo();
        check(xerFinancialsCurrent(*doc.project()) &&
                  doc.project()->xerFinancialRevisions == stable.xerFinancialRevisions,
              "plan undo deleted facts");
        doc.save();
        check(store.load(path).project == *doc.project(), "schema26 persistence");
        auto body = json::encodeProject(*doc.project());
        rejects([&] { decodeProjectFile(envelope(body, 25)); });
        check(decodeProjectFile(envelope(body, 30)) == *doc.project(), "legacy data in schema30 envelope");
        check(decodeProjectFile(envelope(body, 52)) == json::decodeProject(body, 52), "supported schema52");
        rejects([&] { decodeProjectFile(envelope(body, maximumNativeSchema + 1)); });
        auto revisions = body["xerFinancialRevisions"].toArray();
        auto r = revisions[0].toObject();
        r["ownedEntryIds"] = QJsonArray{"missing"};
        revisions[0] = r;
        auto damaged = body;
        damaged["xerFinancialRevisions"] = revisions;
        rejects([&] { decodeProjectFile(envelope(damaged, 26)); });
        doc.renameTask(0, "복구 초안");
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        auto session = recovery.newSession();
        session->checkpoint(doc.recoverySnapshot("2026-11-07T06:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema26 recovery");
        auto claim = recovery.claim(scan.candidates[0]);
        Document recovered(store);
        recovered.recover(claim->snapshot);
        check(recovered.project()->xerFinancialRevisions == doc.project()->xerFinancialRevisions,
              "recovery lost corrections");
        doc.undo();
        doc.saveAs((temp.path() + "/copy.ppm").toStdString());
        check(xerFinancialsCurrent(*doc.project()), "copy invalidated financial adjustments");
        Document ro(store);
        ro.open(doc.path(), true);
        rejects([&] { ro.recordXerFinancials(submission(*ro.project()), xerFinancialProjectionHash); });
        XerFinancialEditDialog readOnly(ro);
        check(!readOnly.findChild<QPushButton *>("xerEditPreview")->isEnabled(), "readonly editor active");
        auto exported = exportPrimaveraXer(*doc.project());
        auto imported = importPrimaveraXer(exported.xer, "corrected.xer").project;
        check(imported.xerFinancials->assignments[0].regularWork == 22501 &&
                  imported.xerFinancials->tasks[0].actualMinor == 14001 && imported.xerFinancials->periods.size() == 5,
              "corrected XER aggregate projection");
        // Export retains aggregate ranges; native source retains explicit day allocation/history.
        write("xer-allocation-qa/source.xer", source);
        write("xer-allocation-qa/export.xer", exported.xer);
        write("xer-allocation-qa/export-report.json", QJsonDocument(exported.report).toJson());
        write("xer-allocation-qa/edited.ppm", envelope(json::encodeProject(*doc.project()), 26));
        write("xer-allocation-qa/edited-body.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
        write("xer-allocation-qa/source-body.json", QJsonDocument(json::encodeProject(p)).toJson());
        bool correctedSeen = false, originalSeen = false;
        QTimer viewTimer;
        viewTimer.setInterval(20);
        QObject::connect(&viewTimer, &QTimer::timeout, [&] {
            auto *view = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!view || view->objectName() != "xerFinancialDialog")
                return;
            const auto count = view->findChild<QTabWidget *>("xerFinancialTabs")->count();
            if (count == 4 && !correctedSeen) {
                correctedSeen = true;
                auto *history = view->findChild<QTableWidget *>("xerFinancialRevisionHistory");
                check(history && history->rowCount() == 3, "aggregate audit view");
                view->grab().save("xer-allocation-qa/corrected-view.png");
                doc.renameTask(0, "조회 중 변경"); // A reference view must outlive replacement of the live Project.
                QTimer::singleShot(0, view->findChild<QPushButton *>("xerViewOriginal"), &QPushButton::click);
            } else if (count == 3) {
                originalSeen = true;
                auto *a = view->findChild<QTableView *>("xerFinancialTable0");
                check(a->model()->data(a->model()->index(0, 2)).toString() == "3.250000", "original view mutated");
                view->grab().save("xer-allocation-qa/original-view.png");
                view->reject();
            } else if (correctedSeen && originalSeen)
                view->reject();
        });
        viewTimer.start();
        showXerFinancials(nullptr, *doc.project());
        viewTimer.stop();
        check(correctedSeen && originalSeen, "original/corrected reference views unavailable");
        doc.undo();
        // AC-only and completely unreported allocation still retain an immutable aggregate audit.
        auto withdrawn = submission(*doc.project());
        withdrawn.allocations.clear();
        doc.recordXerFinancials(withdrawn, xerFinancialProjectionHash);
        check(actualCostAllocations(*doc.project(), *doc.project()->statusAt).at(p.tasks[0].id).allocated == 0,
              "allocation withdrawal");
        auto ac = submission(*doc.project());
        ac.financials.tasks[0].actualMinor = 15001;
        doc.recordXerFinancials(ac, xerFinancialProjectionHash);
        check(costAsOf(*doc.project(), p.tasks[0].id, *doc.project()->statusAt)->actualMinor == 15001 &&
                  xerFinancialsCurrent(*doc.project()),
              "AC-only correction");
        // GUI preview/close/commit uses the same worker transaction.
        const auto uiPath = (temp.path() + "/ui.ppm").toStdString();
        store.save(uiPath, p, {});
        Document uiDoc(store);
        uiDoc.open(uiPath);
        ui(uiDoc, true, false);
        ui(uiDoc, false, true);
        auto pending = prepare(uiDoc, submission(*uiDoc.project()));
        uiDoc.open(uiPath);
        check(uiDoc.applyPreparedEdit(*pending) == EditState::Stale, "new session accepted pending edit");
        MainWindow window(uiDoc);
        auto *action = window.findChild<QAction *>("editXerFinancials");
        check(action && action->isEnabled(), "main menu editor action absent");
        bool seen = false;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (d && d->objectName() == "xerFinancialEditDialog") {
                seen = true;
                d->reject();
            }
        });
        timer.start();
        action->trigger();
        check(seen, "menu failed to open editor");
        timer.stop();
        std::cout << "PASS: XER aggregate correction audit/schema26, exact weighted days and residuals, immutable "
                     "source/native corrections, no double counting, AC-only/withdrawal, cancellation/stale/read-only, "
                     "save/copy/recovery, XER and native editor preview\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
