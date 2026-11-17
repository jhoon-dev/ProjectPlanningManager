#include "ExpandedRepartitionFixture.h"
#include "RepartitionFloatFixture.h"
#include "ProgressPolicyFixture.h"
#include "application/Document.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "calculation/Tracking.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include "ui/LevelingWitnessDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTableView>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <map>
#include <tuple>
using namespace jobs;
namespace {
void check(bool v, const std::string &message) {
    if (!v)
        throw std::runtime_error(message);
}
ScheduleResult analyze(const Project &p, int version = 5) {
    LevelingFloatScope scope(version);
    auto r = calculatePlan(p);
    check(r.status == CalculationStatus::Valid, "invalid fixture");
    r.resourceLeveled = true;
    calculateLevelingFloat(p, r);
    return r;
}
QJsonObject measure(int count, bool separate) {
    const auto p = expanded_fixture::chain(count, 2, separate);
    const auto original = calculatePlan(p);
    QElapsedTimer timer;
    timer.start();
    const auto r = analyze(p);
    const double ms = double(timer.nsecsElapsed()) / 1000000;
    check(r.levelingFloatCalculated, r.levelingFloatReason);
    check(bool(r.levelingWitnesses), "structured witnesses missing");
    QJsonArray rows, proofs, dateRecords;
    std::map<std::tuple<std::string, std::string, std::string>, int> dateIds;
    for (std::size_t i = 0; i < r.tasks.size(); ++i) {
        const auto &row = r.tasks[i];
        const auto moving = i < std::size_t(count);
        check(row.totalFloatMinutes == (moving ? 2 : 0) &&
                  row.freeFloatMinutes == (moving && (separate || i == std::size_t(count - 1)) ? 2 : 0) &&
                  row.critical == !moving,
              "unit-chain float/critical mismatch");
        check(row.start == original.tasks[i].start && row.finish == original.tasks[i].finish, "forward dates changed");
        rows.append(QJsonObject{{"id", QString::fromStdString(row.taskId)},
                                {"start", QString::fromStdString(row.start)},
                                {"finish", QString::fromStdString(row.finish)},
                                {"total", double(row.totalFloatMinutes)},
                                {"free", double(row.freeFloatMinutes)},
                                {"critical", row.critical}});
    }
    for (const auto &proof : r.levelingWitnesses->tasks) {
        QJsonArray dates;
        for (const auto &d : proof.dates) {
            const auto [it, inserted] = dateIds.emplace(std::tuple{d.taskId, d.start, d.finish}, dateRecords.size());
            if (inserted)
                dateRecords.append(QJsonObject{{"id", QString::fromStdString(d.taskId)},
                                               {"start", QString::fromStdString(d.start)},
                                               {"finish", QString::fromStdString(d.finish)}});
            dates.append(it->second);
        }
        proofs.append(QJsonObject{{"id", QString::fromStdString(proof.taskId)}, {"dates", dates}});
    }
    const auto diag = std::find_if(r.diagnostics.begin(), r.diagnostics.end(),
                                   [](const auto &d) { return d.code == "LEVELING_REPARTITION_COMPONENT_POLICY"; });
    check(diag != r.diagnostics.end() && diag->arguments[0] == std::to_string(separate ? count : 1) &&
              diag->arguments[1] == std::to_string(separate ? 1 : count),
          "unsafe or missing component decomposition");
    check(r.levelingWitnesses->tasks.size() == std::size_t(count * (separate ? 2 : 1)), "witness count");
    std::cout << "Expanded " << count << (separate ? " independent units" : " coupled tasks") << ": " << ms << "ms\n";
    return {{"movable", count}, {"separate", separate}, {"analysis_ms", ms},
            {"rows", rows},     {"proofs", proofs},     {"date_records", dateRecords}};
}
void contracts() {
    LevelingFloatScope historical(5);
    auto p = expanded_fixture::chain(129);
    const auto legacy = analyze(p, 4);
    const auto now = analyze(p);
    check(!legacy.levelingFloatCalculated && legacy.levelingFloatReason.find("128") != std::string::npos &&
              now.levelingFloatCalculated,
          "legacy 1.4 cap changed or 129 unsupported");
    auto small = expanded_fixture::chain(10);
    check(analyze(small, 4).tasks == analyze(small).tasks, "existing numerical contract changed");
    for (const auto type : {DependencyType::FS, DependencyType::SS, DependencyType::FF, DependencyType::SF})
        for (const auto lag : {-1, 1}) {
            auto variant = expanded_fixture::chain(3);
            variant.resources[0].maxUnits = 300;
            variant.tasks[1].dependencyRules = {{"T0", type, lag}};
            variant.tasks[2].dependencyRules = {{"T1", type, lag}};
            const auto before = analyze(variant, 4), after = analyze(variant);
            check(before.levelingFloatCalculated && after.levelingFloatCalculated && before.tasks == after.tasks,
                  "four-relation candidate partition changed native values");
        }
    auto aggregate = small;
    aggregate.tasks.resize(3);
    aggregate.taskCosts.clear();
    aggregate.assignments.resize(3);
    for (int i = 0; i < 3; ++i) {
        aggregate.tasks[i].predecessors.clear();
        aggregate.assignments[i].units = 50;
    }
    aggregate.tasks.push_back({"H", "완료 상한", 3, {}});
    const auto summed = levelResources(aggregate, "task-order", 260).leveled;
    check(summed.levelingFloatCalculated && summed.tasks[0].totalFloatMinutes == 1 &&
              summed.tasks[1].totalFloatMinutes == 2 && summed.tasks[2].totalFloatMinutes == 1,
          "aggregate capacity split unsafely");
    check(std::any_of(summed.diagnostics.begin(), summed.diagnostics.end(),
                      [](const auto &d) {
                          return d.code == "LEVELING_REPARTITION_COMPONENT_POLICY" &&
                                 d.arguments == std::vector<std::string>{"1", "3"};
                      }),
          "three-way potential demand was separated by a pairwise capacity test");
    auto split = repartition_fixture::project();
    for (int i = 0; i < 130; ++i) {
        const auto id = "P" + std::to_string(i);
        Task t{id, id, 1, {}};
        t.constraintType = ConstraintType::FNLT;
        t.constraintAt = "2026-11-02 09:03";
        split.tasks.push_back(t);
        split.tasks[0].predecessors.push_back(id);
        split.tasks[0].dependencyRules.push_back({id, DependencyType::SS, 0});
    }
    // Reuse a valid interleaved forward placement, preserving the original priority outcome.
    auto crossed = levelResources(repartition_fixture::project(), "task-order", 260).leveled;
    auto extras = calculatePlan(split);
    crossed.tasks.insert(crossed.tasks.end(), extras.tasks.begin() + 3, extras.tasks.end());
    calculateLevelingFloat(split, crossed);
    check(crossed.levelingFloatCalculated, crossed.levelingFloatReason);
    check(crossed.tasks[0].totalFloatMinutes == 660 && crossed.tasks[1].totalFloatMinutes == 540 &&
              crossed.tasks[1].freeFloatMinutes == 60,
          "large crossed split changed core float");
    for (std::size_t i = 3; i < crossed.tasks.size(); ++i)
        check(crossed.tasks[i].totalFloatMinutes == 2 && crossed.tasks[i].freeFloatMinutes == 0,
              "logical peers not jointly scheduled");
    auto future = expanded_fixture::chain(129);
    future.statusAt = "2026-11-02 09:00";
    future.progress = {progress_fixture::entry(future, "T0", 1, 0, 1, "2026-11-02 09:00")};
    const auto forecast = levelResources(future, "task-order", 260).leveled;
    check(forecast.levelingFloatCalculated && forecast.tasks[0].start == "2026-11-02 09:00" &&
              forecast.tasks[0].remainingStart == "2026-11-02 09:00" && forecast.tasks[0].totalFloatMinutes == 2,
          "actual start changed in large forecast");
    auto limit = expanded_fixture::chain(129);
    limit.resources[0].maxUnits = 12900;
    limit.tasks.back().durationMinutes = 2000;
    for (int i = 0; i < 129; ++i)
        limit.tasks[i].predecessors.clear();
    const auto bounded = analyze(limit);
    check(bounded.status == CalculationStatus::Valid && !bounded.levelingFloatCalculated &&
              !bounded.levelingWitnesses && bounded.resourceOrderLinks.empty() &&
              bounded.levelingFloatReason.find("250,000") != std::string::npos,
          "global candidate budget not atomic");
    check(std::none_of(bounded.tasks.begin(), bounded.tasks.end(), [](const auto &r) { return r.critical; }),
          "uncertified critical flags");
    auto repeated = now;
    calculateLevelingFloat(p, repeated);
    check(repeated.tasks == now.tasks && repeated.diagnostics.size() == now.diagnostics.size(),
          "repeated decomposition accumulated data");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        calculateRepartitionLevelingFloat(p, repeated);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && repeated.tasks == now.tasks, "cancelled calculation changed dates");
}
void lifecycleAndOutput() {
    auto p = expanded_fixture::chain(129);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(p);
    selection.adoptedAt = "2026-11-04T00:00:00Z";
    selection.reason = "128개 초과 검토";
    selection.sourceDocumentId = p.documentId;
    selection.sourceRevision = p.revision;
    p.planLeveling = selection;
    LevelingFloatScope old(4);
    auto previous = prepareBaseline(p, "기존 제한 기준선", "PM", "보존", "2026-11-04T00:00:00Z");
    previous.id = "old-4";
    previous.version = 1;
    LevelingFloatScope current(5);
    p.baselines.push_back(previous);
    ++p.revision;
    auto next = prepareBaseline(p, "확장 기준선", "PM", "검증", "2026-11-04T00:00:00Z");
    next.id = "new-5";
    next.version = 2;
    p.baselines.push_back(next);
    ++p.revision;
    check(previous.engineVersion == "jobs-adopted-leveling-1.4" && next.engineVersion == "jobs-adopted-leveling-1.5",
          "baseline engine identity");
    verifyBaseline(previous);
    verifyBaseline(next);
    JsonProjectStore store;
    QFile::remove("expanded-repartition-float-qa/selected.ppm");
    store.save("expanded-repartition-float-qa/selected.ppm", p, {});
    Document doc(store);
    doc.open("expanded-repartition-float-qa/selected.ppm", true);
    check(doc.project()->baselines[0] == previous && calculatePlan(*doc.project()).tasks == next.schedule,
          "archive/reopen changed old or current schedule");
    const auto schedule = calculatePlan(*doc.project());
    LevelingWitnessDialog view(*doc.project(), schedule, "T0", "129개 결합 작업");
    view.show();
    QApplication::processEvents();
    check(view.findChild<QTableView *>("witnessDates")->model()->rowCount() == 129,
          "full structured evidence lost after preview truncation");
    check(view.grab().save("expanded-repartition-float-qa/witness-129.png"), "large witness screenshot");
    view.close();
    ReportOptions options;
    options.reportId = "expanded-repartition";
    options.generatedAt = "2026-11-04T00:00:00Z";
    const auto report = buildReport(*doc.project(), options);
    QFile::remove("expanded-repartition-float-qa/report.xlsx");
    exportXlsx(report, "expanded-repartition-float-qa/report.xlsx");
    PdfOptions pdf;
    pdf.font = QApplication::font();
    QFile::remove("expanded-repartition-float-qa/report.pdf");
    exportPdf(report, "expanded-repartition-float-qa/report.pdf", ReportFamily::All, pdf);
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
        QDir().mkpath("expanded-repartition-float-qa");
        QJsonArray cases;
        cases.append(measure(129, false));
        cases.append(measure(512, false));
        cases.append(measure(1000, true));
        contracts();
        lifecycleAndOutput();
        QFile file("expanded-repartition-float-qa/metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write");
        file.write(QJsonDocument(QJsonObject{{"engine", "jobs-adopted-leveling-1.5"}, {"cases", cases}}).toJson());
        std::cout << "Expanded repartition, resource coupling, limits, legacy baselines, UI and reports passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
