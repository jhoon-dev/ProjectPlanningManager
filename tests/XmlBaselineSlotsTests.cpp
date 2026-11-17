#include "XmlBaselineFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlRollUp.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
XmlFormulaResult get(const Project &p, const std::string &id, FieldScope scope = FieldScope::Task, int row = 0) {
    return xmlFormulaValue(p, scope, "field:" + id, scope == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           scope == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           scope == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
Project fixture() {
    auto p = xmlBaselineFixture();
    // Give every frozen version an independent, exactly known direct cost.
    for (auto &b : p.baselines) {
        b.input.currency = p.currency;
        for (const auto &t : b.input.tasks)
            b.input.taskCosts.push_back({t.id, std::int64_t(b.version * 100), "start", "physical"});
        for (auto &r : b.input.resources)
            r.rates = {{"2026-11-01", 0}};
    }
    for (int slot = 0; slot <= 10; ++slot) {
        const auto n = std::to_string(slot), prefix = "[Baseline" + n;
        actualField(p, FieldScope::Task, "S" + n, CustomFieldType::Start, prefix + " Start]");
        actualField(p, FieldScope::Task, "D" + n, CustomFieldType::Duration, prefix + " Duration]");
        actualField(p, FieldScope::Task, "W" + n, CustomFieldType::Number, prefix + "Work]");
        actualField(p, FieldScope::Task, "C" + n, CustomFieldType::Cost, prefix + " Cost]");
        actualField(p, FieldScope::Project, "W" + n, CustomFieldType::Number, prefix + " Work]");
        actualField(p, FieldScope::Resource, "W" + n, CustomFieldType::Number, prefix + "Work]");
        actualField(p, FieldScope::Assignment, "W" + n, CustomFieldType::Number, "[Task.Baseline" + n + " Work]");
    }
    actualField(p, FieldScope::Task, "DIFF", CustomFieldType::Number, "[Baseline1 Duration]-[Baseline0Duration]");
    actualField(p, FieldScope::Task, "SPAN", CustomFieldType::Number,
                "DateDiff(\"n\",[Baseline0 Start],[Baseline1Start])");
    p.customFields[p.customFields.size() - 2].xmlRollUp = XmlRollUp{2, 0};
    return p;
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    check(get(p, "S0").value == "2026-11-12T09:00:00" && get(p, "S1").value == "2026-11-16T09:00:00", "slot dates");
    check(get(p, "W0").value == "5.64" && get(p, "W1").value == "2.07", "slot curves and calendars");
    check(get(p, "W0", FieldScope::Project).value == "10.65" && get(p, "W1", FieldScope::Project).value == "7.08",
          "project frozen totals");
    check(get(p, "W0", FieldScope::Resource).value == "3" && get(p, "W1", FieldScope::Resource).value == "0.75",
          "resource frozen totals");
    check(get(p, "W0", FieldScope::Assignment).value == "5.64", "owner reference");
    check(parseFieldCost(*get(p, "C0").value, p.currency) == 100 &&
              parseFieldCost(*get(p, "C1").value, p.currency) == 200,
          "independent slot cost");
    check(get(p, "DIFF").value == "-4" && get(p, "SPAN").value == "5760", "mixed slot arithmetic");
    for (int slot = 2; slot <= 10; ++slot) {
        const auto value = get(p, "S" + std::to_string(slot));
        check(!value.value && value.missing && !value.issue.empty(), "missing slot does not reuse latest");
    }
    check(!get(p, "S0", FieldScope::Task, 4).value && get(p, "S0", FieldScope::Task, 4).missing, "new task unknown");
    check(xmlScheduleCaption(p).find("Baseline1") != std::string::npos, "numbered provenance");
}
void contract() {
    auto p = fixture();
    validate(p);
    verify(p);
    check(nativeSchemaFor(p) == 74 && json::decodeProject(json::encodeProject(p), 73) == p, "schema73 roundtrip");
    auto leafOnly = p;
    for (auto *input :
         {static_cast<PlanInput *>(&leafOnly), &leafOnly.baselines[0].input, &leafOnly.baselines[1].input})
        for (auto &field : input->customFields)
            field.xmlRollUp.reset();
    check(nativeSchemaFor(leafOnly) == 73, "numbered leaf references keep minimum schema73");
    bool rejected = false;
    try {
        (void)json::decodeProject(json::encodeProject(p), 72);
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, "old schema rejects numbered binding");
    for (const auto *name : {"[Baseline11 Start]", "[Baseline01 Start]", "[Baseline-1 Work]"})
        check(bindXmlFormula(p, FieldScope::Task, name).references.empty(), "invalid slot reserved");
    for (int slot = 0; slot <= 10; ++slot) {
        auto one = p;
        one.baselines[1].version = slot + 1;
        one.baselines.erase(one.baselines.begin());
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            customFields(one, scope)
                .erase(std::remove_if(customFields(one, scope).begin(), customFields(one, scope).end(),
                                      [](const auto &f) { return !f.id.starts_with("S"); }),
                       customFields(one, scope).end());
        ScheduleFormulaScope values(one, nullptr, ScheduleFormulaMode::Active, false);
        check(get(one, "S" + std::to_string(slot)).value == "2026-11-16T09:00:00", "lookup uses version not position");
    }
    auto bad = p;
    bad.baselines[1].schedule.pop_back();
    {
        ScheduleFormulaScope scope(bad, nullptr, ScheduleFormulaMode::Active, false);
        check(get(bad, "S0").value && !get(bad, "S1").value && !get(bad, "S1").missing, "slot failure isolation");
    }
    bad = p;
    bad.baselines.push_back(bad.baselines[1]);
    {
        ScheduleFormulaScope scope(bad, nullptr, ScheduleFormulaMode::Active, false);
        check(!get(bad, "S1").value && !get(bad, "S1").missing, "duplicate versions rejected");
    }
    auto inactive = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(inactive, s))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(nativeSchemaFor(inactive) == 74, "inactive stable bindings retain schema73");
    Project history = xmlBaselineFixture();
    history.scenarios.push_back({});
    history.scenarios.back().input = p;
    check(nativeSchemaFor(history) == 74, "frozen scenario detection");
    auto exchange = p;
    // XML has ten Start/Duration slots per scope. Slot lookup itself is tested
    // for all eleven versions above; exchange a representative subset together.
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        auto &fields = customFields(exchange, s);
        fields.erase(std::remove_if(fields.begin(), fields.end(),
                                    [](const auto &f) {
                                        return f.id.size() > 1 && f.id[1] >= '0' && f.id[1] <= '9' &&
                                               std::stoi(f.id.substr(1)) >= 2;
                                    }),
                     fields.end());
    }
    const auto xml = exportProjectXml(exchange);
    check(xml.xml.contains("Baseline1 Duration") && xml.xml.contains("Baseline0 Start"),
          "XML preserves numbered spelling");
    const auto imported = importProjectXml(xml.xml, "slots.xml").project;
    check(hasXmlNumberedBaselineReferences(imported, true), "XML rebinds numbered references");
    {
        ScheduleFormulaScope scope(imported, nullptr, ScheduleFormulaMode::Active, false);
        const auto f = std::find_if(imported.customFields.begin(), imported.customFields.end(),
                                    [](const auto &field) { return field.name == "S0"; });
        check(f != imported.customFields.end() && !get(imported, f->id).value,
              "XML static values never fabricate native baseline");
    }
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("slots.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("후속", "PM", "확인", "2026-11-15T09:00:00Z");
    {
        ScheduleFormulaScope scope(*doc.project(), nullptr, ScheduleFormulaMode::Active, false);
        check(get(*doc.project(), "S2").value && get(*doc.project(), "S0").value == "2026-11-12T09:00:00",
              "new approval fills only its slot");
    }
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-15T09:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid,
          "recovery reads schema73");
    std::cout << "PASS numbered baseline slots: v1-v11, values/cost/calendars, missing/error isolation, "
                 "schema/XML/recovery\n";
}
void ui() {
    auto p = fixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("slots.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    bool checked = false;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        for (auto *combo : dialog->findChildren<QComboBox *>())
            if (combo->findData("[Baseline10 Start]") >= 0 && combo->findData("[Baseline1 Cost]") >= 0)
                checked = true;
        dialog->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:S1", nullptr);
    check(checked, "UI offers numbered slots");
    std::cout << "PASS numbered baseline UI insertion\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &arg : error->diagnostic().arguments)
                std::cerr << ' ' << arg;
        return 1;
    }
}
