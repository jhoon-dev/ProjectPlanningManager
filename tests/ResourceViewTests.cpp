#include "application/Document.h"
#include "domain/ResourceView.h"
#include "calculation/ResourceViewReport.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *s) {
    if (!ok)
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
    p.documentId = "resource-view";
    p.name = "자원 보기 검증";
    p.startDate = "2026-11-02";
    p.tasks = {{"T", "설계", 480, {}}};
    p.resourceFields = {{"region", "지역", CustomFieldType::Choice, "", {{"seoul", "서울"}, {"busan", "부산"}}},
                        {"rank", "숙련도", CustomFieldType::Number, "점", {}},
                        {"note", "메모", CustomFieldType::Text, "", {}},
                        {"date", "인증일", CustomFieldType::Date, "", {}}};
    for (int i = 0; i < 5; ++i) {
        Resource r;
        r.id = "R" + std::to_string(i);
        r.name = "자원 " + std::to_string(i);
        p.resources.push_back(r);
    }
    p.resources[0].attributes = {
        {"field:region", "seoul"}, {"field:rank", "10"}, {"field:note", ""}, {"field:date", "2026-10-01"}};
    p.resources[1].attributes = {
        {"field:region", "busan"}, {"field:rank", "2"}, {"field:note", "0"}, {"field:date", "2026-11-01"}};
    p.resources[2].attributes = {{"field:region", "seoul"}, {"field:rank", "0"}};
    p.resources[3].maxUnits.reset();
    p.resources[4].maxUnits = 0;
    p.resources[4].type = "equipment";
    p.assignments = {{"T", "R0", 100}, {"T", "R2", 100}};
    return p;
}
std::vector<std::string> ids(const Project &p, const ResourceViewSettings &v) {
    std::vector<std::string> out;
    for (const auto &r : projectResources(p, v))
        out.push_back(p.resources[r.resourceIndex].id);
    return out;
}
void run() {
    auto p = fixture();
    validate(p);
    ResourceViewSettings v;
    v.sortKey = "field:rank";
    check(ids(p, v) == std::vector<std::string>{"R2", "R1", "R0", "R3", "R4"}, "numeric sorting/missing last");
    v.descending = true;
    check(ids(p, v) == std::vector<std::string>{"R0", "R1", "R2", "R3", "R4"}, "descending null order");
    v.filters = {{"field:region", "equals", "seoul"}, {"field:rank", "gt", "0"}};
    check(ids(p, v) == std::vector<std::string>{"R0"}, "AND filter");
    v.filters = {{"field:note", "equals", ""}};
    check(ids(p, v) == std::vector<std::string>{"R0"}, "empty distinct from missing");
    v.filters = {{"field:note", "missing", ""}};
    check(ids(p, v) == std::vector<std::string>{"R2", "R3", "R4"}, "missing filter");
    v.filters = {{"field:note", "contains", "0"}};
    check(ids(p, v) == std::vector<std::string>{"R1"}, "contains text zero");
    v.filters = {{"capacity", "equals", "0"}};
    check(ids(p, v) == std::vector<std::string>{"R4"}, "capacity zero vs unknown");
    v.filters = {{"assigned", "equals", "yes"}};
    check(ids(p, v) == std::vector<std::string>{"R0", "R2"}, "assignment membership");
    v.filters = {{"field:date", "ge", "2026-11-01"}};
    check(ids(p, v) == std::vector<std::string>{"R1"}, "date order");
    v.filters.clear();
    v.query = "자원 1";
    check(ids(p, v) == std::vector<std::string>{"R1"}, "unicode query");
    v.query.clear();
    v.groupBy = {"field:region", "type"};
    v.descending = false;
    const auto grouped = projectResources(p, v);
    check(grouped.size() == 5 && grouped.front().groups[0].label == "부산" && grouped.back().groups[0].token == "m",
          "stable group identity");
    for (const auto &bad :
         std::vector<ResourceViewSettings>{{"", "missing", false, {}, {}},
                                           {"", "name", false, {"type", "type"}, {}},
                                           {"", "name", false, {}, {{"name", "gt", "a"}}},
                                           {"", "name", false, {}, {{"field:rank", "equals", "2.0"}}},
                                           {"", "name", false, {}, {{"type", "wat", "labor"}}},
                                           {"", "name", false, {}, {{"field:region", "equals", "unknown"}}}})
        rejects([&] { projectResources(p, bad); });
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool stopped = false;
    try {
        projectResources(p, v, control);
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "cancellation");
    QTemporaryDir temp;
    JsonProjectStore store;
    auto path = temp.filePath("r.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    d.captureBaseline("승인", "PM", "동결", "2026-11-06T01:00:00Z");
    d.save();
    const auto before = *d.project();
    const auto dates = calculatePlan(before);
    const auto costs = analyzeCosts(before, "");
    auto id = d.putSavedResourceView({{}, "지역별 숙련도", v});
    check(d.dirty() && nativeSchemaFor(*d.project()) == 21, "saved view dirty/schema");
    check(d.project()->baselines == before.baselines && calculatePlan(*d.project()).tasks == dates.tasks &&
              analyzeCosts(*d.project(), "").plan == costs.plan,
          "view changed calculation/archive");
    d.undo();
    check(!d.dirty() && d.project()->savedResourceViews.empty(), "view undo/savepoint");
    d.redo();
    rejects([&] { d.removeCustomField("region", FieldScope::Resource); });
    auto copy = d.project()->resourceFields[0];
    copy.name = "권역";
    d.putCustomField(copy, FieldScope::Resource);
    check(projectResources(*d.project(), d.project()->savedResourceViews[0].settings).front().groups[0].label == "부산",
          "rename changed stable criteria");
    check(json::decodeProject(json::encodeProject(*d.project()), 21) == *d.project(), "schema21 roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(*d.project()), 20); });
    auto duplicate = d.project()->savedResourceViews[0];
    duplicate.id.clear();
    rejects([&] { d.putSavedResourceView(duplicate); });
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-06T01:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "resource view recovery");
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    for (auto name : {"resources.ppm", "resources.xlsx", "resources.pdf"})
        QFile::remove(output + "/" + name);
    d.saveAs((output + "/resources.ppm").toStdString());
    const auto report = resourceViewReport(
        *d.project(), v, {{}, {}, "budget-remaining-v1", {}, {}, "resource-view", "2026-11-06T01:00:00Z", false});
    check(report.tables[0].rows.size() == 5, "report unique grain");
    exportXlsx(report, output + "/resources.xlsx");
    check(exportPdf(report, output + "/resources.pdf", ReportFamily::Resources) > 0, "view PDF");
    d.open(d.path(), true);
    rejects([&] { d.removeSavedResourceView(id); });
    auto large = p;
    large.resources.clear();
    large.assignments.clear();
    for (int i = 0; i < 10000; ++i) {
        auto r = p.resources[i % 5];
        r.id = "L" + std::to_string(i);
        large.resources.push_back(r);
    }
    QElapsedTimer timer;
    timer.start();
    check(projectResources(large, v).size() == 10000, "large projection");
    std::cout << "PASS resource view: typed filters, groups, stable IDs, missing/empty/zero, saved views, history, "
                 "schema, recovery, exports; 10k="
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
    } catch (const ppm::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
