#include "application/Document.h"
#include "domain/Classification.h"
#include "ui/TaskViewProjection.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "calculation/Reports.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <limits>
#include <iostream>

namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "invalid classification accepted");
}
QByteArray read(const std::string &path) {
    QFile f(QString::fromStdString(path));
    check(f.open(QIODevice::ReadOnly), "file open failed");
    return f.readAll();
}
QByteArray signedFile(QJsonObject root, const QJsonObject &payload) {
    root["payload"] = payload;
    root["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
    return QJsonDocument(root).toJson();
}
void numberContract() {
    for (const auto &s : {"0", "-0", "+0.000000", "1", "-12.125", "999999999.999999", "-999999999.999999"})
        check(parseFieldNumber(formatFieldNumber(parseFieldNumber(s))) == parseFieldNumber(s),
              "fixed decimal roundtrip failed");
    check(formatFieldNumber(parseFieldNumber("+001.250000")) == "1.25", "numeric canonicalization failed");
    for (const auto &s : {"", " ", "1e2", "NaN", "inf", "1,000", ".5", "1.", "1.1234567", "1000000000", "--2", "1x"})
        rejects([&] { parseFieldNumber(s); });
    rejects([] { formatFieldNumber(std::numeric_limits<std::int64_t>::min()); });
}
void dataAndHistory() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("Classification contract", "2026-11-02", {"A", "B", "C", "D"});
    for (std::size_t i = 0; i < 4; ++i)
        doc.setDuration(i, std::int64_t(i + 1) * 60);
    doc.setPredecessors(1, {doc.project()->tasks[0].id});
    const auto schedule = calculate(*doc.project()).tasks;
    std::vector<std::string> tasks;
    for (const auto &t : doc.project()->tasks)
        tasks.push_back(t.id);
    const auto text = doc.putCustomField({{}, "구역", CustomFieldType::Text, {}, {}});
    const auto number = doc.putCustomField({{}, "수량", CustomFieldType::Number, "m²", {}});
    const auto date = doc.putCustomField({{}, "납기", CustomFieldType::Date, {}, {}});
    const auto choice =
        doc.putCustomField({{}, "검토", CustomFieldType::Choice, {}, {{"todo", "대기"}, {"done", "완료"}}});
    const auto codes = doc.putActivityCodeType(
        {{}, "공종", {{"civil", "토목", {}}, {"structure", "구조물", "civil"}, {"steel", "철골", {}}}});
    doc.setTaskAttribute({tasks[0], tasks[1]}, "field:" + text, "동측");
    doc.setTaskAttribute({tasks[2]}, "field:" + text, std::string{});
    doc.setTaskAttribute({tasks[0]}, "field:" + number, "10");
    doc.setTaskAttribute({tasks[1]}, "field:" + number, "2");
    doc.setTaskAttribute({tasks[2]}, "field:" + number, "0");
    doc.setTaskAttribute({tasks[0]}, "field:" + date, "2026-11-03");
    doc.setTaskAttribute({tasks[0]}, "field:" + choice, "todo");
    doc.setTaskAttribute({tasks[0], tasks[1]}, "code:" + codes, "structure");
    check(calculate(*doc.project()).tasks == schedule, "classification changed CPM dates/float");
    const auto before = *doc.project();
    for (const auto &value : {"2026-02-30", "11/03/2026"})
        rejects([&] { doc.setTaskAttribute({tasks[0]}, "field:" + date, value); });
    rejects([&] { doc.setTaskAttribute({tasks[0], "missing"}, "field:" + number, "3"); });
    rejects([&] { doc.setTaskAttribute({tasks[0], tasks[0]}, "field:" + number, "3"); });
    rejects([&] { doc.setTaskAttribute({tasks[0]}, "field:" + choice, "unknown"); });
    rejects([&] { doc.removeCustomField(text); });
    rejects([&] { doc.removeActivityCodeType(codes); });
    auto changedField = *customField(*doc.project(), "field:" + number);
    changedField.type = CustomFieldType::Text;
    rejects([&] { doc.putCustomField(changedField); });
    auto changedCode = *codeType(*doc.project(), "code:" + codes);
    changedCode.values[0].parentId = "structure";
    rejects([&] { doc.putActivityCodeType(changedCode); });
    changedCode = *codeType(*doc.project(), "code:" + codes);
    changedCode.values.erase(changedCode.values.begin() + 1);
    rejects([&] { doc.putActivityCodeType(changedCode); });
    check(*doc.project() == before, "rejected metadata edit changed document/revision");
    const auto baselineId = doc.captureBaseline("Frozen classification", "PM", "approved", "2026-11-02T00:00:00Z");
    const auto frozen = doc.project()->baselines[0];
    auto dictionary = *customField(*doc.project(), "field:" + choice);
    dictionary.options[0].name = "검토 대기";
    doc.putCustomField(dictionary);
    changedCode = *codeType(*doc.project(), "code:" + codes);
    changedCode.values[0].name = "기반 공사";
    changedCode.values[1].parentId = "steel";
    doc.putActivityCodeType(changedCode);
    check(attributeText(*doc.project(), "field:" + choice, "todo") == "검토 대기" &&
              attributeText(*doc.project(), "code:" + codes, "structure") == "철골 › 구조물" &&
              doc.project()->baselines[0] == frozen,
          "dictionary rename/move lost ID assignment or changed baseline");
    verifyBaseline(frozen);
    doc.setTaskAttribute({tasks[0]}, "field:" + number, "12.500000");
    check(attributeValue(doc.project()->tasks[0], "field:" + number) == "12.5", "number was not canonicalized");
    doc.undo();
    check(attributeValue(doc.project()->tasks[0], "field:" + number) == "10", "attribute undo failed");
    doc.redo();
    check(attributeValue(doc.project()->tasks[0], "field:" + number) == "12.5", "attribute redo failed");
    const auto comparison = compareToBaseline(*doc.project(), baselineId);
    check(std::find(comparison.tasks[0].changes.begin(), comparison.tasks[0].changes.end(),
                    "사용자 필드·작업 코드 값 변경") != comparison.tasks[0].changes.end() &&
              comparison.contextChanges.size() >= 2,
          "classification baseline differences not reported");
    const auto copies = doc.duplicateTasks({tasks[0]});
    check(std::any_of(
              doc.project()->tasks.begin(), doc.project()->tasks.end(),
              [&](const auto &t) { return t.id == copies[0] && t.attributes == doc.project()->tasks[0].attributes; }),
          "task duplication lost classification");
    doc.undo();
    doc.applyCalculation(calculate(*doc.project()));
    TaskViewSettings settings;
    settings.groupBy = {"field:" + text, "code:" + codes};
    settings.sortKey = "field:" + number;
    settings.attributeFilters = {{"field:" + number, "present", ""}};
    settings.columns = {{"field:" + number, 180, false}, {"name", 220, false}, {"id", 160, true}};
    const auto viewId = doc.putSavedTaskView({{}, "현장 분류", settings});
    check(doc.calculation()->status == CalculationStatus::Valid &&
              doc.calculation()->inputRevision == doc.project()->revision,
          "saving layout invalidated valid schedule");
    doc.undo();
    check(doc.project()->savedTaskViews.empty() && doc.calculation()->status == CalculationStatus::Valid,
          "view undo changed schedule");
    doc.redo();
    check(doc.project()->savedTaskViews[0].id == viewId, "view redo lost identity");
    const auto checkpoint = doc.recoverySnapshot("2026-11-02T10:00:00Z");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(checkpoint);
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema10 recovery scan failed");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->customFields == doc.project()->customFields &&
              restored.project()->savedTaskViews == doc.project()->savedTaskViews &&
              restored.project()->tasks == doc.project()->tasks &&
              restored.project()->baselines == doc.project()->baselines,
          "recovery lost classification/history/views");
    doc.saveAs((temp.path() + "/classified.ppm").toStdString());
    auto root = QJsonDocument::fromJson(read(doc.path())).object();
    check(root["schemaVersion"] == QJsonValue(10) && store.load(doc.path()).project == *doc.project() &&
              !store.load(doc.path()).requiresSaveAs,
          "schema10 native roundtrip failed");
    auto payload = root["payload"].toObject();
    payload["unknownClassification"] = 1;
    rejects([&] { decodeProjectFile(signedFile(root, payload)); });
    auto downgraded = root;
    downgraded["schemaVersion"] = 9;
    rejects([&] { decodeProjectFile(QJsonDocument(downgraded).toJson()); });
    Document reader(store);
    reader.open(doc.path(), true);
    rejects([&] { reader.setTaskAttribute({tasks[0]}, "field:" + number, "1"); });
    rejects([&] { reader.putSavedTaskView({{}, "readonly", {}}); });
    const auto saved = *doc.project();
    doc.setTaskAttribute({tasks[0]}, "field:" + number, "42");
    check(doc.dirty(), "attribute edit did not mark dirty");
    doc.undo();
    check(!doc.dirty(), "attribute undo did not restore save point");
    doc.saveAs((temp.path() + "/copy.ppm").toStdString());
    check(doc.project()->documentId != saved.documentId && doc.project()->customFields == saved.customFields &&
              doc.project()->savedTaskViews == saved.savedTaskViews && doc.project()->baselines == saved.baselines,
          "file copy lost metadata or history");
    const auto xml = exportProjectXml(*doc.project());
    const auto xer = exportPrimaveraXer(*doc.project());
    check(QJsonDocument(xml.report).toJson().contains("ExtendedAttribute") &&
              QJsonDocument(xer.report).toJson().contains("TASK/UDF/views"),
          "exchange omitted classification loss warning");
    const auto exchangedCodes = importPrimaveraXer(xer.xer, "classification.xer").project;
    check(exchangedCodes.codeTypes.size() == doc.project()->codeTypes.size() &&
              exchangedCodes.codeTypes[0].values.size() == doc.project()->codeTypes[0].values.size() &&
              !exchangedCodes.tasks[0].attributes.empty() &&
              attributeText(exchangedCodes, "code:" + exchangedCodes.codeTypes[0].id,
                            exchangedCodes.tasks[0].attributes[0].value) ==
                  attributeText(*doc.project(), "code:" + codes, "structure"),
          "XER code hierarchy or task classification did not round trip");
    ReportOptions options;
    options.reportId = "classification";
    options.generatedAt = "2026-11-02T10:00:00Z";
    const auto report = buildReport(*doc.project(), options);
    const auto table = std::find_if(report.tables.begin(), report.tables.end(),
                                    [](const auto &t) { return t.id == "taskClassification"; });
    check(table != report.tables.end() && table->rows.size() == 20, "classification report grain mismatch");
    const auto numeric = std::find_if(table->rows.begin(), table->rows.end(), [&](const auto &row) {
        return std::get<std::string>(row[0].value) == tasks[0] &&
               std::get<std::string>(row[2].value) == "field:" + number;
    });
    check(numeric != table->rows.end() && std::get<std::int64_t>((*numeric)[4].value) == 12500000 &&
              (*numeric)[4].format == CellFormat::FieldNumber,
          "report number lost six-place precision");
    Document plain(store);
    plain.create("Legacy writer", "2026-11-02", {"T"});
    plain.saveAs((temp.path() + "/plain.ppm").toStdString());
    check(QJsonDocument::fromJson(read(plain.path())).object()["schemaVersion"] == QJsonValue(9),
          "plain schema9 writer was changed");
}
void grouping() {
    Project p;
    p.documentId = "groups";
    p.name = "Groups";
    p.startDate = "2026-11-02";
    p.customFields = {{"region", "구역", CustomFieldType::Text, {}, {}},
                      {"quantity", "수량", CustomFieldType::Number, {}, {}}};
    p.tasks = {{"A", "A", 60, {}}, {"B", "B", 120, {}}, {"C", "C", 180, {}}, {"D", "D", 0, {}}, {"E", "E", {}, {}}};
    p.tasks[0].attributes = {{"field:region", "east"}, {"field:quantity", "10"}};
    p.tasks[1].attributes = {{"field:region", "east"}, {"field:quantity", "2"}};
    p.tasks[2].attributes = {{"field:region", "west"}, {"field:quantity", "0"}};
    p.tasks[3].attributes = {{"field:region", ""}};
    TaskViewSpec spec;
    spec.classification.groupBy = {"field:region"};
    spec.classification.sortKey = "field:quantity";
    const auto grouped = projectTaskView(p, nullptr, spec);
    check(grouped.rows == std::vector<std::size_t>({3, 1, 0, 2, 4}) && grouped.groups.size() == 4,
          "typed numeric/group/missing ordering failed");
    std::size_t total = 0;
    std::int64_t minutes = 0;
    for (const auto &g : grouped.groups) {
        total += g.tasks;
        minutes += g.durationMinutes;
    }
    check(total == 5 && minutes == 360 && grouped.groups.back().missingDurations == 1,
          "group totals duplicated or inferred unknown duration");
    spec.classification.groupPath = {"veast"};
    check(projectTaskView(p, nullptr, spec).rows == std::vector<std::size_t>({1, 0}),
          "group selection did not preserve row identity");
    spec.classification.groupPath.clear();
    spec.classification.groupBy.clear();
    spec.classification.descending = true;
    check(projectTaskView(p, nullptr, spec).rows == std::vector<std::size_t>({0, 1, 2, 3, 4}),
          "descending numeric sort misplaced missing values");
    spec.classification.attributeFilters = {{"field:region", "equals", ""}};
    check(projectTaskView(p, nullptr, spec).rows == std::vector<std::size_t>({3}), "empty text was treated as missing");
    spec.classification.attributeFilters = {{"field:region", "missing", ""}};
    check(projectTaskView(p, nullptr, spec).rows == std::vector<std::size_t>({4}), "missing filter failed");
    auto bad = p;
    bad.codeTypes = {{"type", "Cycle", {{"X", "X", "Y"}, {"Y", "Y", "X"}}}};
    rejects([&] { validate(bad); });
    bad = p;
    bad.tasks[0].attributes.push_back({"field:quantity", "3"});
    rejects([&] { validate(bad); });
    TaskViewSettings stale;
    stale.groupBy = {"field:deleted"};
    rejects([&] { validateTaskView(p, stale); });
    stale = {};
    stale.columns = {{"unknown", 120, false}};
    rejects([&] { validateTaskView(p, stale); });
    bad = p;
    bad.codeTypes = {{"deep", "Depth", {}}};
    for (int i = 0; i < 16; ++i)
        bad.codeTypes[0].values.push_back({std::to_string(i), "Node", i ? std::to_string(i - 1) : ""});
    validate(bad);
    bad.codeTypes[0].values.push_back({"16", "Node", "15"});
    rejects([&] { validate(bad); });
    bad = p;
    bad.tasks[0].attributes[0].value = std::string(16385, 'x');
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[0].options = {{"x", "Option"}};
    rejects([&] { validate(bad); });
    bad = p;
    bad.savedTaskViews = {{"v", "View", {}}};
    bad.savedTaskViews[0].settings.columns = {{"field:quantity", 0, false}};
    rejects([&] { validate(bad); });
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        numberContract();
        dataAndHistory();
        grouping();
        std::cout << "classification: types/identity/atomicity/history/schema/recovery/groups/reports passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
