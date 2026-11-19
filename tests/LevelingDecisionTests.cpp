#include "application/Document.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/LevelingDecisionJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "ui/LevelingDecisionDialog.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <future>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *message) {
    if (!ok)
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
LevelingSelection selection(const Document &d) {
    LevelingSelection s;
    s.policy = levelingPolicyFor(*d.project());
    s.sourceDocumentId = d.project()->documentId;
    s.sourceRevision = d.project()->revision;
    s.reason = "주간 자원 조정안 채택";
    s.adoptedAt = "2026-11-05T01:00:00Z";
    return s;
}
auto prepare(Document &d, EditCommand command,
             std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async,
                      [request = d.makeEditRequest(std::move(command)), control]() mutable {
                          return Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
void fixture(Document &d) {
    d.create("평준화 결정 이력 검증", "2026-11-02", {"기초", "설비"});
    d.setDuration(0, 480);
    d.setDuration(1, 480);
    Resource r;
    r.name = "공유 작업자";
    const auto rid = d.putResource(r);
    const auto source = d.snapshot();
    for (const auto &t : source->tasks)
        d.setAssignments(t.id, {{t.id, rid, 100}});
}
void lifecycle(const QString &output) {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    fixture(d);
    const auto baseline = d.captureBaseline("최초 계획", "PM", "승인", "2026-11-05T00:00:00Z");
    (void)baseline;
    const auto before = *d.project();
    auto worker = prepare(d, AdoptLevelingEdit{selection(d)});
    check(worker->state() == EditState::Ready && *d.project() == before, "worker altered live history");
    check(d.applyPreparedEdit(*worker) == EditState::Applied, "adopt worker");
    const auto first = d.project()->levelingDecisions.at(0);
    check(first.action == "adopt" && first.reason == selection(d).reason && !first.beforePlan && first.afterPlan &&
              first.sourceRevision == before.revision && first.recordedRevision == d.project()->revision &&
              first.sourceDocumentId == before.documentId && first.previousEntryId.empty() &&
              validUtcStamp(first.recordedAt),
          "adoption provenance");
    check(d.calculation()->projectFinish == "2026-11-03 18:00", "adoption changed calculation");
    d.saveAs(temp.filePath("history.ppm").toStdString());
    check(nativeSchemaFor(*d.project()) == 15, "history must require schema15");
    d.undo();
    check(!d.project()->planLeveling && d.project()->levelingDecisions.size() == 2 && d.dirty() &&
              d.project()->levelingDecisions[0] == first && d.project()->levelingDecisions.back().action == "undo",
          "undo erased decision or lost dirty state");
    d.redo();
    check(d.project()->levelingDecisions.size() == 3 && d.project()->planLeveling && d.dirty() &&
              d.project()->levelingDecisions.back().action == "redo",
          "redo lost decision or treated changed ledger as saved input");
    auto alt = selection(d);
    alt.priority = "longest-first";
    d.adoptLeveling(alt);
    check(d.project()->levelingDecisions.back().beforePlan->priority == "task-order" &&
              d.project()->levelingDecisions.back().afterPlan->priority == "longest-first",
          "replacement not recorded");
    const auto count = d.project()->levelingDecisions.size();
    d.renameTask(0, "기초 수정");
    check(!d.project()->planLeveling && d.project()->levelingDecisions.size() == count + 1 &&
              d.project()->levelingDecisions.back().action == "invalidate",
          "fast rename bypassed ledger");
    d.undo();
    const auto oldCount = d.project()->levelingDecisions.size();
    TaskEdit edit;
    edit.taskId = d.project()->tasks[1].id;
    edit.name = "표에서 수정";
    auto bulk = prepare(d, BatchTaskEdit{{edit}});
    check(d.applyPreparedEdit(*bulk) == EditState::Applied && d.project()->levelingDecisions.size() == oldCount + 1,
          "bulk bypassed ledger");
    auto undo = prepare(d, HistoryEdit{false});
    check(d.applyPreparedEdit(*undo) == EditState::Applied && d.project()->planLeveling &&
              d.project()->levelingDecisions.back().action == "undo",
          "worker undo removed ledger");
    auto redo = prepare(d, HistoryEdit{true});
    check(d.applyPreparedEdit(*redo) == EditState::Applied && !d.project()->planLeveling &&
              d.project()->levelingDecisions.back().action == "redo",
          "worker redo removed ledger");
    undo = prepare(d, HistoryEdit{false});
    check(d.applyPreparedEdit(*undo) == EditState::Applied, "restore selection");
    auto canceled = prepare(d, ClearLevelingEdit{});
    auto control = std::make_shared<CalculationControl>();
    auto cancellation = prepare(d, ClearLevelingEdit{}, control);
    control->cancel();
    const auto intact = *d.project();
    check(d.applyPreparedEdit(*cancellation) == EditState::Cancelled && *d.project() == intact,
          "canceled commit recorded event");
    control = std::make_shared<CalculationControl>();
    control->cancel();
    auto stopped = prepare(d, ClearLevelingEdit{}, control);
    check(stopped->state() == EditState::Cancelled && *d.project() == intact, "canceled worker recorded event");
    d.clearLeveling();
    check(d.project()->levelingDecisions.back().action == "release", "explicit release misclassified");
    const auto released = *d.project();
    d.clearLeveling();
    check(*d.project() == released, "no-op release recorded event");
    check(d.applyPreparedEdit(*canceled) == EditState::Stale && *d.project() == released,
          "stale result recorded event");
    d.adoptLeveling(selection(d));
    d.setStatusAt("2026-11-02 18:00");
    check(d.project()->levelingDecisions.back().beforePlan && !d.project()->levelingDecisions.back().afterPlan,
          "status change not recorded");
    d.adoptLeveling(selection(d));
    check(d.project()->levelingDecisions.back().afterRemaining && !d.project()->planLeveling, "remaining scope lost");
    const auto untouched = d.project()->levelingDecisions;
    CostEntry cost;
    cost.taskId = d.project()->tasks[0].id;
    cost.recordedAt = "2026-11-05T02:00:00Z";
    cost.reporter = "PM";
    cost.reason = "누계 관측";
    cost.actualMinor = 10;
    cost.currency = d.project()->currency;
    d.recordCost(cost);
    check(d.project()->remainingLeveling && d.project()->levelingDecisions == untouched,
          "cost fact invalidated selection");
    d.save();
    auto oldSession = prepare(d, ClearLevelingEdit{});
    d.open(d.path());
    const auto reopened = *d.project();
    check(d.applyPreparedEdit(*oldSession) == EditState::Stale && *d.project() == reopened, "session leaked decision");
    Document ro(store);
    ro.open(d.path(), true);
    rejects([&] { ro.clearLeveling(); });
    check(*ro.project() == reopened, "readonly changed decisions");
    d.saveAs(temp.filePath("copy.ppm").toStdString());
    check(d.project()->documentId != reopened.documentId &&
              d.project()->levelingDecisions == reopened.levelingDecisions,
          "copy rewrote original provenance");
    d.clearLeveling();
    check(d.project()->levelingDecisions.back().sourceDocumentId == d.project()->documentId,
          "new event attributed to ancestor");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-05T03:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema15 recovery unreadable");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->levelingDecisions == d.project()->levelingDecisions &&
              restored.project()->baselines == before.baselines,
          "recovery changed archives");
    QDir().mkpath(output);
    store.save((output + "/source.ppm").toStdString(), *d.project(), {});
    const auto xml = exportProjectXml(*d.project());
    const auto xer = exportPrimaveraXer(*d.project());
    check(QJsonDocument(xml.report).toJson().contains("LevelingDecisions") &&
              QJsonDocument(xer.report).toJson().contains("leveling_decisions"),
          "exchange omitted journal loss");
    const auto xmlPackage = publishProjectXml(xml, temp.path() + "/xml-exchange");
    const auto xerPackage = publishPrimaveraXer(xer, temp.path() + "/xer-exchange");
    for (const auto &package : {xmlPackage, xerPackage})
        check(store.load((package + "/source.ppm").toStdString()).project == *d.project(),
              "exchange native copy lost decisions");
    const auto body = json::encodeProject(*d.project());
    check(json::decodeProject(body, 15) == *d.project(), "schema15 roundtrip");
    rejects([&] { json::decodeProject(body, 14); });
    auto bad = *d.project();
    bad.levelingDecisions[1].previousEntryId = "missing";
    rejects([&] { validate(bad); });
    bad = *d.project();
    bad.levelingDecisions[1].beforePlan.reset();
    rejects([&] { validate(bad); });
    bad = *d.project();
    bad.levelingDecisions[0].recordedRevision = bad.revision + 1;
    rejects([&] { validate(bad); });
    bad = *d.project();
    bad.remainingLeveling = first.afterPlan;
    rejects([&] { validate(bad); });
    const auto entries = body["levelingDecisions"].toArray();
    for (const auto &key : {"recordedAt", "sourceRevision", "beforePlan", "reason"}) {
        auto altered = entries;
        auto e = altered[0].toObject();
        e.remove(key);
        altered[0] = e;
        rejects([&] { json::decodeLevelingDecisions(altered); });
    }
    auto malformed = entries;
    auto e = malformed[0].toObject();
    e["sourceRevision"] = 1.5;
    malformed[0] = e;
    rejects([&] { json::decodeLevelingDecisions(malformed); });
    // Old selections have no invented adoption event; the first real release anchors history.
    Project legacy = before;
    legacy.planLeveling = first.afterPlan;
    legacy.revision = first.recordedRevision;
    store.save(temp.filePath("legacy.ppm").toStdString(), legacy, {});
    Document old(store);
    old.open(temp.filePath("legacy.ppm").toStdString());
    check(nativeSchemaFor(*old.project()) == 12 && old.project()->levelingDecisions.empty(), "legacy changed");
    old.clearLeveling();
    check(old.project()->levelingDecisions.size() == 1 && old.project()->levelingDecisions[0].beforePlan,
          "legacy release not anchored");
    // The full journal rejects another transition without dropping an existing record.
    auto capped = before;
    auto entry = first;
    capped.levelingDecisions.clear();
    for (int i = 0; i < 10000; ++i) {
        entry.id = "limit-" + std::to_string(i);
        entry.previousEntryId = i ? capped.levelingDecisions.back().id : "";
        entry.sourceRevision = before.revision + std::uint64_t(i);
        entry.recordedRevision = entry.sourceRevision + 1;
        entry.action = i % 2 ? "release" : "adopt";
        entry.beforePlan = capped.planLeveling;
        entry.afterPlan = i % 2 ? std::nullopt : first.afterPlan;
        if (entry.afterPlan)
            entry.afterPlan->sourceRevision = entry.sourceRevision;
        capped.planLeveling = entry.afterPlan;
        capped.levelingDecisions.push_back(entry);
    }
    capped.revision = entry.recordedRevision;
    validate(capped);
    auto proposal = capped;
    ++proposal.revision;
    proposal.planLeveling = first.afterPlan;
    rejects([&] { appendLevelingDecision(proposal, capped, "adopt", "한도 검사"); });
    check(proposal.levelingDecisions == capped.levelingDecisions, "limit truncated ledger");
    const auto cappedPath = temp.filePath("capped.ppm").toStdString();
    store.save(cappedPath, capped, {});
    Document full(store);
    full.open(cappedPath);
    const auto original = full.snapshot();
    rejects([&] { full.adoptLeveling(selection(full)); });
    check(*full.project() == *original && !full.canUndo() && !full.dirty(), "limit partially committed document");
}
void ui(const QString &output) {
    JsonProjectStore store;
    Document d(store);
    fixture(d);
    d.adoptLeveling(selection(d));
    d.renameTask(0, "설계 변경 반영");
    d.undo();
    MainWindow window(d);
    check(window.findChild<QAction *>("levelingDecisions"), "resource menu missing history action");
    LevelingDecisionDialog dialog(d);
    dialog.show();
    QTest::qWait(30);
    auto *table = dialog.findChild<QTableView *>("levelingDecisionTable");
    auto *detail = dialog.findChild<QPlainTextEdit *>("levelingDecisionDetail");
    check(table && detail && table->model()->rowCount() == 3 && detail->isReadOnly() &&
              table->editTriggers() == QAbstractItemView::NoEditTriggers && detail->toPlainText().contains("변경 전") &&
              detail->toPlainText().contains("주간 자원 조정안"),
          "history screen missing readonly details");
    check(dialog.grab().save(output + "/history.png"), "history capture");
    dialog.resize(800, 600);
    QTest::qWait(20);
    check(dialog.grab().save(output + "/history-small.png"), "small history capture");
    d.clearLeveling();
    check(table->model()->rowCount() == 4 && detail->toPlainText().contains("해제"), "screen stale after edit");
    QTemporaryDir temp;
    d.saveAs(temp.filePath("readonly.ppm").toStdString());
    Document readonly(store);
    readonly.open(d.path(), true);
    const auto original = readonly.snapshot();
    {
        LevelingDecisionDialog reader(readonly);
        check(reader.findChild<QTableView *>("levelingDecisionTable")->model()->rowCount() == 4,
              "readonly history unavailable");
    }
    check(*readonly.project() == *original, "history viewer mutated readonly source");
    d.create("새 문서", "2026-11-02", {});
    check(table->model()->rowCount() == 0 && detail->toPlainText().isEmpty(), "old document leaked to screen");
    dialog.close();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        const auto output = QDir::currentPath() + "/leveling-decision-qa";
        // CTest may run the same fixture again; each run writes a fresh native artifact.
        const auto artifact = output + "/source.ppm";
        if (QFile::exists(artifact))
            check(QFile::remove(artifact), "remove previous generated fixture");
        lifecycle(output);
        ui(output);
        std::cout
            << "decision lifecycle, worker/cancel/session, legacy/schema15/recovery, validation/limit and UI passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
