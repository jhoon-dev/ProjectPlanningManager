#include "application/Document.h"
#include "calculation/Reports.h"
#include "calculation/WorkCalendar.h"
#include "persistence/JsonProjectStore.h"
#include "ui/ComparisonDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <functional>
#include <iostream>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void waitFor(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 15000)
        QTest::qWait(10);
    check(ready(), "display computation timed out");
}
QByteArray bytes(const std::string &path) {
    QFile file(QString::fromStdString(path));
    check(file.open(QIODevice::ReadOnly), "fixture read failed");
    return file.readAll();
}
void splitDelay(ppm::JsonProjectStore &store, bool reverse, bool explicitCalendar, bool forecast, bool effort = true,
                bool split = true, int plainMinutes = 1440) {
    ppm::Document doc(store);
    doc.create("Split delay", "2026-10-26",
               reverse ? std::vector<std::string>{"B plain", "A split"}
                       : std::vector<std::string>{"A split", "B plain"});
    const std::size_t ai = reverse ? 1 : 0, bi = reverse ? 0 : 1;
    const auto aid = doc.project()->tasks[ai].id, bid = doc.project()->tasks[bi].id;
    doc.setDuration(ai, 480);
    doc.setDuration(bi, plainMinutes);
    if (explicitCalendar) {
        const auto calendar = doc.putCalendar({"day", "Shared day", {1, 2, 3, 4, 5}, {{540, 720}, {780, 1080}}, {}});
        doc.setTaskCalendar(ai, calendar);
        doc.setTaskCalendar(bi, calendar);
    }
    const auto rid = doc.putResource({{}, "R", "labor", 100, {}});
    doc.setAssignments(aid, {{aid, rid, 100}});
    doc.setAssignments(bid, {{bid, rid, 100}});
    if (effort) {
        doc.setTaskEffort(ai, ppm::TaskType::FixedDuration, false, 480, {},
                          split ? std::vector<ppm::TaskBreak>{{"2026-10-26 13:00", "2026-10-27 13:00"}}
                                : std::vector<ppm::TaskBreak>{});
        doc.setTaskEffort(bi, ppm::TaskType::FixedDuration, false, plainMinutes, {}, {});
    }
    if (forecast)
        doc.setStatusAt("2026-10-26 09:00");
    const auto source = *doc.project();
    const auto level = ppm::levelResources(source, "shortest-first", 20);
    check(level.leveled.status == ppm::CalculationStatus::Valid, "delay fixture did not level");
    const auto &before = level.original.tasks[bi], &after = level.leveled.tasks[bi];
    ppm::WorkCalendar ownCalendar(source, source.tasks[bi]);
    const auto delay = ownCalendar.offset(after.remainingStart.value_or(after.start)) -
                       ownCalendar.offset(before.remainingStart.value_or(before.start));
    check(delay == (effort && split ? 960 : 480), "unexpected true task-calendar delay");
    ppm::ResourceComparisonDialog dialog(doc);
    dialog.show();
    auto *priority = dialog.findChild<QComboBox *>("levelingPriority");
    priority->setCurrentIndex(priority->findData("shortest-first"));
    dialog.findChild<QPushButton *>("runLeveling")->click();
    auto *table = dialog.findChild<QTableWidget *>("levelingTasks");
    waitFor([&] { return table->rowCount() == 2 && table->item(int(bi), 5); });
    check(table->item(int(bi), 0)->text() == "B plain" &&
              table->item(int(bi), 5)->text() == QString::number(double(delay) / 60, 'f', 2) &&
              table->item(int(ai), 5)->text() == "0.00" && *doc.project() == source,
          "shared calendar cache mixed task breaks or display modified the document");
    dialog.hide();
}
std::string sourceValue(const ppm::ProjectReport &report, const std::string &key) {
    const auto source = std::find_if(report.tables.begin(), report.tables.end(),
                                     [](const auto &table) { return table.id == "source"; });
    check(source != report.tables.end(), "source table missing");
    for (const auto &row : source->rows)
        if (std::get<std::string>(row[0].value) == key)
            return std::get<std::string>(row[1].value);
    throw std::runtime_error("source engine field missing");
}
void engineProvenance(ppm::JsonProjectStore &store, const std::string &path, int mode) {
    ppm::Project p;
    p.documentId = "engine-provenance";
    p.name = "Engine provenance";
    p.startDate = "2026-10-26";
    p.calculationProfile = ppm::constraintProfile;
    p.tasks = {{"M", "M", 0, {}}};
    p.tasks[0].constraintType = ppm::ConstraintType::SNET;
    p.tasks[0].constraintAt = "2026-10-27 09:00";
    p.taskCosts = {{"M", 100, "uniform", "physical"}};
    p.statusAt = "2026-10-27 09:00";
    std::string baselineId, baselineEngine = "없음";
    if (mode != 2) {
        const auto plan = ppm::calculatePlan(p, mode == 0 ? ppm::legacyEngineFor(p) : ppm::engineFor(p));
        ppm::Baseline b;
        b.id = "B";
        b.version = 1;
        b.name = "Approved";
        b.approvedBy = "PM";
        b.reason = "Approved";
        b.createdAt = "2026-10-30T00:00:00Z";
        b.sourceDocumentId = p.documentId;
        b.inputRevision = 1;
        b.input = p;
        b.engineVersion = plan.engineVersion;
        b.projectFinish = *plan.projectFinish;
        b.schedule = plan.tasks;
        p.baselines = {b};
        p.revision = 2;
        baselineId = b.id;
        baselineEngine = b.engineVersion;
    }
    store.save(path, p, {});
    const auto disk = bytes(path);
    ppm::Document doc(store);
    doc.open(path);
    const auto comparison = ppm::compareToBaseline(*doc.project(), baselineId);
    const auto currentEngine = comparison.plan.engineVersion;
    const auto change = "계산 엔진 변경: " + baselineEngine + " → " + currentEngine;
    check(comparison.tasks[0].changes.empty(), "fixture unexpectedly changed task input");
    if (mode == 0)
        check(comparison.planFinishVariance == -1 && comparison.contextChanges == std::vector<std::string>{change},
              "engine-only finish variance has no explanation with both versions");
    else
        check(comparison.contextChanges.empty(), "same/no baseline engine created a false change label");
    ppm::ComparisonDialog dialog(doc, baselineId);
    dialog.show();
    auto *summary = dialog.findChild<QLabel *>("comparisonSummary");
    waitFor([&] { return summary->text().contains("현재 계획 계산 엔진"); });
    const auto text = summary->text();
    check(text.contains(QString::fromStdString("현재 계획 계산 엔진 " + currentEngine)) &&
              text.contains(QString::fromStdString("예측 계산 엔진 " + comparison.forecast.engineVersion)),
          "comparison UI omitted plan or forecast engine provenance");
    if (mode != 2)
        check(text.contains(QString::fromStdString("기준선 계산 엔진 " + baselineEngine)),
              "comparison UI omitted recorded baseline engine");
    else
        check(text.contains("기준선 없음"), "comparison UI did not identify missing baseline");
    check(text.contains("계산 엔진 변경:") == (mode == 0), "comparison UI engine-change explanation is wrong");
    dialog.hide();
    ppm::ReportOptions options;
    options.baselineId = baselineId;
    options.reportId = "engine-provenance";
    options.generatedAt = "2026-10-30T00:00:00Z";
    const auto report = ppm::buildReport(*doc.project(), options);
    check(sourceValue(report, "기준선 계산 엔진") == baselineEngine &&
              sourceValue(report, "현재 계획 계산 엔진") == currentEngine &&
              sourceValue(report, "계산 엔진") == comparison.forecast.engineVersion,
          "shared report source table omitted baseline/plan engine or changed active-engine meaning");
    check(*doc.project() == p && !doc.dirty() && store.load(path).project == p && bytes(path) == disk,
          "provenance display/report modified archive metadata or original bytes");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir dir;
        ppm::JsonProjectStore store;
        for (bool reverse : {false, true})
            for (bool calendar : {false, true})
                for (bool forecast : {false, true})
                    splitDelay(store, reverse, calendar, forecast);
        splitDelay(store, false, false, false, true, true, 960);
        splitDelay(store, false, true, false, true, false);
        splitDelay(store, false, true, false, false, false);
        for (int mode : {0, 1, 2})
            engineProvenance(store, dir.filePath(QString::number(mode) + ".ppm").toStdString(), mode);
        std::cout << "PASS: split delay display across order/calendar/forecast variants, ordinary PDM, engine-only "
                     "variance explanation, same/no baseline UI/report provenance and original-byte preservation\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
