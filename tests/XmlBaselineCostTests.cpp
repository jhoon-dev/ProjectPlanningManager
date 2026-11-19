#include "XmlBaselineFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/ResourceViewReport.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "calculation/Costs.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
XmlFormulaResult get(const Project &p, FieldScope s, const std::string &id, int row = 0) {
    return xmlFormulaValue(p, s, "field:" + id, s == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           s == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           s == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, message);
}
Project fixture() {
    auto p = xmlBaselineFixture();
    p.name = "최초 기준선 원가";
    p.currency = "USD";
    const auto costs = [](PlanInput &input, int multiple) {
        input.currency = "USD";
        input.resources[0].rates = {{"2026-11-01", 1001 * multiple}, {"2026-11-13", 2001 * multiple}};
        input.resources[1].rates = {{"2026-11-01", 3001 * multiple}, {"2026-11-13", 4001 * multiple}};
        input.taskCosts = {{"A", 101 * multiple, "uniform", "physical"},
                           {"B", 203 * multiple, "finish", "physical"},
                           {"M", 307 * multiple, "start", "physical"},
                           {"D", 0, "uniform", "physical"}};
        input.resourcePlan.materials = {{"MAT", "자재", "개", 107 * multiple, 10000}};
        input.resourcePlan.uses = {{"USE", "D", "MAT", 1500, "start"}};
        input.resourcePlan.expenses = {{"EXP", "D", "경비", "GENERAL", 83 * multiple, "finish"}};
    };
    costs(p.baselines[0].input, 1);
    costs(p.baselines[1].input, 2);
    costs(p, 10);
    p.taskCosts.push_back({"X", 0, "uniform", "physical"});
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        actualField(p, scope, "BC", CustomFieldType::Cost, "[Baseline Cost]");
    actualField(p, FieldScope::Task, "BCN", CustomFieldType::Number, "[BaselineCost]");
    actualField(p, FieldScope::Task, "DOUBLE", CustomFieldType::Number, "[Baseline Cost]*2");
    actualField(p, FieldScope::Assignment, "TC", CustomFieldType::Cost, "[Task.Baseline Cost]");
    actualField(p, FieldScope::Assignment, "RC", CustomFieldType::Cost, "[Resource.BaselineCost]");
    actualField(p, FieldScope::Assignment, "PC", CustomFieldType::Cost, "[Project.Baseline Cost]");
    customFields(p, FieldScope::Assignment).push_back({"receivedBC", "원가전파", CustomFieldType::Cost, "", {}});
    auto *bc = const_cast<CustomField *>(customField(p, "field:BC"));
    bc->xmlRollUp = XmlRollUp{2, 0};
    bc->xmlRollDown = XmlRollDown{"receivedBC", true};
    customFields(p, FieldScope::Resource).back().xmlRollUp = XmlRollUp{2, 0};
    p.tasks[0].attributes.push_back({"field:BC", "99"});
    p.tasks[4].attributes.push_back({"field:BC", "777"});
    return p;
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    const bool eur = p.currency == "EUR", jpy = p.currency == "JPY";
    const auto formatted = [&](std::int64_t n) { return formatFieldCost(n, p.currency); };
    const std::vector<std::int64_t> tasks =
        eur ? std::vector<std::int64_t>{145, 227, 154, 123} : std::vector<std::int64_t>{289, 454, 307, 244};
    for (int i = 0; i < 4; ++i) {
        if (get(p, FieldScope::Task, "BC", i).value != formatted(tasks[i]))
            std::cerr << i << " actual "
                      << get(p, FieldScope::Task, "BC", i).value.value_or(get(p, FieldScope::Task, "BC", i).issue)
                      << " expected " << formatted(tasks[i]) << "\n";
        check(get(p, FieldScope::Task, "BC", i).value == formatted(tasks[i]), "frozen task costs");
    }
    check(get(p, FieldScope::Project, "BC").value == formatted(eur ? 649 : 1294), "frozen project total");
    check(get(p, FieldScope::Resource, "BC").value == formatted(eur ? 25 : 50) &&
              get(p, FieldScope::Resource, "BC", 1).value == formatted(eur ? 194 : 389) &&
              get(p, FieldScope::Resource, "BC", 2).value == formatted(0),
          "resource assignment costs and known zero");
    check(get(p, FieldScope::Assignment, "BC").value == formatted(eur ? 25 : 50) &&
              get(p, FieldScope::Assignment, "BC", 1).value == formatted(eur ? 69 : 138) &&
              get(p, FieldScope::Assignment, "BC", 2).value == formatted(eur ? 125 : 251),
          "assignment daily half-up");
    check(get(p, FieldScope::Assignment, "TC").value == formatted(tasks[0]) &&
              get(p, FieldScope::Assignment, "RC").value == formatted(eur ? 25 : 50) &&
              get(p, FieldScope::Assignment, "PC").value == formatted(eur ? 649 : 1294),
          "owners/project prefixes");
    check(get(p, FieldScope::Task, "BCN").value == (jpy   ? "289"
                                                    : eur ? "1.45"
                                                          : "2.89") &&
              get(p, FieldScope::Task, "DOUBLE").value == (jpy   ? "578"
                                                           : eur ? "2.9"
                                                                 : "5.78"),
          "major-unit numeric arithmetic");
    for (auto s : {FieldScope::Task, FieldScope::Resource, FieldScope::Assignment}) {
        auto r = get(p, s, "BC", s == FieldScope::Task ? 4 : 3);
        check(r.missing && !r.value && !r.issue.empty(), "post-baseline objects are missing");
    }
    RollUpValues wbs(p);
    check(wbs.cell("G", "field:BC").value == formatted(eur ? 495 : 987) &&
              wbs.cell("H", "field:BC").value == formatted(eur ? 154 : 307) && !wbs.cell("", "field:BC").value,
          "current WBS strict membership");
    RollUpValues resources(p, FieldScope::Resource, {{"old", "", {0, 1, 2}}, {"new", "", {3}}});
    check(resources.cell("old", "field:BC").value == formatted(eur ? 219 : 439) &&
              !resources.cell("new", "field:BC").value,
          "resource groups");
    check(RollDownValues(p).value(p.assignments[0], "field:receivedBC") == formatted(tasks[0]),
          "roll-down keeps whole owner cost");
}
void vectors() {
    QJsonArray rows;
    std::uint64_t state = 941387;
    for (int i = 0; i < 128; ++i) {
        auto p = fixture();
        auto &b = p.baselines[0].input;
        const auto next = [&] {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return std::int64_t(state % 100000 + 1);
        };
        const auto r1 = next(), r2 = next(), s1 = next(), s2 = next(), fa = next(), fb = next(), fm = next(),
                   mat = next(), expense = next();
        b.resources[0].rates[0].hourlyMinor = r1;
        b.resources[0].rates[1].hourlyMinor = r2;
        b.resources[1].rates[0].hourlyMinor = s1;
        b.resources[1].rates[1].hourlyMinor = s2;
        b.taskCosts[0].fixedMinor = fa;
        b.taskCosts[1].fixedMinor = fb;
        b.taskCosts[2].fixedMinor = fm;
        b.resourcePlan.materials[0].unitPriceMinor = mat;
        b.resourcePlan.expenses[0].amountMinor = expense;
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        QJsonArray task, resource, assignment;
        for (int j = 0; j < 4; ++j)
            task.append(QString::number(parseFieldCost(*get(p, FieldScope::Task, "BC", j).value, "USD")));
        for (int j = 0; j < 3; ++j) {
            resource.append(QString::number(parseFieldCost(*get(p, FieldScope::Resource, "BC", j).value, "USD")));
            assignment.append(QString::number(parseFieldCost(*get(p, FieldScope::Assignment, "BC", j).value, "USD")));
        }
        rows.append(
            QJsonObject{{"r1", QString::number(r1)},
                        {"r2", QString::number(r2)},
                        {"s1", QString::number(s1)},
                        {"s2", QString::number(s2)},
                        {"fixedA", QString::number(fa)},
                        {"fixedB", QString::number(fb)},
                        {"fixedM", QString::number(fm)},
                        {"material", QString::number(mat)},
                        {"expense", QString::number(expense)},
                        {"tasks", task},
                        {"resources", resource},
                        {"assignments", assignment},
                        {"project", QString::number(parseFieldCost(*get(p, FieldScope::Project, "BC").value, "USD"))}});
    }
    save(QDir::currentPath() + "/xml-baseline-cost-qa/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    auto p = fixture();
    validate(p);
    for (const auto &b : p.baselines)
        verifyBaseline(b);
    verify(p);
    auto costOnly = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(costOnly, s))
            if (f.xmlFormula && std::any_of(f.xmlFormula->references.begin(), f.xmlFormula->references.end(),
                                            [](const auto &r) { return r.key != "builtin:baselinecost"; }))
                f.xmlFormula->enabled = false;
    verify(costOnly);
    check(hasXmlBaselineCostReferences(p, true) && nativeSchemaFor(p) == 74, "binding feature detection/schema62");
    check(!get(p, FieldScope::Task, "BC").value && !get(p, FieldScope::Task, "BC").missing,
          "explicit evaluation context");
    auto stale = calculatePlan(p);
    stale.status = CalculationStatus::Stale;
    {
        ScheduleFormulaScope context(p, &stale);
        check(get(p, FieldScope::Task, "BC").value == "2.89", "stale current schedule is irrelevant");
    }
    auto absent = p;
    absent.baselines.clear();
    {
        ScheduleFormulaScope c(absent);
        auto v = get(absent, FieldScope::Task, "BC");
        check(v.missing && !v.value, "no baseline");
    }
    auto unknown = p;
    unknown.baselines[0].input.resources[0].rates.clear();
    {
        ScheduleFormulaScope c(unknown);
        check(get(unknown, FieldScope::Task, "BC").missing && get(unknown, FieldScope::Project, "BC").missing &&
                  get(unknown, FieldScope::Resource, "BC", 1).value == "3.89" &&
                  get(unknown, FieldScope::Task, "BW").value == "5.64",
              "unknown cost is distinct from work/dates");
    }
    unknown = p;
    unknown.baselines[0].input.taskCosts[3].fixedMinor.reset();
    {
        ScheduleFormulaScope c(unknown);
        check(get(unknown, FieldScope::Task, "BC", 3).missing &&
                  get(unknown, FieldScope::Resource, "BC").value == "0.5",
              "missing fixed cost does not affect resource");
    }
    for (int kind = 0; kind < 3; ++kind) {
        auto bad = p;
        if (kind == 0)
            bad.baselines[0].input.currency = "JPY";
        if (kind == 1)
            bad.baselines[0].input.taskCosts[0].fixedMinor = maxMoney;
        if (kind == 2)
            bad.baselines[0].schedule.pop_back();
        ScheduleFormulaScope c(bad);
        auto v = get(bad, FieldScope::Task, "BC");
        check(!v.value && !v.missing && !v.issue.empty(), "currency/range/malformed errors");
    }
    auto deleted = p;
    deleted.tasks.erase(deleted.tasks.begin() + 1);
    deleted.assignments.erase(deleted.assignments.begin() + 2);
    {
        ScheduleFormulaScope c(deleted);
        check(get(deleted, FieldScope::Project, "BC").value == "12.94" &&
                  RollUpValues(deleted).cell("G", "field:BC").value == "5.33",
              "project includes removed frozen task, group current members");
    }
    auto jpy = p;
    jpy.currency = "JPY";
    for (auto &b : jpy.baselines)
        b.input.currency = "JPY";
    verify(jpy);
    check(json::decodeProject(json::encodeProject(p), 62) == p, "roundtrip62");
    rejects([&] { json::decodeProject(json::encodeProject(p), 61); }, "61 rejects cost binding");
    auto disabled = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(disabled, s))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(!hasXmlBaselineCostReferences(disabled, true) && nativeSchemaFor(disabled) == 74,
          "disabled binding retained62");
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    bool stopped = false;
    try {
        EvaluationScope e(cancel);
        ScheduleFormulaScope c(p);
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "cancellation");
    const auto out = QDir::currentPath() + "/xml-baseline-cost-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    const auto xml = exportProjectXml(p);
    save(out + "/baseline.xml", xml.xml);
    save(out + "/baseline.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    const auto imported = importProjectXml(xml.xml, "baseline.xml").project;
    check(imported.baselines.empty() && hasXmlBaselineCostReferences(imported, true),
          "XML preserves binding but not native baselines");
    {
        ScheduleFormulaScope c(imported);
        auto it = std::find_if(imported.customFields.begin(), imported.customFields.end(),
                               [](const auto &f) { return f.name == "BC"; });
        check(it != imported.customFields.end() &&
                  xmlFormulaValue(imported, FieldScope::Task, "field:" + it->id, &imported.tasks[0]).missing,
              "XML result never becomes fabricated baseline");
    }
    for (const auto *name : {"fields.xlsx", "fields.pdf", "source.ppm"})
        QFile::remove(out + "/" + name);
    ReportOptions opt;
    opt.reportId = "xml-baseline-cost";
    opt.generatedAt = "2026-11-14T03:00:00Z";
    auto report = buildReport(p, opt);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "reports");
    JsonProjectStore store;
    auto path = (out + "/source.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "[Baseline Cost]*2");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:BC", changed);
    {
        ScheduleFormulaScope c(*doc.project());
        check(get(*doc.project(), FieldScope::Task, "BC").value == "5.78", "edit");
    }
    doc.undo();
    verify(*doc.project());
    doc.redo();
    doc.undo();
    doc.captureBaseline("후속", "PM", "현재 원가 채택과 분리", "2026-11-14T03:01:00Z");
    doc.saveScenario("원가 사본", "task-order", 260, "2026-11-14T03:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    verify(*doc.project());
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-14", "동결 항목별 환산"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "반올림 검증";
    fx.recordedAt = "2026-11-14T03:03:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 62) == *doc.project(), "FX archives62");
    save(out + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, s))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 74 && json::decodeProject(json::encodeProject(frozen), 62) == frozen,
          "frozen-only62");
    auto observed = *doc.project();
    observed.statusAt = "2026-11-16 09:01";
    observed.progress.push_back(actualEntry(observed, "A0", "A", *observed.statusAt, "2026-11-16 09:00", {}, 1, 3));
    observed.progress.back().planDurationMinutes = 4;
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    verify(migrated);
    check(json::decodeProject(json::encodeProject(migrated), 62) == migrated, "ID archives62");
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery62");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:BC", {}); }, "read-only");
    vectors();
    std::cout << "PASS Baseline Cost: frozen v1 daily rates/fixed/material/expense, all scopes/strict groups, "
                 "unknown/zero/error, currency62/archives/outputs;128 vectors\n";
}
void ui() {
    auto p = fixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("baseline.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1, 2, 3, 4});
    const auto col = model.columnForKey("field:BC");
    check(model.data(model.index(0, col)).toString().contains("2.89"), "table works without current calculation");
    QTimer poll;
    poll.setInterval(25);
    bool finished = false;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->property("xmlFormulaBusy").toBool())
            return;
        auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
        if (!table)
            return;
        check(table->rowCount() == 5 && table->item(0, 1)->text().contains("2.89") &&
                  table->item(4, 1)->text().contains("미설정"),
              "worker preview frozen/unknown");
        auto *refs = dialog->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Baseline Start]") >= 0 && refs->findData("[Baseline Duration]") >= 0 &&
                  refs->findData("[Baseline Work]") >= 0 && refs->findData("[Baseline Cost]") >= 0,
              "reference insertion");
        check(dialog->findChild<QLabel *>("xmlFormulaSummary")->text().contains("BASE-1"), "preview provenance");
        auto out = QDir::currentPath() + "/xml-baseline-cost-qa";
        QDir().mkpath(out);
        check(dialog->grab().save(out + "/editor.png"), "editor capture");
        dialog->resize(800, 650);
        auto *scroll = dialog->findChild<QScrollArea *>();
        check(scroll && scroll->verticalScrollBar()->maximum() > 0, "small screen scroll");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        check(dialog->grab().save(out + "/baseline-small.png"), "screenshot");
        finished = true;
        poll.stop();
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:BC", nullptr);
    check(finished, "preview finished");
    std::cout << "PASS baseline UI: table without current result, worker preview, insertion/provenance\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Korean font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
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
