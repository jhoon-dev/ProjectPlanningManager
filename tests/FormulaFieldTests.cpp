#include "application/Document.h"
#include "domain/FormulaFields.h"
#include "domain/ResourceView.h"
#include "calculation/ResourceViewReport.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <iostream>
#include <random>
namespace {
using namespace jobs;
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
    throw std::runtime_error("expected rejection");
}
Project fixture() {
    Project p;
    p.documentId = "formula-contract";
    p.name = "계산식 필드 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 120, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    p.projectFields = {{"factor", "배수", CustomFieldType::Number, "", {}},
                       {"count", "작업 가중 수", CustomFieldType::Number, "", {}, "[taskCount]*[field:factor]"}};
    p.projectAttributes = {{"field:factor", "2"}};
    p.customFields = {{"extra", "추가 시간", CustomFieldType::Number, "h", {}},
                      {"hours",
                       "환산 기간",
                       CustomFieldType::Number,
                       "h",
                       {},
                       "[durationHours]*[project:factor]+coalesce([field:extra],0{h})"},
                      {"error", "오류 확인", CustomFieldType::Number, "", {}, "1/0"}};
    p.resourceFields = {
        {"score", "숙련도", CustomFieldType::Number, "점", {}},
        {"weighted", "환산 숙련도", CustomFieldType::Number, "점", {}, "[field:score]*[project:factor]"},
        {"fte", "가용 단위", CustomFieldType::Number, "", {}, "[capacity]/100{%}"}};
    for (int i = 0; i < 3; ++i) {
        Resource r;
        r.id = "R" + std::to_string(i);
        r.name = "인력 " + std::to_string(i);
        r.rates = {{"2026-11-02", 60}};
        p.resources.push_back(r);
    }
    p.resources[0].attributes = {{"field:score", "10"}};
    p.resources[1].attributes = {{"field:score", "2"}};
    p.resources[1].maxUnits = 0;
    p.resources[2].maxUnits.reset();
    p.assignments = {{"T", "R0", 100}};
    return p;
}
FormulaValue eval(const std::string &expression, const std::string &unit = {}) {
    PlanInput p;
    p.projectFields = {{"out", "결과", CustomFieldType::Number, unit, {}, expression}};
    validateFormulaFields(p);
    return formulaValues(p, FieldScope::Project)->at("field:out");
}
void arithmetic(const QString &output) {
    PlanInput escaped;
    const std::string special = "a]b\\c";
    escaped.projectFields = {{special, "괄호 ID", CustomFieldType::Number, "", {}},
                             {"out", "결과", CustomFieldType::Number, "", {}}};
    const auto reference = formulaReferenceChoices(escaped, FieldScope::Project).front().second;
    escaped.projectFields[1].expression = reference + "*2";
    escaped.projectAttributes = {{"field:" + special, "3"}};
    check(formulaValues(escaped, FieldScope::Project)->at("field:out").number == 6000000,
          "escaped stable ID reference");
    for (const auto &[expression, expected] :
         std::vector<std::pair<std::string, std::string>>{{"1+2*3", "7"},
                                                          {"8/4/2", "1"},
                                                          {"-(2+3)", "-5"},
                                                          {"1/3*3", "0.999999"},
                                                          {"0.000001/2", "0.000001"},
                                                          {"-0.000001/2", "-0.000001"},
                                                          {"0.000005*0.1", "0.000001"},
                                                          {"999999999.999999/999999999.999999", "1"},
                                                          {"abs(-999999999.999999)", "999999999.999999"},
                                                          {"min(2,max(3,4))", "2"},
                                                          {"coalesce(0,1/0)", "0"},
                                                          {"8{h}/2{h}", "4"}}) {
        const auto v = eval(expression);
        check(v.number && formatFieldNumber(*v.number) == expected, "exact fixed-six arithmetic");
    }
    check(eval("min(8{h},4{h})", "h").number == 4000000, "typed functions");
    for (const auto *s : {"1/0", "coalesce(1/0,2)", "999999999.999999+0.000001", "(999999999.999999*2)/2"})
        check(!eval(s).number && !eval(s).missing && !eval(s).issue.empty(), "arithmetic error became zero");
    for (const auto *s : {" ", ".1", "1.", "1e3", "NaN", "1;2", "eval(1)", "[missing]", "[field:none]", "1{h}+2",
                          "1{h}*2{h}", "1{h}/2{%}", "min(1,2{h})"})
        rejects([&] { eval(s); });
    rejects([&] { eval("1{h}"); });
    rejects([&] { eval(std::string(2049, '1')); });
    rejects([&] { eval(std::string(33, '(') + "1" + std::string(33, ')')); });
    rejects([&] { eval(std::string("1{a\0b}", 6)); });
    std::mt19937 rng(20261106);
    QJsonArray rows;
    EvaluationScope scope;
    for (int i = 0; i < 400; ++i) {
        const auto a = std::int64_t(rng() % 2000000001) - 1000000000;
        auto b = std::int64_t(rng() % 2000000001) - 1000000000;
        if (i % 31 == 0)
            b = 0;
        if (i % 31 == 1)
            b = 1;
        if (i % 31 == 2)
            b = -1;
        const auto op = std::string(1, "+-*/"[i % 4]);
        PlanInput p;
        p.projectFields = {{"a", "A", CustomFieldType::Number, "", {}},
                           {"b", "B", CustomFieldType::Number, "", {}},
                           {"out", "결과", CustomFieldType::Number, "", {}, "[field:a]" + op + "[field:b]"}};
        p.projectAttributes = {{"field:a", formatFieldNumber(a)}, {"field:b", formatFieldNumber(b)}};
        const auto v = formulaValues(p, FieldScope::Project)->at("field:out");
        rows.append(QJsonObject{{"a", QString::fromStdString(formatFieldNumber(a))},
                                {"b", QString::fromStdString(formatFieldNumber(b))},
                                {"op", QString::fromStdString(op)},
                                {"raw", v.number ? QJsonValue(double(*v.number)) : QJsonValue(QJsonValue::Null)},
                                {"issue", QString::fromStdString(v.issue)}});
    }
    QFile file(output + "/formula-arithmetic.json");
    check(file.open(QIODevice::WriteOnly), "arithmetic artifact");
    file.write(QJsonDocument(rows).toJson());
}
void run() {
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    arithmetic(output);
    auto p = fixture();
    validate(p);
    check(resolvedAttributeValue(p, p.tasks[0], "field:hours") == "16" &&
              resolvedAttributeValue(p, p.tasks[1], "field:hours") == "4",
          "task/project references");
    check(resolvedProjectAttributeValue(p, "field:count") == "4", "project counts");
    check(resolvedAttributeValue(p, p.resources[0], "field:weighted") == "20" &&
              resolvedAttributeValue(p, p.resources[1], "field:fte") == "0",
          "resource numeric/zero");
    auto rValues = formulaValues(p, FieldScope::Resource, nullptr, &p.resources[2]);
    check(!rValues->at("field:weighted").number && rValues->at("field:weighted").missing, "missing propagation");
    auto errors = formulaValues(p, FieldScope::Task, &p.tasks[0]);
    check(!errors->at("field:error").number && !errors->at("field:error").missing, "error/missing distinction");
    ResourceViewSettings view;
    view.sortKey = "field:weighted";
    view.filters = {{"field:weighted", "ge", "4"}};
    auto rows = projectResources(p, view);
    check(rows.size() == 2 && p.resources[rows[0].resourceIndex].id == "R1", "computed numeric resource filter/sort");
    view.groupBy = {"field:weighted"};
    p.savedResourceViews = {{"rv", "계산식 자원 보기", view}};
    validate(p);
    {
        EvaluationScope scope;
        check(resolvedAttributeValue(p, p.tasks[0], "field:hours") == "16", "cache seed");
        p.projectAttributes[0].value = "3";
        check(resolvedAttributeValue(p, p.tasks[0], "field:hours") == "24", "project cache invalidation");
        p.projectAttributes[0].value = "2";
        p.tasks[0].durationMinutes = 600;
        check(resolvedAttributeValue(p, p.tasks[0], "field:hours") == "20", "input cache invalidation");
        p.tasks[0].durationMinutes = 480;
    }
    auto bad = p;
    bad.customFields[1].expression = "[field:hours]";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[1].expression = "[durationHours]+[project:factor]";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[1].expression = "[field:missing]";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields.push_back({"text", "문자열", CustomFieldType::Text, "", {}});
    bad.customFields[1].expression = "[field:text]";
    rejects([&] { validate(bad); });
    bad = p;
    bad.tasks[0].attributes = {{"field:hours", "16"}};
    rejects([&] { validate(bad); });
    bad = p;
    bad.resourceFields[1].type = CustomFieldType::Text;
    rejects([&] { validate(bad); });
    bad = p;
    bad.projectFields[0].expression = "[field:count]";
    rejects([&] { validate(bad); });
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool stopped = false;
    try {
        EvaluationScope scope(cancelled);
        formulaValues(p, FieldScope::Task, &p.tasks[0]);
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "formula cancellation");
    QTemporaryDir temp;
    JsonProjectStore store;
    auto path = temp.filePath("formula.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    d.captureBaseline("계산식 승인", "PM", "동결", "2026-11-06T01:00:00Z");
    d.save();
    const auto before = *d.project();
    const auto baseline = before.baselines;
    const auto dates = calculatePlan(before);
    const auto costs = analyzeCosts(before, "");
    d.setProjectAttribute("field:factor", "3");
    check(resolvedAttributeValue(*d.project(), d.project()->tasks[0], "field:hours") == "24", "dependency update");
    check(d.project()->baselines == baseline && calculatePlan(*d.project()).tasks == dates.tasks &&
              analyzeCosts(*d.project(), "").plan == costs.plan,
          "formula changed schedule/cost/frozen input");
    check(resolvedAttributeValue(d.project()->baselines[0].input, d.project()->baselines[0].input.tasks[0],
                                 "field:hours") == "16",
          "frozen formula changed");
    d.undo();
    check(!d.dirty() && resolvedProjectAttributeValue(*d.project(), "field:count") == "4", "formula undo/savepoint");
    d.redo();
    d.undo();
    auto f = d.project()->customFields[1];
    f.expression = "[durationHours]*4";
    d.putCustomField(f);
    check(resolvedAttributeValue(*d.project(), d.project()->tasks[0], "field:hours") == "32",
          "formula definition change");
    d.undo();
    rejects([&] { d.setTaskAttribute({"T"}, "field:hours", "1"); });
    rejects([&] { d.setTaskAttribute({"T"}, "field:hours", {}); });
    rejects([&] { d.setResourceAttribute({"R0"}, "field:weighted", "1"); });
    rejects([&] { d.setProjectAttribute("field:count", "1"); });
    rejects([&] { d.removeCustomField("extra"); });
    rejects([&] { d.removeCustomField("factor", FieldScope::Project); });
    const auto stable = *d.project();
    f = d.project()->customFields[1];
    f.expression = "[field:hours]";
    rejects([&] { d.putCustomField(f); });
    check(*d.project() == stable, "invalid formula partially applied");
    d.setStatusAt("2026-11-02 18:00");
    d.setDatedRemainingWork({"T", "R0", *d.project()->statusAt, {{"2026-11-03", 6001}}});
    check(calculate(*d.project()).status == CalculationStatus::Valid && nativeSchemaFor(*d.project()) == 23,
          "three-feature coexistence");
    check(json::decodeProject(json::encodeProject(*d.project()), 23) == *d.project(), "schema23 roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(*d.project()), 22); });
    auto malformed = json::encodeProject(*d.project());
    auto definitions = malformed["customFields"].toArray();
    auto definition = definitions[1].toObject();
    definition["expression"] = false;
    definitions[1] = definition;
    malformed["customFields"] = definitions;
    rejects([&] { json::decodeProject(malformed, 23); });
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-06T01:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "formula recovery scan");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->customFields == d.project()->customFields &&
              restored.project()->remainingWork == d.project()->remainingWork &&
              restored.project()->savedResourceViews == d.project()->savedResourceViews,
          "three-feature recovery");
    for (auto name : {"formulas.ppm", "formulas.xlsx", "formulas.pdf", "formula-resources.xlsx"})
        QFile::remove(output + "/" + name);
    d.saveAs((output + "/formulas.ppm").toStdString());
    const auto report = buildReport(
        *d.project(), {{}, {}, "budget-remaining-v1", {}, {}, "formula-report", "2026-11-06T01:00:00Z", false});
    exportXlsx(report, output + "/formulas.xlsx");
    check(exportPdf(report, output + "/formulas.pdf", ReportFamily::Schedule) > 0, "formula PDF");
    exportXlsx(
        resourceViewReport(*d.project(), view,
                           {{}, {}, "budget-remaining-v1", {}, {}, "formula-resources", "2026-11-06T01:00:00Z", false}),
        output + "/formula-resources.xlsx");
    d.open(d.path(), true);
    rejects([&] { d.putCustomField(f); });
    auto large = p;
    large.resources.clear();
    large.assignments.clear();
    for (int i = 0; i < 10000; ++i) {
        auto r = p.resources[i % 3];
        r.id = "L" + std::to_string(i);
        large.resources.push_back(r);
    }
    QElapsedTimer timer;
    timer.start();
    check(projectResources(large, view).size() == 6667, "computed 10k resource filter");
    std::cout << "PASS formula fields: exact arithmetic, units, refs/cycles, null/error, dependencies, readonly, "
                 "views, archives, schema/recovery, three-feature coexistence; 10k="
              << timer.elapsed() << "ms\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const jobs::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
