#include "application/Document.h"
#include "domain/Activities.h"
#include "domain/Classification.h"
#include "calculation/ActivitySummary.h"
#include "calculation/ProgressPolicies.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/ProjectXml.h"
#include "persistence/ProgressPolicyJson.h"
#include "reporting/ReportExport.h"
#include "ProgressPolicyFixture.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &call) {
    try {
        call();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
const TaskSchedule &row(const ScheduleResult &r, const std::string &id) {
    for (const auto &t : r.tasks)
        if (t.taskId == id)
            return t;
    throw std::runtime_error("row missing");
}
ProgressPolicySelection choice(const Project &p, const std::string &policy) {
    return {policy, "PM", "2026-11-05T00:00:00Z", "endpoint policy", p.documentId, p.revision};
}
void actualDates() {
    auto p = progress_fixture::small(effortProfile);
    p.tasks[1].predecessors.push_back("D");
    p.tasks[1].dependencyRules.push_back({"D", DependencyType::FF, 0});
    const auto before = p;
    const auto comparison = compareProgressPolicies(p);
    check(comparison.computed && comparison.actualDates.status == CalculationStatus::Valid, "actual dates comparison");
    const auto a = row(comparison.retained, "B"), b = row(comparison.overridden, "B"),
               c = row(comparison.actualDates, "B");
    check(a.finish == "2026-11-04 14:00" && b.finish == "2026-11-03 14:00" && c.finish == "2026-11-03 18:00",
          "A/B/C independent golden endpoints");
    check(c.start == "2026-11-02 14:00" && c.remainingStart == "2026-11-03 14:00",
          "actual start moved or finish relationship lost");
    check(row(comparison.actualDates, "E").finish == "2026-11-02 18:00", "completed actual moved");
    check(p == before, "comparison modified source");
    p.progressPolicy = choice(p, actualDatesProgressPolicy);
    auto graph = progressPolicyGraph(p);
    check(graph.tasks[1].predecessors == std::vector<std::string>{"D"} &&
              graph.tasks[1].dependencyRules[0].type == DependencyType::FF,
          "historical start/future finish mask");
    check(calculate(p).tasks == comparison.actualDates.tasks, "selected actual dates differs from preview");
    check(nativeSchemaFor(p) == 19, "actual dates schema");
    auto object = json::encodeProject(p);
    check(json::decodeProject(object, 19) == p, "actual dates roundtrip");
    rejects([&] { json::decodeProject(object, 18); });
    p.tasks[0].predecessors = {"B"};
    check(calculate(p).status == CalculationStatus::Conflict, "actual date mask bypassed source cycle");
    write("activity-policy-comparison.json", progressPolicyJson(comparison, "2026-11-05T00:00:00Z"));
}
void activities() {
    QTemporaryDir temp;
    JsonProjectStore files;
    Project initial;
    initial.documentId = "activity-fixture";
    initial.name = "지원 활동 검증";
    initial.startDate = "2026-11-02";
    initial.calculationProfile = effortProfile;
    initial.wbsNodes = {{"root", "현장", ""}, {"child", "구역", "root"}};
    initial.tasks = {{"A", "설계", 480, {}, "root"}, {"B", "시공", 480, {"A"}, "child"}, {"C", "독립", 60, {}}};
    initial.taskCosts = {
        {"A", 100000, "uniform", "physical"}, {"B", 0, "uniform", "physical"}, {"C", 0, "uniform", "physical"}};
    {
        Document guard(files);
        guard.create("Steps 보호", "2026-11-02", {"보고 작업"});
        guard.setDuration(0, 60);
        guard.setStatusAt("2026-11-02 18:00");
        ActivityDefinition d;
        d.name = "Steps";
        d.taskId = guard.project()->tasks[0].id;
        d.steps = {{"step", "검토", 1}};
        const auto definitionId = guard.putActivity(d);
        StepReport r;
        r.taskId = d.taskId;
        r.reporter = "PM";
        r.reason = "보호 검사";
        r.recordedAt = "2026-11-05T00:00:00Z";
        r.steps = {{d.steps[0], 5000}};
        guard.recordSteps(r);
        guard.removeActivity(definitionId);
        check(!guard.previewRemoveTasks({d.taskId}).protectedTaskIds.empty(),
              "Steps-only facts omitted from deletion guard");
        rejects([&] { guard.removeTasks({d.taskId}); });
    }
    const auto path = temp.filePath("source.ppm").toStdString();
    files.save(path, initial, {});
    Document doc(files);
    doc.open(path);
    const auto work = calculatePlan(*doc.project()).tasks;
    ActivityDefinition loe;
    loe.kind = "loe";
    loe.name = "현장 관리";
    loe.startTaskId = "A";
    loe.finishTaskId = "B";
    const auto loeId = doc.putActivity(loe);
    check(doc.dirty(), "support edit not dirty");
    doc.undo();
    check(!doc.dirty() && doc.project()->activities.empty(), "support undo");
    doc.redo();
    ActivityDefinition summary;
    summary.kind = "wbs-summary";
    summary.name = "현장 요약";
    summary.wbsId = "root";
    doc.putActivity(summary);
    auto interval = activityIntervals(*doc.project(), calculate(*doc.project()));
    check(interval.size() == 2 && interval[0].start == "2026-11-02 09:00" && interval[0].finish == "2026-11-03 18:00" &&
              interval[0].durationMinutes == 960,
          "LOE golden interval");
    check(interval[1].sourceTasks == 2 && interval[1].durationMinutes == 960, "nested WBS summary/source exclusion");
    check(calculatePlan(*doc.project()).tasks == work && doc.project()->tasks.size() == 3 &&
              doc.project()->assignments.empty(),
          "support altered CPM/work counts");
    auto stale = calculate(*doc.project());
    ++stale.inputRevision;
    check(!activityIntervals(*doc.project(), stale)[0].start, "stale interval displayed");
    auto invalid = loe;
    invalid.id = loeId;
    invalid.startTaskId = "missing";
    const auto valid = *doc.project();
    rejects([&] { doc.putActivity(invalid); });
    check(*doc.project() == valid, "invalid support partial edit");
    invalid.startTaskId = "B";
    invalid.finishTaskId = "A";
    doc.putActivity(invalid);
    check(!activityIntervals(*doc.project(), calculate(*doc.project()))[0].start, "negative LOE silently clamped");
    doc.undo();
    ActivityDefinition definition;
    definition.name = "설계 Steps";
    definition.taskId = "A";
    definition.steps = {{"s1", "도면", 1}, {"s2", "승인", 3}};
    const auto stepsId = doc.putActivity(definition);
    definition.id = stepsId;
    const auto duplicated = doc.duplicateTasks({"A"});
    check(doc.project()->activities.size() == 4 && doc.project()->activities.back().taskId == duplicated[0] &&
              doc.project()->activities.back().steps == definition.steps,
          "task duplication lost Steps definitions");
    doc.undo();
    check(doc.project()->activities.size() == 3 && doc.project()->tasks.size() == 3,
          "duplicate undo lost support state");
    const auto baseline = doc.captureBaseline("승인 계획", "PM", "test", "2026-11-02T00:00:00Z");
    check(doc.project()->baselines[0].input.activities.size() == 3, "baseline lost definitions");
    doc.setStatusAt("2026-11-02 18:00");
    auto fact = progress_fixture::entry(*doc.project(), "A", 480, 240, 240, "2026-11-02 09:00");
    doc.recordProgress(fact);
    StepReport report;
    report.taskId = "A";
    report.reporter = "PM";
    report.reason = "weighted test";
    report.recordedAt = "2026-11-05T00:00:00Z";
    report.steps = {{definition.steps[0], 10000}, {definition.steps[1], 0}};
    check(!activityProgress(*doc.project(), "A").physical, "missing Steps report became zero");
    doc.recordSteps(report);
    check(!doc.canUndo(), "immutable Steps report was undoable");
    auto metrics = activityProgress(*doc.project(), "A");
    check(metrics.physical == 2500 && metrics.duration == 5000 && !metrics.work, "physical/duration/work conflated");
    check(analyzeCosts(*doc.project(), baseline).evm.ev == 25000, "Steps did not drive physical EV");
    const auto frozen = doc.project()->stepReports;
    definition.steps[0].weight = 7;
    definition.steps[1].weight = 1;
    doc.putActivity(definition);
    check(stepsPhysicalAsOf(*doc.project(), "A", *doc.project()->statusAt) == 2500 &&
              doc.project()->stepReports == frozen,
          "weight edit rewrote old report");
    doc.undo();
    check(doc.project()->stepReports == frozen, "definition undo removed reports");
    doc.redo();
    doc.recordSteps(report);
    check(activityProgress(*doc.project(), "A").physical == 8750 &&
              analyzeCosts(*doc.project(), baseline).evm.ev == 87500,
          "current definition was not frozen into correction");
    check(doc.project()->stepReports[0].steps[0].step.weight == 1 &&
              doc.project()->stepReports[1].steps[0].step.weight == 7,
          "report weights mutated");
    const auto beforeInvalid = *doc.project();
    auto wrong = report;
    wrong.steps[0].basisPoints = 10001;
    rejects([&] { doc.recordSteps(wrong); });
    check(*doc.project() == beforeInvalid, "invalid report partial mutation");
    doc.setStatusAt("2026-11-02 17:00");
    check(!activityProgress(*doc.project(), "A").physical, "future Steps report leaked backward");
    doc.setStatusAt("2026-11-02 18:00");
    CostEntry manual;
    manual.taskId = "A";
    manual.actualMinor = 0;
    manual.physicalBasisPoints = 1000;
    manual.recordedAt = "2026-11-05T00:00:00Z";
    manual.reporter = "PM";
    manual.reason = "원천 분리";
    doc.recordCost(manual);
    check(analyzeCosts(*doc.project(), baseline).evm.ev == 87500, "manual and Steps physical were added together");
    definition.physicalFromSteps = false;
    doc.putActivity(definition);
    check(activityProgress(*doc.project(), "A").physical == 1000 &&
              analyzeCosts(*doc.project(), baseline).evm.ev == 10000,
          "manual physical source selection");
    doc.undo();
    {
        auto withWork = *doc.project();
        withWork.progress.back().remainingWorkUnitMinutes = 30000;
        Resource resource;
        resource.id = "R";
        resource.name = "인력";
        withWork.resources.push_back(resource);
        withWork.assignments.push_back({"A", "R", 100, {}});
        PeriodActualEntry actual;
        actual.id = "work-report";
        actual.taskId = "A";
        actual.resourceId = "R";
        actual.resourceType = "labor";
        actual.date = "2026-11-02";
        actual.statusAt = *withWork.statusAt;
        actual.recordedAt = "2026-11-05T00:00:00Z";
        actual.reporter = "PM";
        actual.reason = "공수 분리";
        actual.sourceDocumentId = withWork.documentId;
        actual.currency = withWork.currency;
        actual.recordedRevision = withWork.revision;
        actual.workUnitMinutes = 30000;
        withWork.periodActuals.push_back(actual);
        validate(withWork);
        check(activityProgress(withWork, "A").work == 5000 && activityProgress(withWork, "A").physical == 8750,
              "work percent reused physical or duration");
    }
    auto full = json::encodeProject(*doc.project());
    check(json::decodeProject(full, 19) == *doc.project(), "schema19 roundtrip");
    rejects([&] { json::decodeProject(full, 18); });
    auto malformed = full;
    auto definitions = malformed["activities"].toArray();
    auto first = definitions[0].toObject();
    first["unknown"] = true;
    definitions[0] = first;
    malformed["activities"] = definitions;
    rejects([&] { json::decodeProject(malformed, 19); });
    auto output = QDir::currentPath() + "/activity-extension-qa";
    check(QDir().mkpath(output), "output directory");
    const auto copy = output + "/activities.ppm";
    if (QFile::exists(copy))
        check(QFile::remove(copy), "remove previous fixture");
    doc.saveAs(copy.toStdString());
    check(files.load(copy.toStdString()).project == *doc.project(), "save/readback");
    Document readOnly(files);
    readOnly.open(copy.toStdString(), true);
    rejects([&] { readOnly.putActivity(loe); });
    rejects([&] { readOnly.recordSteps(report); });
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    RecoverySnapshot snapshot;
    snapshot.project = *doc.project();
    snapshot.capturedAt = "2026-11-05T00:00:00Z";
    session->checkpoint(snapshot);
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema19 recovery scan");
    auto claim = recovery.claim(scan.candidates[0]);
    check(claim->snapshot.project == *doc.project(), "recovery lost activity definitions/reports");
    claim->session->retire();
    ReportOptions options;
    options.baselineId = baseline;
    options.reportId = "activities";
    options.generatedAt = "2026-11-05T00:00:00Z";
    const auto rendered = buildReport(*doc.project(), options);
    check(std::any_of(rendered.tables.begin(), rendered.tables.end(),
                      [](const auto &t) { return t.id == "supportActivities" && t.rows.size() == 2; }),
          "support report table");
    for (const auto *name : {"activities.xlsx", "activities.pdf"}) {
        const auto file = output + '/' + name;
        if (QFile::exists(file))
            check(QFile::remove(file), "remove old report");
    }
    exportXlsx(rendered, output + "/activities.xlsx");
    check(exportPdf(rendered, output + "/activities.pdf") > 0, "report PDF");
    check(QJsonDocument(exportPrimaveraXer(*doc.project()).report).toJson().contains("PROJECT/activities"),
          "XER silent support loss");
    check(QJsonDocument(exportProjectXml(*doc.project()).report).toJson().contains("/Project/Activities"),
          "XML silent support loss");
    write(output + "/expected.json", QJsonDocument(QJsonObject{{"schema", 19},
                                                               {"stepReports", 2},
                                                               {"physicalBasisPoints", 8750},
                                                               {"durationBasisPoints", 5000},
                                                               {"EV", 87500},
                                                               {"supportActivities", 2},
                                                               {"originalTasks", 3},
                                                               {"firstReportWeight", 1},
                                                               {"correctedWeight", 7}})
                                         .toJson());
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (QFileInfo::exists("C:/Windows/Fonts/malgun.ttf")) {
        const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
        if (font >= 0)
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    }
    try {
        actualDates();
        activities();
        std::cout << "PASS: Steps/LOE/WBS Summary/Actual Dates, EVM, archives/recovery, schema19 and report exports\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (const auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &v : a->diagnostic().arguments)
                std::cerr << v << '\n';
        return 1;
    }
}
