#include "XmlBaselineFixture.h"
#include "XmlRemainingFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlSummaryDuration.h"
#include "domain/CalculationControl.h"
#include "domain/XmlRollUp.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
XmlFormulaResult get(const Project &p, const std::string &id, FieldScope scope = FieldScope::Project) {
    return xmlFormulaValue(p, scope, "field:" + id, scope == FieldScope::Task ? &p.tasks.front() : nullptr);
}
void rejects(const Project &p, int schema) {
    bool rejected = false;
    try {
        (void)json::decodeProject(json::encodeProject(p), schema);
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, "older schema rejects new project duration binding");
}
void baseline() {
    auto p = xmlBaselineFixture();
    for (auto &b : p.baselines)
        b.input.xmlFormulaCalendarId = "C";
    p.xmlFormulaCalendarId = "C";
    actualField(p, FieldScope::Project, "PD", CustomFieldType::Duration, "[Baseline Duration]");
    actualField(p, FieldScope::Project, "PD0", CustomFieldType::Number, "[Baseline0 Duration]");
    actualField(p, FieldScope::Project, "PD1", CustomFieldType::Number, "[Baseline1Duration]");
    actualField(p, FieldScope::Task, "BD0", CustomFieldType::Number, "[Baseline0 Duration]");
    p.customFields.back().xmlRollUp = XmlRollUp{2, 0};
    actualField(p, FieldScope::Task, "PD0", CustomFieldType::Number, "[Project.Baseline0 Duration]");
    validate(p);
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(get(p, "PD0").value == "9" && get(p, "PD1").value == "6", "frozen project calendar per slot");
        check(parseFieldDuration(*get(p, "PD").value).minutes == 9000000, "typed project duration");
        check(get(p, "PD0", FieldScope::Task).value == "9", "project prefix from leaf");
        RollUpValues groups(p);
        check(groups.cell("G", "field:BD0").value == "9" && groups.cell("H", "field:BD0").value == "0",
              "WBS parallel span and milestone");
        check(!groups.cell("", "field:BD0").value && groups.cell("", "field:BD0").missing, "strict missing membership");
        RollUpValues filtered(p, FieldScope::Task, {{"one", "", {1}}, {"empty", "", {}}});
        check(filtered.cell("one", "field:BD0").value == "6" && !filtered.cell("empty", "field:BD0").value,
              "filtered summary and empty group");
    }
    p.calendars[0].periods = {{0, 1440}};
    p.calendars[0].weekdays = {1, 2, 3, 4, 5, 6, 7};
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(get(p, "PD0").value == "9", "current calendar changes cannot rewrite frozen summary");
    }
    check(nativeSchemaFor(p) == 74 && json::decodeProject(json::encodeProject(p), 74) == p, "schema74 roundtrip");
    rejects(p, 73);
    const auto xml = exportProjectXml(p);
    check(xml.xml.contains("Project.Baseline0 Duration") || xml.xml.contains("Baseline0 Duration"),
          "XML project duration names");
    const auto imported = importProjectXml(xml.xml, "summary.xml").project;
    check(hasXmlSummaryDurationReferences(imported, true), "XML project summary bindings remain active");
    auto old = xmlBaselineFixture();
    check(nativeSchemaFor(old) == 74 && json::decodeProject(json::encodeProject(old), 50) == old,
          "legacy summary settings read and upgrade on save");
    for (auto *input : {static_cast<PlanInput *>(&old), &old.baselines[0].input, &old.baselines[1].input})
        for (auto &field : input->customFields)
            field.xmlRollUp.reset();
    check(nativeSchemaFor(old) == 50, "leaf-only baseline keeps legacy minimum");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("summary.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    check(*doc.project() == p, "file preserves summary bindings");
    doc.renameTask(0, "복구할 편집");
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-15T09:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "schema74 recovery");
}
void actual() {
    auto p = xmlActualFixture();
    actualField(p, FieldScope::Project, "AD", CustomFieldType::Number, "[Actual Duration]");
    actualField(p, FieldScope::Task, "AD", CustomFieldType::Number, "[ActualDuration]");
    p.customFields.back().xmlRollUp = XmlRollUp{2, 0};
    validate(p);
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(!get(p, "AD").value && get(p, "AD").missing, "unreported included task is unknown");
        RollUpValues groups(p, FieldScope::Task, {{"AB", "", {0, 1}}, {"B", "", {2, 3}}});
        check(groups.cell("AB", "field:AD").value == "1350", "corrected actual start to current status working span");
        RollUpValues one(p, FieldScope::Task, {{"A", "", {0}}, {"M", "", {3}}, {"empty", "", {}}});
        check(one.cell("A", "field:AD").value == "300" && one.cell("M", "field:AD").value == "0" &&
                  !one.cell("empty", "field:AD").value,
              "completed/not-started/empty actual summaries");
        check(get(p, "AD", FieldScope::Task).value == "300", "leaf remains reported actual duration");
    }
    p.progress.push_back(actualEntry(p, "C0", "C", *p.statusAt, {}, {}, 0, 120));
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(get(p, "AD").value == "1350", "explicit not-started report completes knowledge");
    }
    p.statusAt = "2026-11-11 18:00";
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(!get(p, "AD").value, "future reports cannot fill historical summary");
        RollUpValues one(p, FieldScope::Task, {{"A", "", {0}}});
        check(one.cell("A", "field:AD").value == "300", "historical actual date correction");
    }
    p.statusAt.reset();
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        check(!get(p, "AD").value && get(p, "AD").missing, "actual status required");
    }
    rejects(p, 73);
}
void remaining() {
    auto p = xmlRemainingFixture();
    p.xmlFormulaCalendarId = "C";
    actualField(p, FieldScope::Project, "RD", CustomFieldType::Number, "[Remaining Duration]");
    actualField(p, FieldScope::Task, "SD", CustomFieldType::Number, "[RemainingDuration]");
    p.customFields.back().xmlRollUp = XmlRollUp{2, 0};
    const auto result = calculate(p);
    check(result.status == CalculationStatus::Valid, "remaining fixture calculates");
    QJsonArray spans;
    {
        ScheduleFormulaScope scope(p, &result);
        check(get(p, "RD").value.has_value(), "remaining project duration exists");
        RollUpValues groups(p, FieldScope::Task, {{"done", "", {0}}, {"empty", "", {}}, {"active", "", {1, 2, 3, 4}}});
        check(groups.cell("done", "field:SD").value == "0" && !groups.cell("empty", "field:SD").value,
              "completed excluded, empty unknown");
        std::int64_t sum = 0;
        for (const auto &row : result.tasks) {
            if (row.completedActual)
                continue;
            spans.append(QJsonObject{{"start", QString::fromStdString(row.remainingStart.value_or(row.start))},
                                     {"finish", QString::fromStdString(row.finish)}});
            sum += scope.values()->remainingDurations.at(row.taskId);
        }
        check(parseFieldNumber(*get(p, "RD").value) != sum * fieldNumberScale, "parallel durations are not summed");
        QDir().mkpath("xml-summary-qa");
        QFile file("xml-summary-qa/remaining.json");
        check(file.open(QIODevice::WriteOnly), "oracle output");
        file.write(QJsonDocument(QJsonObject{{"spans", spans}, {"value", QString::fromStdString(*get(p, "RD").value)}})
                       .toJson());
    }
    for (auto status : {CalculationStatus::Stale, CalculationStatus::Conflict, CalculationStatus::Incomplete,
                        CalculationStatus::Cancelled}) {
        auto invalid = result;
        invalid.status = status;
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        const auto value = get(p, "RD");
        check(!value.value &&
                  value.missing == (status == CalculationStatus::Incomplete || status == CalculationStatus::Cancelled),
              "invalid result classification");
    }
    auto plan = calculatePlan(p);
    {
        ScheduleFormulaScope scope(p, &plan, ScheduleFormulaMode::Plan);
        check(get(p, "RD").value.has_value(), "plan export obtains separate remaining forecast");
    }
    auto changed = p;
    ++changed.revision;
    {
        ScheduleFormulaScope scope(changed, &result, ScheduleFormulaMode::Active, false);
        check(!get(changed, "RD").value && !get(changed, "RD").missing, "stale input rejected");
    }
    rejects(p, 73);
    const auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        ScheduleFormulaScope scope(p, &result);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation propagates");
}
void calendarMatrix() {
    QJsonArray cases;
    for (int i = 0; i < 128; ++i) {
        Project p;
        p.documentId = "matrix";
        p.startDate = "2026-11-01";
        p.tasks = {{"A", "A", 1, {}}, {"B", "B", 1, {}}};
        p.xmlFormulaCalendarId = "C";
        p.calendars = {{"C",
                        "프로젝트",
                        i % 2 ? std::vector<int>{1, 3, 5, 7} : std::vector<int>{1, 2, 3, 4, 5},
                        {{540, 720}, {780, 1000 + i % 81}},
                        {"2026-11-11", "2026-11-16"}}};
        const auto first = QDate(2026, 11, 1).addDays(i % 19);
        const auto last = first.addDays(i % 7);
        const auto start = first.toString(Qt::ISODate).toStdString() + "T09:01:00.123456";
        const auto finish = last.toString(Qt::ISODate).toStdString() + "T16:02:00.654321";
        auto values = std::make_shared<XmlScheduleValues>();
        values->actualTasks = {{"A", {start, finish}}, {"B", {start, finish}}};
        XmlScheduleScope scope(p, values);
        const auto value = xmlSummaryDuration(p, "builtin:actualduration", {"A", "B"});
        check(value.value.has_value(), "microsecond calendar matrix");
        QJsonArray weekdays;
        for (auto day : p.calendars.front().weekdays)
            weekdays.append(day);
        cases.append(QJsonObject{{"start", QString::fromStdString(start)},
                                 {"finish", QString::fromStdString(finish)},
                                 {"weekdays", weekdays},
                                 {"lastMinute", 1000 + i % 81},
                                 {"value", QString::fromStdString(*value.value)}});
    }
    QDir().mkpath("xml-summary-qa");
    QFile file("xml-summary-qa/calendars.json");
    check(file.open(QIODevice::WriteOnly), "calendar oracle output");
    file.write(QJsonDocument(cases).toJson());
}
void ui() {
    auto p = xmlActualFixture();
    actualField(p, FieldScope::Project, "AD", CustomFieldType::Number, "[Actual Duration]");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("summary.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    bool checked = false;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        for (auto *combo : dialog->findChildren<QComboBox *>())
            if (combo->findData("[Actual Duration]") >= 0 && combo->findData("[Baseline10 Duration]") >= 0 &&
                combo->findData("[Remaining Duration]") >= 0)
                checked = true;
        dialog->reject();
    });
    editXmlFormula(doc, FieldScope::Project, "field:AD", nullptr);
    check(checked, "project duration insertion options");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else {
            baseline();
            actual();
            remaining();
            calendarMatrix();
        }
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &arg : error->diagnostic().arguments)
                std::cerr << ' ' << arg;
        return 1;
    }
    std::cout << "PASS summary durations\n";
}
