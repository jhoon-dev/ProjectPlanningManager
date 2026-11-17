#include "application/Document.h"
#include "calculation/ResourcePlanning.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <random>
#include <numeric>
using namespace jobs;
namespace {
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected resource planning rejection");
}
void write(const QString &name, const QByteArray &bytes) {
    QFile file("resource-planning-qa/" + name);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write evidence");
}
Project fixture(TaskType type = TaskType::FixedDuration) {
    Project p;
    p.documentId = "resource-planning";
    p.name = "생산성·고유 곡선 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "시공", 960, {}}};
    p.tasks[0].taskType = type;
    if (type != TaskType::FixedDuration) {
        p.tasks[0].workDriven = true;
        p.tasks[0].workUnitMinutes = 96000;
    }
    Resource r;
    r.id = "R";
    r.name = "작업 인력";
    r.maxUnits = 200;
    r.rates = {{"2026-11-02", 600}, {"2026-11-03", 1200}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100}};
    p.taskCosts = {{"T", 0}};
    return p;
}
ResourcePlanningSubmission submission(std::string action = "estimate") {
    ResourcePlanningSubmission s;
    s.action = action;
    s.taskId = "T";
    s.resourceId = "R";
    s.reason = "물량/측정 생산성 검토";
    s.reporter = "계획 담당";
    s.recordedAt = "2026-11-09T01:00:00Z";
    s.confirmed = true;
    return s;
}
auto prepare(Document &d, ResourcePlanningSubmission s,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(ResourcePlanningEdit{std::move(s)}), c);
}
std::int64_t work(const Project &p, const std::string &resource = "R") {
    auto basis = p;
    basis.planLeveling.reset();
    basis.remainingLeveling.reset();
    basis.levelingDecisions.clear();
    const auto r = calculatePlan(basis);
    check(r.status == CalculationStatus::Valid, "valid plan");
    std::int64_t n = 0;
    for (const auto &s : TaskExecution(basis, basis.tasks[0]).spans(r.tasks[0]))
        if (s.resourceId == resource)
            n += (s.finish - s.start) * s.units;
    return n;
}
void curveCases() {
    QJsonArray cases;
    std::mt19937 random(20261109);
    const auto anchor = QDateTime::fromString("2026-11-02 00:00", "yyyy-MM-dd HH:mm");
    for (int sample = 0; sample < 72; ++sample) {
        auto p = fixture();
        p.tasks[0].durationMinutes = 80;
        p.tasks[0].calendarId = "C";
        p.tasks[0].breaks = {{"2026-11-02 09:05", "2026-11-02 09:06"}};
        p.calendars = {{"C", "20분", {1, 2, 3, 4, 5}, {{540, 560}}, {}},
                       {"R", "15분", {1, 2, 3, 4, 5}, {{542, 557}}, {"2026-11-04"}}};
        p.resources[0].calendarId = "R";
        std::vector<std::vector<std::int64_t>> available(20);
        std::int64_t relative = 0;
        for (std::int64_t at = 540; relative < 80; ++at) {
            auto time = anchor.addSecs(at * 60);
            const auto date = time.date().toString(Qt::ISODate).toStdString();
            int minute = time.time().hour() * 60 + time.time().minute();
            if (time.date().dayOfWeek() > 5 || minute < 540 || minute >= 560 || (date == "2026-11-02" && minute == 545))
                continue;
            if (minute >= 542 && minute < 557 && date != "2026-11-04")
                available[std::size_t(relative / 4)].push_back(at);
            ++relative;
        }
        std::vector<int> raw(20);
        int denominator = 0;
        for (int i = 0; i < 20; ++i)
            if (!available[i].empty()) {
                raw[i] = 1 + int(random() % 300);
                denominator += raw[i];
            }
        NativeCurve c;
        c.source = "p6-xer";
        c.externalId = std::to_string(sample + 1);
        c.name = "검증 곡선";
        c.weights = {0};
        int allocated = 0;
        std::vector<std::pair<int, int>> remainder;
        for (int i = 0; i < 20; ++i) {
            int v = std::int64_t(raw[i]) * 100000000 / denominator;
            c.weights.push_back(v);
            allocated += v;
            remainder.emplace_back(std::int64_t(raw[i]) * 100000000 % denominator, i);
        }
        std::stable_sort(remainder.begin(), remainder.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        for (int i = 0; allocated < 100000000; ++i, ++allocated)
            ++c.weights[std::size_t(remainder[i].second + 1)];
        const std::int64_t target = 1 + random() % 10000;
        std::vector<std::int64_t> amounts(20);
        std::vector<std::pair<std::int64_t, int>> residual;
        std::int64_t sum = 0;
        for (int i = 0; i < 20; ++i) {
            amounts[i] = target * c.weights[i + 1] / 100000000;
            sum += amounts[i];
            residual.emplace_back(target * c.weights[i + 1] % 100000000, i);
        }
        std::stable_sort(residual.begin(), residual.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        for (int i = 0; sum < target; ++i, ++sum)
            ++amounts[residual[i].second];
        std::map<std::int64_t, int> expected;
        for (int i = 0; i < 20; ++i) {
            const auto n = available[i].size();
            check(n || !amounts[i], "oracle zero capacity");
            for (std::size_t j = 0; j < n; ++j) {
                int rate = int(amounts[i] / std::int64_t(n) + (j < std::size_t(amounts[i] % std::int64_t(n))));
                if (rate)
                    expected[available[i][j]] = rate;
            }
        }
        const auto out = applyNativeCurve(p, "T", "R", c, target);
        const auto plan = calculatePlan(out);
        check(plan.status == CalculationStatus::Valid, "curve plan");
        std::map<std::int64_t, int> actual;
        QJsonArray spans;
        for (const auto &s : TaskExecution(out, out.tasks[0]).spans(plan.tasks[0])) {
            spans.append(QJsonObject{{"start", qint64(s.start)}, {"finish", qint64(s.finish)}, {"units", s.units}});
            for (auto at = s.start; at < s.finish; ++at) {
                check(!actual.contains(at), "overlapping curve spans");
                actual[at] = s.units;
            }
        }
        check(actual == expected && work(out) == target, "independent curve minute ledger");
        check(out.assignments[0].nativeCurve->weights == c.weights && nativeSchemaFor(out) == 36,
              "curve provenance/schema36");
        cases.append(QJsonObject{{"input", json::encodeProject(p)},
                                 {"weights",
                                  [&] {
                                      QJsonArray a;
                                      for (int w : c.weights)
                                          a.append(w);
                                      return a;
                                  }()},
                                 {"target", qint64(target)},
                                 {"output", json::encodeProject(out)},
                                 {"spans", spans}});
    }
    write("curve-cases.json", QJsonDocument(cases).toJson());
}
void run() {
    QDir().mkpath("resource-planning-qa");
    QJsonArray arithmetic;
    ProductivityRate rate{"m3", "2026-11-09", "실측 30m3 / 인시", "담당", 30000};
    for (auto quantity : {0LL, 1LL, 1000LL, 123456LL, 1000000000LL})
        for (auto r : {1LL, 3LL, 1000LL, 12500LL, 1000000000LL}) {
            auto v = rate;
            v.quantityMilliPerHour = r;
            QJsonObject row{{"quantity", qint64(quantity)}, {"rate", qint64(r)}};
            try {
                row["work"] = qint64(productivityWork(quantity, v));
            } catch (const AppError &) {
                row["rejected"] = true;
            }
            arithmetic.append(row);
        }
    write("productivity-arithmetic.json", QJsonDocument(arithmetic).toJson());
    curveCases();
    QTemporaryDir temp;
    JsonProjectStore store;
    for (auto type : {TaskType::FixedDuration, TaskType::FixedWork, TaskType::FixedUnits}) {
        auto p = fixture(type);
        const auto path = temp.filePath(QString::number(int(type)) + ".ppm").toStdString();
        store.save(path, p, {});
        Document d(store);
        d.open(path);
        d.captureBaseline("승인", "PM", "원래 계획", "2026-11-09T01:00:00Z");
        d.setStatusAt("2026-11-02 18:00");
        ProgressEntry progress;
        progress.taskId = "T";
        progress.statusAt = *d.project()->statusAt;
        progress.actualStart = "2026-11-02 09:00";
        progress.actualDurationMinutes = 480;
        progress.remainingMinutes = 480;
        progress.remainingWorkUnitMinutes = 48000;
        progress.reporter = "관측 담당";
        progress.reason = "원래 보고 보존";
        progress.recordedAt = "2026-11-09T01:00:00Z";
        d.recordProgress(progress);
        PeriodActualSubmission actual;
        actual.reporter = "관측 담당";
        actual.reason = "실측";
        actual.recordedAt = "2026-11-09T01:00:00Z";
        PeriodActualEntry fact;
        fact.taskId = "T";
        fact.resourceId = "R";
        fact.date = "2026-11-02";
        fact.workUnitMinutes = 6000;
        actual.entries = {fact};
        d.recordPeriodActuals(actual);
        CostEntry cost;
        cost.taskId = "T";
        cost.statusAt = *d.project()->statusAt;
        cost.actualMinor = 12345;
        cost.currency = "KRW";
        cost.reporter = "원가 담당";
        cost.reason = "관측 AC";
        cost.recordedAt = "2026-11-09T01:00:00Z";
        d.recordCost(cost);
        d.saveScenario("변경 전 잔여", "task-order", 30, "2026-11-09T01:00:00Z", d.project()->documentId,
                       d.project()->revision);
        const auto before = *d.project();
        auto s = submission("rate");
        s.rate = rate;
        d.resourcePlanning(s);
        check(work(*d.project()) == 96000 && d.project()->tasks == before.tasks, "catalog edited work");
        s = submission();
        s.quantityMilli = 120000;
        const auto stable = *d.project();
        auto ready = prepare(d, s);
        check(ready->state() == EditState::Ready && *d.project() == stable, "productivity preview");
        check(d.applyPreparedEdit(*ready) == EditState::Applied && work(*d.project()) == 24000, "productivity apply");
        const auto after = *d.project();
        const auto schedule = calculatePlan(after);
        check(schedule.tasks[0].finish == (type == TaskType::FixedDuration ? "2026-11-03 18:00" : "2026-11-02 14:00"),
              "task-type productivity maintenance");
        check(after.baselines == before.baselines && after.progress == before.progress &&
                  after.periodActuals == before.periodActuals && after.costEntries == before.costEntries &&
                  after.scenarios == before.scenarios,
              "observations/frozen plan changed");
        d.undo();
        check(work(*d.project()) == 96000 && !d.project()->assignments[0].productivityEstimate, "estimate undo");
        d.redo();
        d.save();
        check(store.load(d.path()).project == *d.project(), "schema36 roundtrip");
        rejects([&] { json::decodeProject(json::encodeProject(*d.project()), 35); });
        auto bad = s;
        bad.confirmed = false;
        const auto original = *d.project();
        rejects([&] { d.resourcePlanning(bad); });
        check(*d.project() == original, "unconfirmed applied");
        auto cancelled = std::make_shared<CalculationControl>();
        cancelled->cancel();
        check(prepare(d, s, cancelled)->state() == EditState::Cancelled, "productivity cancellation");
        s.quantityMilli = 60000;
        ready = prepare(d, s);
        d.renameTask(0, "변경");
        check(d.applyPreparedEdit(*ready) == EditState::Stale, "productivity stale commit");
        d.undo();
        if (type == TaskType::FixedDuration) {
            write("before.json", QJsonDocument(json::encodeProject(stable)).toJson());
            write("estimated.json", QJsonDocument(json::encodeProject(after)).toJson());
            const auto report = buildReport(
                after, {{}, {}, "budget-remaining-v1", {}, {}, "productivity", "2026-11-09T01:00:00Z", false});
            exportXlsx(report, temp.path() + "/report.xlsx");
            PdfOptions pdf;
            pdf.font = QGuiApplication::font();
            check(exportPdf(report, temp.path() + "/report.pdf", ReportFamily::All, pdf) > 0, "productivity PDF");
            for (const auto &name : {"report.xlsx", "report.pdf"}) {
                QFile f(temp.path() + "/" + name);
                check(f.open(QIODevice::ReadOnly), "report read");
                write(name, f.readAll());
            }
            auto curve = submission("curve");
            curve.curve = mspWorkContour(6);
            d.resourcePlanning(curve);
            write("curve-estimated.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
            const auto curveReport = buildReport(
                *d.project(), {{}, {}, "budget-remaining-v1", {}, {}, "curve-proof", "2026-11-09T01:00:00Z", false});
            exportXlsx(curveReport, temp.path() + "/curve-report.xlsx");
            check(exportPdf(curveReport, temp.path() + "/curve-report.pdf", ReportFamily::All, pdf) > 0,
                  "curve provenance PDF");
            for (const auto &name : {"curve-report.xlsx", "curve-report.pdf"}) {
                QFile f(temp.path() + "/" + name);
                check(f.open(QIODevice::ReadOnly), "curve report read");
                write(name, f.readAll());
            }
            CurrencyConversionSubmission fx;
            fx.rate = {"KRW", "JPY", "2026-11-09", "Archive preservation test", "exact-minor-v1", 1, 1};
            fx.reporter = "PM";
            fx.reason = "표현 통화 전환";
            fx.recordedAt = "2026-11-09T01:00:00Z";
            fx.confirmed = true;
            d.convertCurrency(fx);
            auto clear = submission("curve");
            d.resourcePlanning(clear);
            clear = submission("estimate");
            d.resourcePlanning(clear);
            clear = submission("rate");
            d.resourcePlanning(clear);
            check(!hasResourcePlanning(*d.project()) && nativeSchemaFor(*d.project()) == 36,
                  "archived-only metadata schema36");
            const auto archivedPath = temp.filePath("archived-copy.ppm").toStdString();
            d.saveAs(archivedPath);
            check(store.load(archivedPath).project == *d.project() && store.load(archivedPath)
                                                                          .project.currencyConversions[0]
                                                                          .before->assignments[0]
                                                                          .nativeCurve.has_value(),
                  "currency snapshot metadata replay");
            QFile archive(QString::fromStdString(archivedPath));
            check(archive.open(QIODevice::ReadOnly), "archive read");
            write("archived-only.ppm", archive.readAll());
            d.renameTask(0, "복구 대상");
            JsonRecoveryStore recovery(temp.path() + "/recovery");
            auto session = recovery.newSession();
            session->checkpoint(d.recoverySnapshot("2026-11-09T01:00:00Z"));
            session.reset();
            const auto scan = recovery.scan();
            check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema36 recovery scan");
            auto claim = recovery.claim(scan.candidates[0]);
            Document restored(store);
            restored.recover(claim->snapshot);
            check(restored.project()->currencyConversions == d.project()->currencyConversions,
                  "recovered metadata archives");
        }
        d.open(d.path(), true);
        rejects([&] { d.resourcePlanning(s); });
    }
    auto p = fixture();
    for (auto type : {TaskType::FixedDuration, TaskType::FixedWork, TaskType::FixedUnits}) {
        auto multiple = fixture(type);
        auto other = multiple.resources[0];
        other.id = "S";
        multiple.resources.push_back(other);
        multiple.assignments.push_back({"T", "S", 100});
        multiple.resources[0].productivity = rate;
        const auto file = temp.filePath("multi-" + QString::number(int(type)) + ".ppm").toStdString();
        store.save(file, multiple, {});
        Document d(store);
        d.open(file);
        const auto otherWork = work(*d.project(), "S");
        auto s = submission();
        s.quantityMilli = 120000;
        if (type != TaskType::FixedDuration)
            rejects([&] { d.resourcePlanning(s); });
        s.keepDuration = true;
        d.resourcePlanning(s);
        check(work(*d.project()) == 24000 && work(*d.project(), "S") == otherWork,
              "multiple-resource work conservation");
    }
    auto leveled = fixture();
    leveled.revision = 1;
    store.save(temp.filePath("leveled.ppm").toStdString(), leveled, {});
    Document adoption(store);
    adoption.open(temp.filePath("leveled.ppm").toStdString());
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(*adoption.project());
    selection.adoptedAt = "2026-11-09T01:00:00Z";
    selection.reason = "review";
    selection.sourceDocumentId = adoption.project()->documentId;
    selection.sourceRevision = adoption.project()->revision;
    adoption.adoptLeveling(selection);
    const auto adopted = adoption.project()->planLeveling;
    auto rateEdit = submission("rate");
    rateEdit.rate = rate;
    adoption.resourcePlanning(rateEdit);
    check(adoption.project()->planLeveling == adopted, "catalog invalidated adopted schedule");
    auto estimateEdit = submission();
    estimateEdit.quantityMilli = 120000;
    adoption.resourcePlanning(estimateEdit);
    check(!adoption.project()->planLeveling, "physical demand kept stale adoption");
    auto legacy = fixture();
    legacy.calculationProfile = "native-fs0-workminutes-v1";
    legacy.resources[0].productivity = rate;
    const auto legacyFile = temp.filePath("legacy.ppm").toStdString();
    store.save(legacyFile, legacy, {});
    Document old(store);
    old.open(legacyFile);
    auto upgrade = submission();
    upgrade.quantityMilli = 120000;
    old.resourcePlanning(upgrade);
    check(work(*old.project()) == 24000 && isEffort(*old.project()), "legacy plan productivity upgrade");
    for (int code = 0; code <= 7; ++code) {
        auto curve = mspWorkContour(code);
        check(std::accumulate(curve.weights.begin(), curve.weights.end(), 0) == 100000000, "MS preset area");
        auto out = applyNativeCurve(p, "T", "R", curve);
        check(work(out) == 96000, "preset total");
        const auto xml = exportProjectXml(out);
        const auto x = importProjectXml(xml.xml, "curve.xml");
        check(work(x.project, x.project.resources[0].id) == 96000, "MS XML curve readback");
        const auto xer = exportPrimaveraXer(out);
        const auto y = importPrimaveraXer(xer.xer, "curve.xer");
        check(work(y.project, y.project.resources[0].id) == 96000, "P6 curve readback");
        if (code == 6) {
            write("curve.xml", xml.xml);
            write("curve.xer", xer.xer);
            write("curved.json", QJsonDocument(json::encodeProject(out)).toJson());
        }
    }
    auto precision = mspWorkContour(0);
    precision.source = "p6-xer";
    precision.mspCode = -1;
    precision.weights.assign(21, 0);
    precision.weights[1] = 33333333;
    precision.weights[2] = 66666667;
    const auto precise = applyNativeCurve(p, "T", "R", precision);
    const auto preciseRead = importPrimaveraXer(exportPrimaveraXer(precise).xer, "precision.xer");
    check(preciseRead.project.assignments[0].nativeCurve->weights == precision.weights,
          "six decimal external percentages preserved");
    auto custom = p;
    custom.assignments[0].periods = {{0, 120, 0}, {120, 480, 75}, {480, 960, 150}};
    const auto xml = exportProjectXml(custom);
    auto read = importProjectXml(xml.xml, "manual.xml");
    check(work(read.project, read.project.resources[0].id) == work(custom), "custom timephased total");
    write("manual.xml", xml.xml);
    write("manual.json", QJsonDocument(json::encodeProject(custom)).toJson());
    auto zero = p;
    zero.assignments[0].periods = {{0, 960, 0}};
    check(work(importProjectXml(exportProjectXml(zero).xml, "zero.xml").project, "xml-resource-1") == 0,
          "explicit zero work XML");
    auto zeroGoal = fixture(TaskType::FixedWork);
    zeroGoal.tasks[0].workUnitMinutes = 0;
    zeroGoal.assignments[0].productivityEstimate =
        ProductivityEstimate{rate, 0, 0, "zero quantity", "2026-11-09T01:00:00Z", true, "work-duration-v1", "PM"};
    check(work(importProjectXml(exportProjectXml(zeroGoal).xml, "zero-goal.xml").project, "xml-resource-1") == 0,
          "zero productivity XML milestone");
    const auto zeroXer = importPrimaveraXer(exportPrimaveraXer(zeroGoal).xer, "zero-goal.xer");
    check(work(zeroXer.project, zeroXer.project.resources[0].id) == 0, "zero productivity XER milestone");
    auto invalid = rate;
    invalid.quantityMilliPerHour = 0;
    rejects([&] { productivityWork(1, invalid); });
    invalid = rate;
    invalid.unit = "";
    rejects([&] { productivityWork(1, invalid); });
    auto c = mspWorkContour(6);
    c.weights[0] = 1;
    rejects([&] { applyNativeCurve(p, "T", "R", c); });
    std::cout << "PASS resource productivity/curves: integer quantity-to-work, 72 independent minute curves, task-type "
                 "maintenance, original plans/facts, schema36, cancel/stale/readonly, named and custom XML/XER "
                 "readback, real report artifacts\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setFont(QFont("Malgun Gothic", 9));
    try {
        run();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
