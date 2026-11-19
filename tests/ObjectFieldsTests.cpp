#include "application/Document.h"
#include "domain/Classification.h"
#include "calculation/Costs.h"
#include "calculation/Reports.h"
#include "calculation/Resources.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QFont>
#include <QFontDatabase>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
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
auto prepare(Document &d, EditCommand command,
             std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async,
                      [request = d.makeEditRequest(std::move(command)), control]() mutable {
                          return Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
const ReportTable &table(const ProjectReport &report, const std::string &id) {
    for (const auto &t : report.tables)
        if (t.id == id)
            return t;
    throw std::runtime_error("missing object report table");
}
std::vector<CustomField> fields() {
    return {{"label", "담당", CustomFieldType::Text, {}, {}},
            {"amount", "수량", CustomFieldType::Number, "m²", {}},
            {"due", "검토일", CustomFieldType::Date, {}, {}},
            {"phase", "검토 상태", CustomFieldType::Choice, {}, {{"review", "검토"}, {"done", "완료"}}}};
}
void contracts() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document d(store);
    d.create("프로젝트·자원 필드 검증", "2026-11-02", {"기초", "설비"});
    d.setDuration(0, 480);
    d.setDuration(1, 240);
    const auto wbs = d.addWbs("현장");
    d.assignWbs(0, wbs);
    d.setPredecessors(1, {d.project()->tasks[0].id});
    d.setTaskCost({d.project()->tasks[0].id, 10});
    d.setTaskCost({d.project()->tasks[1].id, 0});
    Resource r;
    r.name = "배정 인력";
    r.rates = {{"2026-11-02", 10}};
    const auto r1 = d.putResource(r);
    r.id.clear();
    r.name = "미배정 장비";
    r.type = "equipment";
    const auto r2 = d.putResource(r);
    d.setAssignments(d.project()->tasks[0].id, {{d.project()->tasks[0].id, r1, 100}});
    const auto original = calculate(*d.project()).tasks;
    check(analyzeCosts(*d.project(), "").plan == 90, "cost fixture");
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource})
        for (const auto &f : fields())
            d.putCustomField(f, scope);
    d.setTaskAttribute({d.project()->tasks[0].id}, "field:label", "작업 담당");
    d.setProjectAttribute("field:label", "=2+3 발주처");
    d.setProjectAttribute("field:amount", "+000.000000");
    d.setProjectAttribute("field:due", "2028-02-29");
    d.setProjectAttribute("field:phase", "review");
    d.setResourceAttribute({r1}, "field:label", "토목");
    d.setResourceAttribute({r2}, "field:label", std::string{});
    d.setResourceAttribute({r1}, "field:amount", "12.500001");
    d.setResourceAttribute({r2}, "field:amount", "0");
    d.setResourceAttribute({r1}, "field:due", "2026-12-01");
    d.setResourceAttribute({r1}, "field:phase", "review");
    d.setResourceAttribute({r2}, "field:phase", "done");
    check(attributeValue(d.project()->projectAttributes, "field:amount") == "0" &&
              attributeValue(d.project()->resources[1].attributes, "field:label") == "" &&
              !attributeValue(d.project()->resources[1].attributes, "field:due"),
          "missing/empty/zero distinction");
    check(attributeValue(d.project()->tasks[0], "field:label") == "작업 담당" &&
              attributeValue(d.project()->projectAttributes, "field:label") == "=2+3 발주처",
          "scope collision");
    check(calculate(*d.project()).tasks == original && analyzeCosts(*d.project(), "").plan == 90,
          "metadata changed schedule/work/cost");
    const auto intact = *d.project();
    rejects([&] { d.setResourceAttribute({r1, "missing"}, "field:label", "partial"); });
    rejects([&] { d.setResourceAttribute({r1, r1}, "field:label", "duplicate"); });
    rejects([&] { d.setResourceAttribute({}, "field:amount", "4"); });
    rejects([&] { d.setProjectAttribute("field:due", "2026-02-29"); });
    rejects([&] { d.setProjectAttribute("field:amount", "1.0000001"); });
    rejects([&] { d.setResourceAttribute({r1}, "field:phase", "unknown"); });
    rejects([&] { d.removeCustomField("label", FieldScope::Project); });
    rejects([&] { d.removeCustomField("label", FieldScope::Resource); });
    auto changed = fields()[1];
    changed.type = CustomFieldType::Text;
    rejects([&] { d.putCustomField(changed, FieldScope::Project); });
    rejects([&] { d.putCustomField(changed, FieldScope::Resource); });
    changed = fields()[3];
    changed.options.erase(changed.options.begin());
    rejects([&] { d.putCustomField(changed, FieldScope::Resource); });
    check(*d.project() == intact, "failed field edit partially committed");
    const auto baselineId = d.captureBaseline("객체 필드 기준선", "PM", "승인", "2026-11-05T00:00:00Z");
    const auto frozen = d.project()->baselines[0];
    d.saveScenario("필드 동결", "task-order", 260, "2026-11-05T00:00:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto scenario = d.project()->scenarios[0];
    changed = fields()[3];
    changed.options[0].name = "검토 대기";
    d.putCustomField(changed, FieldScope::Resource);
    check(d.project()->baselines[0] == frozen && d.project()->scenarios[0] == scenario &&
              attributeText(*d.project(), "field:phase", "review", FieldScope::Resource) == "검토 대기",
          "option rename changed archives/IDs");
    verifyBaseline(frozen);
    verifyBaseline(json::decodeProject(json::encodeProject(*d.project()), 16).baselines[0]);
    // Export a fixed, inspectable snapshot before destructive-current-value/undo cases.
    const auto output = QDir::currentPath() + "/object-fields-qa";
    check(QDir().mkpath(output), "output directory");
    for (const auto *name : {"objects.ppm", "objects.xlsx", "objects.pdf", "wbs.xlsx"}) {
        const auto path = output + '/' + name;
        if (QFile::exists(path))
            check(QFile::remove(path), "replace generated fixture");
    }
    store.save((output + "/objects.ppm").toStdString(), *d.project(), {});
    ReportOptions options;
    options.reportId = "object-fields";
    options.generatedAt = "2026-11-05T01:00:00Z";
    options.baselineId = baselineId;
    const auto report = buildReport(*d.project(), options);
    check(table(report, "projectClassification").rows.size() == 4 &&
              table(report, "resourceClassification").rows.size() == 8,
          "object report grain");
    check(std::get<std::int64_t>(table(report, "projectClassification").rows[1][4].value) == 0 &&
              table(report, "resourceClassification").rows[1][4].format == CellFormat::FieldNumber,
          "typed numeric report");
    exportXlsx(report, output + "/objects.xlsx");
    check(exportPdf(report, output + "/objects.pdf", ReportFamily::All) > 0, "PDF export");
    options.wbsId = wbs;
    const auto scoped = buildReport(*d.project(), options);
    check(table(scoped, "projectClassification").rows.size() == 4 &&
              table(scoped, "resourceClassification").rows.size() == 4,
          "WBS/unassigned resource scope");
    exportXlsx(scoped, output + "/wbs.xlsx");
    auto xml = exportProjectXml(*d.project());
    auto xer = exportPrimaveraXer(*d.project());
    auto mapped = [](const QJsonObject &report, const QString &scope) {
        int count = 0;
        for (const auto &value : report["mapping"].toArray()) {
            const auto entry = value.toObject();
            if (entry["kind"] == "udf" && entry["scope"] == scope)
                ++count;
        }
        return count;
    };
    check(mapped(xml.report, "RSRC") == 4 && mapped(xml.report, "PROJECT") == 4 && mapped(xer.report, "RSRC") == 4 &&
              mapped(xer.report, "PROJECT") == 4,
          "object UDF scope mapping missing");
    check(projectXmlPreview(xml).contains("UDF PROJECT") &&
              QJsonDocument(xer.report).toJson().contains(QString::fromUtf8("옵션 ID").toUtf8()),
          "remaining object exchange loss missing");
    for (const auto &package :
         {publishProjectXml(xml, temp.path() + "/xml"), publishPrimaveraXer(xer, temp.path() + "/xer")})
        check(store.load((package + "/source.ppm").toStdString()).project == *d.project(),
              "exchange lost native fields");
    d.saveAs(temp.filePath("live.ppm").toStdString());
    const auto saved = *d.project();
    check(nativeSchemaFor(saved) == 16 && store.load(d.path()).project == saved, "schema16 roundtrip");
    d.setProjectAttribute("field:label", "다른 발주처");
    check(d.dirty(), "project value not dirty");
    d.undo();
    check(!d.dirty(), "project value undo did not restore save point");
    d.redo();
    d.undo();
    const auto beforeWorker = *d.project();
    auto request = prepare(d, ObjectAttributeEdit{FieldScope::Resource, {r1, r2}, "field:amount", "7.25"});
    check(request->state() == EditState::Ready && *d.project() == beforeWorker &&
              d.project()->resources == saved.resources,
          "worker mutated source values");
    check(d.applyPreparedEdit(*request) == EditState::Applied, "object worker commit");
    check(attributeValue(d.project()->resources[0].attributes, "field:amount") == "7.25" &&
              attributeValue(d.project()->resources[1].attributes, "field:amount") == "7.25",
          "batch omitted a resource");
    d.undo();
    check(!d.dirty() && d.project()->resources == saved.resources, "resource value delta/dirty undo");
    d.redo();
    d.undo();
    auto ctrl = std::make_shared<CalculationControl>();
    request = prepare(d, ObjectAttributeEdit{FieldScope::Project, {}, "field:label", "cancel"}, ctrl);
    const auto beforeCancel = *d.project();
    ctrl->cancel();
    check(d.applyPreparedEdit(*request) == EditState::Cancelled && *d.project() == beforeCancel,
          "cancel partially applied");
    request = prepare(d, ObjectAttributeEdit{FieldScope::Project, {}, "field:label", "stale"});
    d.open(d.path());
    check(d.applyPreparedEdit(*request) == EditState::Stale &&
              d.project()->projectAttributes == saved.projectAttributes,
          "same-ID session accepted stale fields");
    Document reader(store);
    reader.open(d.path(), true);
    rejects([&] { reader.setProjectAttribute("field:label", "blocked"); });
    rejects([&] { reader.setResourceAttribute({r1}, "field:label", "blocked"); });
    rejects([&] { reader.putCustomField(fields()[0], FieldScope::Resource); });
    d.setProjectAttribute("field:label", "복구 값");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-05T02:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema16 recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->projectFields == d.project()->projectFields &&
              restored.project()->projectAttributes == d.project()->projectAttributes &&
              restored.project()->resources == d.project()->resources &&
              restored.project()->baselines == d.project()->baselines,
          "recovery lost fields/archive");
    d.saveAs(temp.filePath("copy.ppm").toStdString());
    check(d.project()->documentId != saved.documentId && d.project()->projectFields == saved.projectFields,
          "copy field loss");
    d.removeResource(r2);
    check(d.project()->resources.size() == 1, "resource deletion");
    d.undo();
    check(d.project()->resources.size() == 2 && d.project()->resources[1].attributes == saved.resources[1].attributes,
          "resource deletion undo lost values");
    LevelingSelection level;
    level.policy = levelingPolicyFor(*d.project());
    level.adoptedAt = "2026-11-05T00:00:00Z";
    level.reason = "필드 편집 무효화 검증";
    level.sourceDocumentId = d.project()->documentId;
    level.sourceRevision = d.project()->revision;
    d.adoptLeveling(level);
    const auto decisions = d.project()->levelingDecisions.size();
    d.setProjectAttribute("field:amount", "5");
    check(!d.project()->planLeveling && d.project()->levelingDecisions.size() == decisions + 1,
          "object edit bypassed invalidation ledger");
    d.undo();
    check(d.project()->planLeveling && attributeValue(d.project()->projectAttributes, "field:amount") == "0",
          "atomic selection/value undo");
    d.clearLeveling();
    // Only current references block dictionary deletion; snapshots retain their own complete dictionary.
    for (const auto scope : {FieldScope::Project, FieldScope::Resource}) {
        const auto definitions = customFields(*d.project(), scope);
        for (const auto &f : definitions) {
            if (scope == FieldScope::Project)
                d.setProjectAttribute("field:" + f.id, {});
            else
                d.setResourceAttribute({r1, r2}, "field:" + f.id, {});
            d.removeCustomField(f.id, scope);
        }
    }
    check(!hasObjectFields(*d.project()) && nativeSchemaFor(*d.project()) == 16 &&
              d.project()->baselines[0] == frozen && d.project()->scenarios[0] == scenario,
          "archive-only schema/deletion semantics");
    verifyBaseline(d.project()->baselines[0]);
    // Strict parser checks include isolated scopes and old-version rejection.
    const auto body = json::encodeProject(saved);
    rejects([&] { json::decodeProject(body, 15); });
    auto malformed = body;
    malformed["projectFields"] = QJsonValue::Null;
    rejects([&] { json::decodeProject(malformed, 16); });
    malformed = body;
    auto values = malformed["projectAttributes"].toArray();
    values.append(values[0]);
    malformed["projectAttributes"] = values;
    rejects([&] { json::decodeProject(malformed, 16); });
    auto bad = saved;
    bad.projectFields.push_back({"private", "프로젝트 전용", CustomFieldType::Text, {}, {}});
    bad.resources[0].attributes.push_back({"field:private", "잘못된 범위"});
    rejects([&] { validate(bad); });
    bad = saved;
    bad.resourceFields[0].id = "changed";
    rejects([&] { validate(bad); });
    bad = saved;
    bad.projectFields.resize(65, fields()[0]);
    rejects([&] { validate(bad); });
    Document isolated(store);
    isolated.create("범위별 같은 키", "2026-11-02", {"작업"});
    Resource item;
    item.name = "자원";
    const auto local = isolated.putResource(item);
    isolated.putCustomField({"same", "같은 이름", CustomFieldType::Text, {}, {}}, FieldScope::Task);
    isolated.putCustomField({"same", "같은 이름", CustomFieldType::Number, {}, {}}, FieldScope::Project);
    isolated.putCustomField({"same", "같은 이름", CustomFieldType::Choice, {}, {{"ok", "선택 값"}}},
                            FieldScope::Resource);
    isolated.setTaskAttribute({isolated.project()->tasks[0].id}, "field:same", "자유 텍스트");
    isolated.setProjectAttribute("field:same", "12.500001");
    isolated.setResourceAttribute({local}, "field:same", "ok");
    rejects([&] { isolated.setProjectAttribute("field:same", "ok"); });
    rejects([&] { isolated.setResourceAttribute({local}, "field:same", "12.500001"); });
    check(attributeValue(isolated.project()->tasks[0], "field:same") == "자유 텍스트" &&
              attributeText(*isolated.project(), "field:same", "ok", FieldScope::Resource) == "선택 값",
          "same-key type isolation");
    Document plain(store);
    plain.create("old writer", "2026-11-02", {});
    check(nativeSchemaFor(*plain.project()) == 9, "old writer changed");
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        contracts();
        std::cout << "Object fields: scopes/types/atomic history/archives/schema16/recovery/worker/export passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
