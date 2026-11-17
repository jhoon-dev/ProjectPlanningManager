#include "XmlActualFixture.h"
#include "XmlBaselineFixture.h"
#include "application/Document.h"
#include "calculation/BaselineFormulaValues.h"
#include "persistence/ProjectFile.h"
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Tracking.h"
#include "calculation/Reports.h"
#include "domain/XmlBaselines.h"
#include "domain/CalculationControl.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "persistence/XmlBaselineExchange.h"
#include "calculation/Costs.h"
#include "calculation/Resources.h"
#include "persistence/XmlBaselineTimephased.h"
#include "persistence/UdfMapping.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/XmlBaselineDialog.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *m) {
    bool b = false;
    try {
        f();
    } catch (const AppError &) {
        b = true;
    }
    check(b, m);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save output");
}
QString xmlUid(const QJsonObject &report, const QString &kind, const QString &id) {
    for (auto value : report["mapping"].toArray()) {
        auto row = value.toObject();
        if (row["kind"] == kind && row["jobsId"] == id)
            return row["xmlUid"].toString();
    }
    throw std::runtime_error("mapping missing");
}
void append(QByteArray &bytes, const QString &tag, const QString &uid, const QString &content) {
    auto text = QString::fromUtf8(bytes);
    QRegularExpression re("<" + tag + ">\\s*<UID>" + QRegularExpression::escape(uid) + "</UID>.*?</" + tag + ">",
                          QRegularExpression::DotMatchesEverythingOption);
    auto match = re.match(text);
    check(match.hasMatch(), "XML parent exists");
    auto at = match.capturedEnd() - tag.size() - 3;
    text.insert(at, content);
    bytes = text.toUtf8();
}
ProjectXmlExchange fixture(bool progress = false) {
    Project p;
    p.documentId = "baseline-source";
    p.name = "외부 XML 기준선";
    p.startDate = "2026-11-16";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"G", "그룹", {}}};
    p.tasks = {{"A", "A", 480, {}}, {"B", "B", 240, {}}, {"M", "M", 0, {}}};
    p.tasks[0].wbsId = "G";
    p.tasks[1].wbsId = "G";
    Resource resource;
    resource.id = "R";
    resource.name = "R";
    p.resources = {resource};
    p.assignments = {{"A", "R", 100}, {"B", "R", 50}};
    actualField(p, FieldScope::Task, "TS", CustomFieldType::Start, "[Baseline1 Start]");
    actualField(p, FieldScope::Task, "TD", CustomFieldType::Number, "[Baseline1 Duration]");
    actualField(p, FieldScope::Task, "TDF", CustomFieldType::Duration, "[Baseline1 Duration]");
    actualField(p, FieldScope::Task, "TW", CustomFieldType::Number, "[Baseline1 Work]");
    actualField(p, FieldScope::Task, "TC", CustomFieldType::Cost, "[Baseline1 Cost]");
    actualField(p, FieldScope::Project, "PD", CustomFieldType::Number, "[Baseline1 Duration]");
    actualField(p, FieldScope::Project, "PW", CustomFieldType::Number, "[Baseline1 Work]");
    actualField(p, FieldScope::Resource, "RW", CustomFieldType::Number, "[Baseline1 Work]");
    actualField(p, FieldScope::Assignment, "AW", CustomFieldType::Number, "[Baseline1 Work]");
    actualField(p, FieldScope::Assignment, "TW", CustomFieldType::Number, "[Task.Baseline1 Work]");
    auto out = exportProjectXml(p);
    auto &bytes = out.xml;
    auto a = xmlUid(out.report, "task", "A"), b = xmlUid(out.report, "task", "B"), wbs = xmlUid(out.report, "wbs", "G"),
         r = xmlUid(out.report, "resource", "R"), assignment = xmlUid(out.report, "assignment", "A/R");
    const auto core = [](int slot, const QString &extra = QString{}) {
        return "<Baseline><Number>" + QString::number(slot) +
               "</Number><Start>2026-11-01T09:00:00.123456</Start><Finish>2026-11-02T10:00:00.123456</"
               "Finish><Duration>PT1H0M0S</Duration><Work>PT30M</Work><Cost>1234</Cost>" +
               extra + "</Baseline>";
    };
    for (int slot = 0; slot <= 10; ++slot)
        if (slot != 1)
            append(bytes, "Task", a, core(slot));
    const auto time = [&](int type, const QString &value, const QString &uid) {
        return "<TimephasedData><Type>" + QString::number(type) + "</Type><UID>" + uid +
               "</UID><Start>2026-11-02T09:00:00</Start><Finish>2026-11-02T09:30:00</Finish><Unit>0</Unit><Value>" +
               value + "</Value></TimephasedData>";
    };
    append(bytes, "Task", a,
           "<Baseline>" + time(18, "PT30M", a) + time(19, "1234", a) +
               "<Number>1</Number><Interim>0</Interim><Start>2026-11-01T09:00:00.123456</"
               "Start><Finish>2026-11-02T10:00:00.123456</Finish><Duration>PT60M</Duration><DurationFormat>3</"
               "DurationFormat><EstimatedDuration>1</EstimatedDuration><Work>PT30M</Work><Cost>1234</Cost><BCWS>400</"
               "BCWS><BCWP>200</BCWP><FixedCost>100</FixedCost></Baseline>");
    append(bytes, "Task", b,
           "<Baseline><Number>1</Number><Duration>PT0S</Duration><Work>PT0S</Work><Cost>0</Cost></Baseline>");
    append(bytes, "Task", wbs,
           "<Baseline><Number>1</Number><Duration>PT111M</Duration><Work>PT30M</Work><Cost>1234</Cost></Baseline>");
    append(bytes, "Task", "0",
           "<Baseline><Number>1</Number><Duration>PT111M</Duration><Work>PT30M</Work><Cost>1234</Cost></Baseline>");
    append(bytes, "Resource", r,
           "<Baseline><Number>1</Number><Work>PT30M</Work><Cost>1234</Cost><BCWS>400</BCWS><BCWP>200</BCWP></"
           "Baseline>");
    append(bytes, "Assignment", assignment,
           "<Baseline>" + time(16, "PT30M", assignment) + time(17, "1234", assignment) +
               "<Number>1</Number><Start>2026-11-02T09:00:00</Start><Finish>2026-11-02T09:30:00</Finish><Work>PT30M</"
               "Work><Cost>1234</Cost></Baseline>");
    if (progress)
        append(bytes, "Task", a,
               "<ActualStart>2026-11-16T09:00:00</ActualStart><ActualDuration>PT60M</"
               "ActualDuration><RemainingDuration>PT420M</RemainingDuration>");
    return importProjectXml(bytes, "external-baselines.xml");
}
XmlBaselineOptions options(std::vector<int> numbers = {1}) {
    return {numbers, "PM", "외부 기준선 검토", "2026-11-15T09:00:00Z", true};
}
XmlFormulaResult get(const Project &p, const std::string &name, FieldScope s = FieldScope::Task, int row = 0) {
    const auto &fields = customFields(p, s);
    auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.name == name; });
    check(f != fields.end(), "formula field alias");
    return xmlFormulaValue(p, s, "field:" + f->id, s == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           s == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           s == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    check(get(p, "TS").value == "2026-11-01T09:00:00.123456" && get(p, "TD").value == "60",
          "external dates and explicit duration");
    check(get(p, "TW").value == "30" && get(p, "TC").value == "12.34", "external work/cost");
    const auto typed = parseFieldDuration(*get(p, "TDF").value);
    check(typed.minutes == 60000000 && typed.estimated, "external typed duration metadata");
    check(get(p, "TW", FieldScope::Task, 1).value == "0" && get(p, "TC", FieldScope::Task, 1).value == "0" &&
              get(p, "TS", FieldScope::Task, 1).missing,
          "zero distinct from absent");
    check(get(p, "TW", FieldScope::Task, 2).missing, "missing object never fills from current plan");
    check(get(p, "PD", FieldScope::Project).value == "111" && get(p, "PW", FieldScope::Project).value == "30",
          "explicit external project totals");
    check(get(p, "RW", FieldScope::Resource).value == "30" && get(p, "AW", FieldScope::Assignment).value == "30" &&
              get(p, "TW", FieldScope::Assignment).value == "30",
          "resource and assignment owner scopes");
    check(xmlScheduleCaption(p).find("외부 XML Baseline1") != std::string::npos, "external provenance");
}
void contract() {
    for (const auto *currency : {"USD", "EUR", "JPY", "KRW"}) {
        PlanInput money;
        money.currency = currency;
        for (auto value : {std::int64_t(0), std::int64_t(1), std::int64_t(1234), maxMoney}) {
            const auto wire = xmlBaselineMoneyText(value, currency);
            check(wire == exportXmlUdfScalar(money, CustomFieldType::Cost, formatFieldCost(value, currency)),
                  "common XML currency wire convention");
            check(xmlBaselineMoney(wire, currency) == value, "exact currency wire roundtrip");
        }
    }
    rejects([&] { xmlBaselineMoney("12.34", "USD"); }, "fractional native minor cost rejected");
    rejects([&] { xmlBaselineMoney("1234", "JPY"); }, "whole-currency precision checked");
    auto input = fixture();
    check(!input.project.xmlBaselines && input.project.baselines.empty(), "default no adoption/approval fabrication");
    check(input.baselineCandidates.size() == 16 &&
              std::all_of(input.baselineCandidates.begin(), input.baselineCandidates.end(),
                          [](const auto &c) { return c.issue.empty(); }),
          "all slots and scopes parsed");
    auto adopted = adoptXmlBaselines(input, options());
    auto spacedBytes = input.xml;
    spacedBytes.replace("<Number>1</Number><Interim>", "<Number> +01 </Number><Interim>");
    spacedBytes.replace("<Cost>1234</Cost>", "<Cost> 1234 </Cost>");
    spacedBytes.replace("<UID>2</UID>", "<UID> +002 </UID>");
    spacedBytes.replace("<TaskUID>2</TaskUID>", "<TaskUID> +002 </TaskUID>");
    const auto spaced = adoptXmlBaselines(importProjectXml(spacedBytes, "whitespace.xml"), options());
    verify(spaced.project);
    auto &p = adopted.project;
    verify(p);
    check(p.xmlBaselines->rows.size() == 6 && p.baselines.empty() && p.revision == input.project.revision + 1,
          "selected slot is atomic and separate");
    check(nativeSchemaFor(p) == 75 && json::decodeProject(json::encodeProject(p), 75) == p, "schema75 roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 74); }, "old schema rejects new data");
    check(nativeSchemaFor(input.project) == 74, "unadopted minimum retained");
    for (int variant = 0; variant < 5; ++variant) {
        auto body = json::encodeProject(p);
        auto source = body["xmlBaselines"].toObject();
        auto rows = source["rows"].toArray();
        if (variant == 0)
            rows.append(rows.first());
        if (variant == 1) {
            auto row = rows.first().toObject();
            row["work"] = -1;
            rows[0] = row;
        }
        if (variant == 2) {
            auto row = rows.first().toObject();
            row["flags"] = QJsonObject{{"DurationFormat", "99"}};
            rows[0] = row;
        }
        if (variant == 3)
            source["sourceSha256"] = "invalid";
        if (variant == 4)
            source["unknown"] = true;
        source["rows"] = rows;
        body["xmlBaselines"] = source;
        rejects([&] { json::decodeProject(body, 75); }, "malformed persisted baseline rejected");
    }
    rejects([&] { adoptXmlBaselines(adopted, options()); }, "repeat adoption rejected");
    rejects([&] { adoptXmlBaselines(input, options({1, 1})); }, "duplicate selection");
    rejects([&] { adoptXmlBaselines(input, options({11})); }, "invalid slot");
    auto modified = input;
    modified.project.tasks[0].name = "tamper";
    rejects([&] { adoptXmlBaselines(modified, options()); }, "draft tampering");
    modified = input;
    modified.baselineCandidates[0].row.work = 99;
    rejects([&] { adoptXmlBaselines(modified, options()); }, "candidate tampering");
    modified = input;
    modified.xml.replace("1234", "1235");
    rejects([&] { adoptXmlBaselines(modified, options()); }, "source tampering");
    for (int kind = 0; kind < 8; ++kind) {
        auto bytes = input.xml;
        if (kind == 0)
            bytes.replace("<Duration>PT60M</Duration>", "<Duration>PT-1M</Duration>");
        if (kind == 1)
            bytes.replace("<Cost>1234</Cost><BCWS>", "<Cost>-1</Cost><BCWS>");
        if (kind == 2)
            bytes.replace("<Number>1</Number><Interim>", "<Number>1</Number><Number>1</Number><Interim>");
        if (kind == 3)
            bytes.replace("<Type>18</Type>", "<Type>24</Type>");
        if (kind == 4)
            bytes.replace("<Duration>PT60M</Duration>", "<Duration>PT0.000001S</Duration>");
        if (kind == 5)
            bytes.replace("<Cost>1234</Cost><BCWS>", "<Cost>12.345</Cost><BCWS>");
        if (kind == 6)
            bytes.replace("<Duration>PT60M</Duration>", "<Duration><X>PT60M</X></Duration>");
        if (kind == 7)
            bytes.replace("<Cost>1234</Cost><BCWS>", "<Unsupported>1</Unsupported><Cost>1234</Cost><BCWS>");
        auto bad = importProjectXml(bytes, "invalid.xml");
        rejects([&] { adoptXmlBaselines(bad, options()); }, "invalid selected slot refused");
        check(adoptXmlBaselines(bad, options({0})).project.xmlBaselines.has_value(), "other slot remains selectable");
    }
    const auto exported = exportProjectXml(p);
    auto ambiguousBytes = input.xml;
    ambiguousBytes.replace("<Number>1</Number><Interim>", "<Number>1</Number><Number>0</Number><Interim>");
    const auto ambiguous = importProjectXml(ambiguousBytes, "ambiguous.xml");
    rejects([&] { adoptXmlBaselines(ambiguous, options({0})); }, "both ambiguous numbers rejected");
    rejects([&] { adoptXmlBaselines(ambiguous, options({1})); }, "both ambiguous numbers rejected");
    check(adoptXmlBaselines(ambiguous, options({2})).project.xmlBaselines.has_value(),
          "unrelated slot remains selectable");
    check(exported.xml.contains("<Baseline>") && exported.xml.contains("<EstimatedDuration>1</EstimatedDuration>"),
          "standard baseline elements");
    auto reimport = importProjectXml(exported.xml, "roundtrip.xml");
    check(!reimport.project.xmlBaselines, "roundtrip still requires explicit adoption");
    auto readopted = adoptXmlBaselines(reimport, options());
    verify(readopted.project);
    check(readopted.project.xmlBaselines->rows.size() == 6, "external row preservation");
    for (const auto &r : readopted.project.xmlBaselines->rows)
        if (r.kind == "task" && r.work == 30000000) {
            check(r.timephased.size() == 2 && r.money.at("FixedCost") == 100 && r.flags.at("DurationFormat") == "3",
                  "flags/money/timephased preserved");
        }
    auto noProject = p;
    std::erase_if(noProject.xmlBaselines->rows, [](const auto &r) { return r.kind == "project"; });
    {
        ScheduleFormulaScope scope(noProject, nullptr, ScheduleFormulaMode::Active, false);
        check(get(noProject, "PD", FieldScope::Project).missing, "project duration never inferred from leaf totals");
    }
    auto dirty = p;
    dirty.startDate = "2027-01-01";
    dirty.holidays = {"2026-11-02"};
    verify(dirty);
    CurrencyRate rate{"USD", "EUR", "2026-11-15", "検算"};
    rate.numerator = 1;
    rate.denominator = 2;
    const auto converted = convertProjectCurrency(p, rate);
    {
        ScheduleFormulaScope scope(converted, nullptr, ScheduleFormulaMode::Active, false);
        check(get(converted, "TC").value == "6.17", "currency conversion external cost");
    }
    for (const auto &r : converted.xmlBaselines->rows)
        for (const auto &t : r.timephased)
            if (xmlBaselineTimeCost(t.type))
                check(t.value == "617", "currency conversion baseline time cost");
    auto yenRate = rate;
    yenRate.targetCurrency = "JPY";
    yenRate.numerator = 100;
    yenRate.denominator = 1;
    const auto yen = convertProjectCurrency(p, yenRate);
    for (const auto &r : yen.xmlBaselines->rows)
        for (const auto &t : r.timephased)
            if (xmlBaselineTimeCost(t.type))
                check(t.value == "123400", "zero-decimal currency timephased wire");
    auto observed = p;
    observed.statusAt = "2026-11-16 18:00";
    observed.progress.push_back(
        actualEntry(observed, "OBS", p.tasks[0].id, *observed.statusAt, "2026-11-16 09:00", {}, 60, 420));
    observed.progress.back().planDurationMinutes = 480;
    auto migrated = migrateLedgerIdentities(observed, {{"task", p.tasks[0].id, "NEW-A"}});
    verify(migrated);
    check(json::decodeProject(json::encodeProject(migrated), 75) == migrated, "ID archives preserve imported baseline");
    auto progress = fixture(true);
    XmlProgressOptions po;
    po.statusAt = "2026-11-16 18:00";
    po.reporter = "PM";
    po.reason = "진척";
    po.recordedAt = "2026-11-15T09:00:00Z";
    po.taskIds = {progress.project.tasks[0].id};
    po.confirmed = true;
    auto combined = adoptXmlBaselines(adoptXmlProgress(progress, po), options());
    check(combined.project.progress.size() == 1 && combined.project.xmlBaselines, "progress + baseline adoption");
    QTemporaryDir tmp;
    const auto dir = publishProjectXml(combined, tmp.path() + "/combined");
    check(QFile::exists(dir + "/project.ppm"), "canonical publication replay");
    auto tampered = combined;
    tampered.project.xmlBaselines->rows[0].cost = 9;
    rejects([&] { publishProjectXml(tampered, tmp.path() + "/tamper"); }, "publication refuses tampering");
    check(!QFile::exists(tmp.path() + "/tamper"), "no partial publication");
    JsonProjectStore store;
    const auto path = tmp.filePath("baseline.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("승인1", "PM", "보존", "2026-11-15T09:01:00Z");
    doc.captureBaseline("승인2", "PM", "보존", "2026-11-15T09:02:00Z");
    verify(*doc.project());
    check(doc.project()->baselines.size() == 2, "native approvals retained independently");
    auto mixed = exportProjectXml(*doc.project());
    auto mixedRead = adoptXmlBaselines(importProjectXml(mixed.xml, "mixed.xml"), options({0, 1}));
    verify(mixedRead.project);
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-15T09:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema75 recovery");
    const auto output = QDir::currentPath() + "/xml-baseline-exchange-qa";
    QDir().mkpath(output);
    save(output + "/source.xml", input.xml);
    save(output + "/source-mapping.json", QJsonDocument(input.report).toJson());
    save(output + "/export.xml", exported.xml);
    save(output + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(output + "/mapping.json", QJsonDocument(exported.report).toJson());
    save(output + "/converted.xml", exportProjectXml(converted).xml);
    QFile::remove(output + "/source.ppm");
    store.save((output + "/source.ppm").toStdString(), p, {});
    for (auto name : {"fields.xlsx", "fields.pdf"})
        QFile::remove(output + "/" + name);
    ReportOptions reportOptions;
    reportOptions.reportId = "external-baseline";
    reportOptions.generatedAt = "2026-11-15T09:00:00Z";
    auto report = buildReport(p, reportOptions);
    exportXlsx(report, output + "/fields.xlsx");
    check(exportPdf(report, output + "/fields.pdf") > 0, "external baseline reports");
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool wasCancelled = false;
    try {
        EvaluationScope scope(cancelled);
        adoptXmlBaselines(input, options());
    } catch (const CalculationCancelled &) {
        wasCancelled = true;
    }
    check(wasCancelled, "cancel propagated");
    std::cout << "PASS external XML baselines: slots/scopes/partial/flags/timephased, canonical "
                 "adoption/export/replay, schema75/currency/ID/recovery/reports\n";
}
void native() {
    auto p = xmlBaselineFixture();
    auto exported = exportProjectXml(p);
    auto imported = importProjectXml(exported.xml, "native.xml");
    check(!imported.baselineCandidates.empty() &&
              std::all_of(imported.baselineCandidates.begin(), imported.baselineCandidates.end(),
                          [](const auto &c) { return c.issue.empty(); }),
          "native baseline projection candidates");
    auto adopted = adoptXmlBaselines(imported, options({0, 1}));
    ScheduleFormulaScope scope(adopted.project, nullptr, ScheduleFormulaMode::Active, false);
    check(get(adopted.project, "BW").value == "5.64" && get(adopted.project, "DN").value == "8",
          "native baseline work/duration survives explicit XML adoption");
    const auto output = QDir::currentPath() + "/xml-baseline-exchange-qa";
    QDir().mkpath(output);
    save(output + "/native-baselines.xml", exported.xml);
    save(output + "/native-projection-mapping.json", QJsonDocument(exported.report).toJson());
    auto removed = p;
    std::erase_if(removed.tasks, [](const auto &t) { return t.id == "B"; });
    std::erase_if(removed.assignments, [](const auto &a) { return a.taskId == "B"; });
    const auto removedExport = exportProjectXml(removed);
    const auto diagnostics = removedExport.report["diagnostics"].toArray();
    check(std::any_of(diagnostics.begin(), diagnostics.end(),
                      [](const auto &d) {
                          return d.toObject()["omittedTasks"].toInt() == 1 &&
                                 d.toObject()["omittedAssignments"].toInt() == 1;
                      }),
          "deleted frozen objects explicitly diagnosed");
    Project large;
    large.documentId = "scale-baseline";
    large.name = "1000 기준선";
    large.startDate = "2026-11-16";
    for (int i = 0; i < 1000; ++i) {
        auto id = "T" + std::to_string(i);
        large.tasks.push_back({id, id, 1, {}});
        large.taskCosts.push_back({id, 0, "start", "physical"});
    }
    auto baseline = prepareBaseline(large, "규모", "PM", "검증", "2026-11-15T09:00:00Z");
    baseline.id = "BASE";
    baseline.version = 1;
    large.baselines.push_back(baseline);
    ++large.revision;
    auto scale = exportProjectXml(large);
    auto source = importProjectXml(scale.xml, "scale.xml");
    check(source.baselineCandidates.size() == 1001, "1000-task baseline projection");
    check(adoptXmlBaselines(source, options({0})).project.xmlBaselines->rows.size() == 1001,
          "linear-size source adoption");
    std::cout << "PASS native baseline exchange: frozen numbers + 1000 tasks\n";
}
Project distributionFixture() {
    auto p = xmlBaselineFixture();
    const auto costs = [](PlanInput &input, int factor) {
        input.currency = "USD";
        input.resources[0].rates = {{"2026-11-01", 1001 * factor}, {"2026-11-13", 2001 * factor}};
        input.resources[1].rates = {{"2026-11-01", 3001 * factor}, {"2026-11-13", 4001 * factor}};
        input.taskCosts = {{"A", 101 * factor, "uniform", "physical"},
                           {"B", 203 * factor, "finish", "physical"},
                           {"M", 307 * factor, "start", "physical"},
                           {"D", 0, "uniform", "physical"}};
        input.resourcePlan.materials = {{"MAT", "자재", "개", 107 * factor, 10000}};
        input.resourcePlan.uses = {{"USE", "D", "MAT", 1500, "start"}};
        input.resourcePlan.expenses = {{"EXP", "D", "경비", "GENERAL", 83 * factor, "finish"}};
    };
    costs(p.baselines[0].input, 1);
    costs(p.baselines[1].input, 2);
    costs(p, 10);
    p.taskCosts.push_back({"X", 0, "uniform", "physical"});
    actualField(p, FieldScope::Task, "TCV", CustomFieldType::Cost, "[Baseline Cost]");
    return p;
}
void timephased() {
    auto p = distributionFixture();
    const auto original = p;
    QJsonArray diagnostics;
    const auto rows = projectXmlBaselines(p, diagnostics);
    check(p == original && !p.xmlBaselines, "native distribution export does not mutate source or fabricate adoption");
    std::size_t count = 0, zeroWork = 0, resourcePeriods = 0;
    for (const auto &r : rows) {
        std::int64_t work = 0, cost = 0;
        bool unknown = false;
        for (const auto &t : r.timephased) {
            ++count;
            check(r.timephasedOutside && t.start && t.finish && *t.start < *t.finish,
                  "native blocks have exact positive civil ranges");
            if (r.kind == "resource")
                ++resourcePeriods;
            check(t.type == xmlBaselineTimeType(r.number, r.kind, xmlBaselineTimeCost(t.type)),
                  "slot/scope timephased type");
            if (xmlBaselineTimeCost(t.type)) {
                check(t.unit == 2, "native cost uses a civil-day bucket");
                if (t.value)
                    cost += xmlBaselineMoney(*t.value, p.currency);
                else
                    unknown = true;
            } else {
                check(t.unit == 0 && t.value, "native work keeps execution intervals");
                auto value = xmlBaselineMinutes(*t.value, maxWork * 10000);
                work += value;
                zeroWork += value == 0;
            }
        }
        check(!r.work || r.work == work, "every exported work series reconciles to frozen total");
        check(!r.cost || (!unknown && r.cost == cost), "every exported cost series reconciles to frozen total");
        if (r.number == 0 && r.kind == "task" && r.id == "A")
            check(r.cost == 289 && r.work == 5640000, "independent first task work and cost");
        if (r.number == 0 && r.kind == "task" && r.id == "M")
            check(r.cost == 307 && r.work == 0 && !r.timephased.empty(), "milestone fixed cost retained");
        if (r.number == 0 && r.kind == "task" && r.id == "D")
            check(r.cost == 244 && r.work == 0, "unassigned material/expense costs retained");
        if (r.number == 0 && r.kind == "project")
            check(r.cost == 1294 && r.work == 10650000, "frozen whole-project sum");
    }
    check(count > 50 && zeroWork && resourcePeriods,
          "detailed contours, explicit idle gaps and resource series emitted");
    auto changed = p;
    changed.startDate = "2026-11-20";
    changed.holidays = {"2026-11-12"};
    changed.assignments[0].units = 1;
    changed.resources[0].rates = {{"2026-01-01", 99999}};
    QJsonArray changedDiagnostics;
    check(projectXmlBaselines(changed, changedDiagnostics) == rows,
          "current calendars/rates/units do not revalue frozen distributions");
    auto partial = p;
    partial.tasks.back().wbsId = "G";
    QJsonArray partialDiagnostics;
    for (const auto &r : projectXmlBaselines(partial, partialDiagnostics))
        if (r.kind == "wbs" && r.id == "G")
            check(!r.work && r.timephased.empty(), "unfrozen WBS member does not invent a complete distribution");
    auto unknown = p;
    unknown.baselines[0].input.resources[0].rates.clear();
    QJsonArray unknownDiagnostics;
    bool missing = false;
    for (const auto &r : projectXmlBaselines(unknown, unknownDiagnostics))
        if (r.number == 0 && r.kind == "task" && r.id == "A") {
            check(!r.cost && r.work == 5640000, "unknown rate leaves work known and cost unknown");
            for (const auto &t : r.timephased)
                if (xmlBaselineTimeCost(t.type) && !t.value)
                    missing = true;
        }
    check(missing, "unknown daily costs omit Value instead of writing zero");
    auto unknownSource = importProjectXml(exportProjectXml(unknown).xml, "unknown-native.xml");
    auto unknownAdopted = adoptXmlBaselines(unknownSource, options({0}));
    check(std::any_of(unknownAdopted.project.xmlBaselines->rows.begin(),
                      unknownAdopted.project.xmlBaselines->rows.end(),
                      [](const auto &r) {
                          return !r.cost && std::any_of(r.timephased.begin(), r.timephased.end(), [](const auto &t) {
                              return xmlBaselineTimeCost(t.type) && !t.value;
                          });
                      }),
          "unknown native costs survive XML adoption without fabricated zero");
    auto removed = p;
    std::erase_if(removed.tasks, [](const auto &t) { return t.id == "B"; });
    std::erase_if(removed.assignments, [](const auto &a) { return a.taskId == "B"; });
    QJsonArray removedDiagnostics;
    for (const auto &r : projectXmlBaselines(removed, removedDiagnostics))
        if (r.number == 0 && r.kind == "project") {
            const auto original = std::find_if(rows.begin(), rows.end(),
                                               [](const auto &s) { return s.number == 0 && s.kind == "project"; });
            check(r == *original, "project distribution retains deleted frozen task contributions");
        }
    auto exported = exportProjectXml(p);
    auto imported = importProjectXml(exported.xml, "native-timephased.xml");
    check(std::all_of(imported.baselineCandidates.begin(), imported.baselineCandidates.end(),
                      [](const auto &c) { return c.issue.empty(); }),
          "all native parent-level blocks parsed without losses");
    auto adopted = adoptXmlBaselines(imported, options({0, 1}));
    check(nativeSchemaFor(adopted.project) == 77 &&
              json::decodeProject(json::encodeProject(adopted.project), 77) == adopted.project,
          "schema77 parent-level distribution persistence");
    rejects([&] { json::decodeProject(json::encodeProject(adopted.project), 76); },
            "schema76 rejects new placement metadata");
    std::size_t adoptedCount = 0;
    for (const auto &r : adopted.project.xmlBaselines->rows)
        adoptedCount += r.timephased.size();
    check(adoptedCount == count, "native detailed records survive explicit adoption");
    std::map<std::pair<std::string, std::string>, std::string> targetIds;
    for (const auto &s : imported.report["mapping"].toArray()) {
        const auto source = s.toObject();
        for (const auto &v : exported.report["mapping"].toArray()) {
            const auto target = v.toObject();
            if (source["kind"] == target["kind"] && source["xmlUid"] == target["xmlUid"])
                targetIds[{source["kind"].toString().toStdString(), source["jobsId"].toString().toStdString()}] =
                    target["jobsId"].toString().toStdString();
        }
    }
    auto links = xmlBaselineLinks(*adopted.project.xmlBaselines);
    for (auto &l : links)
        if (l.kind != "project")
            l.targetId = targetIds.at({l.kind, l.sourceId});
    auto merge = prepareXmlBaselineMerge(p, imported, options({0, 1}), links, true);
    QTemporaryDir mergeDir;
    JsonProjectStore mergeStore;
    const auto mergePath = mergeDir.filePath("detailed.ppm").toStdString();
    mergeStore.save(mergePath, p, {});
    Document mergeDoc(mergeStore);
    mergeDoc.open(mergePath);
    mergeDoc.recordXmlBaselineMerge(merge);
    check(nativeSchemaFor(*mergeDoc.project()) == 77 &&
              json::decodeProject(json::encodeProject(*mergeDoc.project()), 77) == *mergeDoc.project(),
          "parent-level detail and merge audit persist together");
    rejects([&] { json::decodeProject(json::encodeProject(*mergeDoc.project()), 76); },
            "schema76 rejects detail in merge history");
    auto reexported = exportProjectXml(adopted.project);
    auto readAgain = adoptXmlBaselines(importProjectXml(reexported.xml, "again.xml"), options({0, 1}));
    std::size_t again = 0;
    for (const auto &r : readAgain.project.xmlBaselines->rows)
        again += r.timephased.size();
    check(again == count, "adopt/reexport does not duplicate or lose distribution rows");
    auto malformed = exported.xml;
    QRegularExpression time("<TimephasedData>.*?</TimephasedData>", QRegularExpression::DotMatchesEverythingOption);
    auto sourceText = QString::fromUtf8(malformed);
    auto match = time.match(sourceText);
    check(match.hasMatch(), "native timephased block exists");
    sourceText.insert(match.capturedEnd(), match.captured());
    auto duplicate = importProjectXml(sourceText.toUtf8(), "duplicate-time.xml");
    rejects([&] { adoptXmlBaselines(duplicate, options({0})); },
            "duplicate overlapping timephased blocks disable the number");
    auto wrongUidText = QString::fromUtf8(exported.xml);
    auto wrongUid = time.match(wrongUidText);
    auto wrongBlock = wrongUid.captured();
    wrongBlock.replace(QRegularExpression("<UID>[0-9]+</UID>"), "<UID>2147483647</UID>");
    wrongUidText.replace(wrongUid.capturedStart(), wrongUid.capturedLength(), wrongBlock);
    auto badUid = importProjectXml(wrongUidText.toUtf8(), "wrong-uid.xml");
    rejects([&] { adoptXmlBaselines(badUid, options({0})); }, "wrong parent UID blocks the affected number");
    check(adoptXmlBaselines(badUid, options({1})).project.xmlBaselines.has_value(),
          "other number remains selectable after a malformed native block");
    auto mixedText = QString::fromUtf8(exported.xml);
    const auto block = time.match(mixedText).captured();
    const auto baselineTag = mixedText.indexOf("<Baseline>");
    mixedText.insert(baselineTag + 10, block);
    auto mixed = importProjectXml(mixedText.toUtf8(), "mixed-location.xml");
    rejects([&] { adoptXmlBaselines(mixed, options({0})); }, "inline plus parent-level duplicates rejected");
    auto allSlots = p;
    allSlots.baselines.clear();
    allSlots.revision += 12;
    for (int n = 0; n < 12; ++n) {
        auto b = p.baselines[0];
        b.id = "DIST-" + std::to_string(n);
        b.version = std::uint64_t(n + 1);
        allSlots.baselines.push_back(std::move(b));
    }
    auto every = importProjectXml(exportProjectXml(allSlots).xml, "all-native-numbers.xml");
    std::set<int> numbers;
    for (const auto &c : every.baselineCandidates) {
        check(c.issue.empty(), "all native timephased types valid");
        numbers.insert(c.row.number);
    }
    check(numbers.size() == 11 && *numbers.begin() == 0 && *numbers.rbegin() == 10,
          "all baseline numbers; v12 remains native only");
    auto external = p;
    external.xmlBaselines = adoptXmlBaselines(fixture(), options({0})).project.xmlBaselines;
    auto externalRow = external.xmlBaselines->rows.front();
    externalRow.id = "A";
    externalRow.timephased = {{9, "1", "2026-11-01T09:00:00", "2026-11-01T09:30:00", "PT30M", 0}};
    external.xmlBaselines->rows = {externalRow};
    QJsonArray externalDiagnostics;
    int externalCount = 0;
    for (const auto &r : projectXmlBaselines(external, externalDiagnostics))
        if (r.number == 0) {
            check(r == externalRow, "adopted external number is never recomputed from native curves");
            ++externalCount;
        }
    check(externalCount == 1, "native distribution does not fill externally missing objects");
    for (const auto &currency : {"JPY", "KRW"}) {
        CurrencyRate rate{"USD", currency, "2026-11-15", "通貨"};
        rate.numerator = 100;
        rate.denominator = 1;
        auto converted = convertProjectCurrency(p, rate);
        QJsonArray d;
        for (const auto &r : projectXmlBaselines(converted, d)) {
            std::optional<std::int64_t> sum = 0;
            for (const auto &t : r.timephased)
                if (xmlBaselineTimeCost(t.type)) {
                    if (!t.value)
                        sum.reset();
                    else if (sum)
                        *sum += xmlBaselineMoney(*t.value, currency);
                }
            check(!r.cost || r.cost == sum, "zero-decimal currency timephased cost and core total reconcile");
        }
    }
    std::size_t limitCount = 100000;
    auto limitedRows = rows;
    rejects([&] { addNativeBaselineTimephased(p, p.baselines[0], limitedRows, 0, limitCount, diagnostics); },
            "record cap rejects rather than truncating");
    const auto dir = QDir::currentPath() + "/native-baseline-timephased-qa";
    QDir().mkpath(dir);
    save(dir + "/project.xml", exported.xml);
    save(dir + "/mapping.json", QJsonDocument(exported.report).toJson());
    save(dir + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(dir + "/adopted.json", QJsonDocument(json::encodeProject(adopted.project)).toJson());
    save(dir + "/reexported.xml", reexported.xml);
    QJsonArray inputs;
    for (const auto &b : p.baselines) {
        Project frozen;
        static_cast<PlanInput &>(frozen) = b.input;
        frozen.documentId = b.sourceDocumentId;
        frozen.revision = b.inputRevision;
        ScheduleResult schedule;
        schedule.documentId = frozen.documentId;
        schedule.inputRevision = frozen.revision;
        schedule.profile = frozen.calculationProfile;
        schedule.engineVersion = b.engineVersion;
        schedule.status = CalculationStatus::Valid;
        schedule.tasks = b.schedule;
        schedule.projectFinish = b.projectFinish;
        QJsonArray work, cost;
        auto first = b.schedule.front().start.substr(0, 10);
        for (const auto &r : b.schedule)
            first = std::min(first, r.start.substr(0, 10));
        for (const auto &s : resourceOccupancies(frozen, schedule))
            work.append(QJsonObject{{"task", QString::fromStdString(s.taskId)},
                                    {"resource", QString::fromStdString(s.resourceId)},
                                    {"start", QString::fromStdString(s.start)},
                                    {"finish", QString::fromStdString(s.finish)},
                                    {"units", s.units}});
        for (const auto &d : plannedCostDays(frozen, schedule, first, b.projectFinish.substr(0, 10)))
            cost.append(
                QJsonObject{{"task", QString::fromStdString(d.taskId)},
                            {"resource", QString::fromStdString(d.resourceId)},
                            {"date", QString::fromStdString(d.date)},
                            {"amount", d.amount ? QJsonValue(qint64(*d.amount)) : QJsonValue(QJsonValue::Null)}});
        inputs.append(QJsonObject{{"slot", int(b.version - 1)}, {"work", work}, {"cost", cost}});
    }
    save(dir + "/execution.json", QJsonDocument(inputs).toJson());
    auto actual = p;
    actual.statusAt = "2026-11-16 18:00";
    save(dir + "/actuals.xml", exportProjectXmlActuals(actual).xml);
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        QJsonArray d;
        projectXmlBaselines(p, d);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && p == original, "cancellation propagates without mutating frozen data");
    std::cout << "PASS native baseline timephased: " << count
              << " blocks, contours/idle/day rounding/fixed/material/expense, scopes/slots, "
                 "unknown/currency/schema77/re-adoption/limits\n";
}
Project mergeTarget(const ProjectXmlExchange &source) {
    auto p = source.project;
    p.documentId = "native-baseline-merge";
    p.revision = 10;
    p.xmlWbs.reset();
    for (auto &t : p.tasks) {
        t.id = "native-" + t.id;
        if (!t.wbsId.empty())
            t.wbsId = "native-" + t.wbsId;
    }
    for (auto &w : p.wbsNodes) {
        w.id = "native-" + w.id;
        if (!w.parentId.empty())
            w.parentId = "native-" + w.parentId;
    }
    for (auto &r : p.resources)
        r.id = "native-" + r.id;
    for (auto &a : p.assignments) {
        a.taskId = "native-" + a.taskId;
        a.resourceId = "native-" + a.resourceId;
    }
    auto old = *adoptXmlBaselines(source, options({0})).project.xmlBaselines;
    for (auto &r : old.rows) {
        if (!r.id.empty())
            r.id = "native-" + r.id;
        if (!r.resourceId.empty())
            r.resourceId = "native-" + r.resourceId;
    }
    old.weekdays = {1}; // A distinct previously frozen calendar must survive a later import.
    old.sourceName = "previous.xml";
    p.xmlBaselines = old;
    auto baseline = prepareBaseline(p, "기존 승인", "PM", "보존", "2026-11-15T08:00:00Z");
    baseline.id = "native-approved";
    baseline.version = 1;
    p.baselines.push_back(baseline);
    ++p.revision;
    validate(p);
    return p;
}
std::vector<XmlBaselineLink> mergeLinks(const ProjectXmlExchange &source, std::vector<int> numbers = {1}) {
    auto selected = adoptXmlBaselines(source, options(std::move(numbers)));
    auto links = xmlBaselineLinks(*selected.project.xmlBaselines);
    for (auto &l : links)
        if (l.kind != "project")
            l.targetId = "native-" + l.sourceId;
    return links;
}
void mergeContract() {
    auto source = fixture();
    auto target = mergeTarget(source);
    const auto links = mergeLinks(source);
    auto submission = prepareXmlBaselineMerge(target, source, options(), links, false);
    auto result = mergeXmlBaselineSets(target.xmlBaselines, submission);
    check(result.origins.size() == 2 && xmlBaselineOrigin(result, 0).weekdays == std::vector<int>{1},
          "old number calendar retained");
    check(xmlBaselineOrigin(result, 0).sourceName == "previous.xml" &&
              xmlBaselineOrigin(result, 1).sourceName != "previous.xml",
          "per-number provenance");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("merge.ppm").toStdString();
    store.save(path, target, {});
    Document doc(store);
    doc.open(path);
    auto prepared = Document::prepareEdit(doc.makeEditRequest(XmlBaselineMergeEdit{submission}), {});
    if (prepared->failure())
        throw AppError(*prepared->failure());
    check(prepared->state() == EditState::Ready, "worker prepares baseline merge");
    check(*doc.project() == target, "preview does not mutate target");
    check(doc.applyPreparedEdit(*prepared) == EditState::Applied && doc.dirty(), "atomic merge marks document dirty");
    auto p = *doc.project();
    check(p.xmlBaselineMerges.size() == 1 && p.baselines == target.baselines && p.progress == target.progress &&
              p.periodActuals == target.periodActuals,
          "native history and ledgers retained");
    auto plan = static_cast<PlanInput>(p);
    plan.xmlBaselines = target.xmlBaselines;
    check(plan == static_cast<const PlanInput &>(target), "merge changes no current plan inputs");
    verify(p);
    check(baselineExchangeValues(p, 0)->baselineDurationCalendar->calendars[0].weekdays == std::vector<int>{1},
          "formula uses old frozen calendar");
    check(baselineExchangeValues(p, 1)->baselineDurationCalendar->calendars[0].weekdays == submission.incoming.weekdays,
          "formula uses new frozen calendar");
    check(nativeSchemaFor(p) == 76 && json::decodeProject(json::encodeProject(p), 76) == p, "schema76 merge roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 75); }, "schema75 rejects merge extensions");
    auto broken = p;
    broken.xmlBaselineMerges[0].after.rows[0].work = 123;
    rejects([&] { validate(broken); }, "stored after must replay from before and links");
    broken = p;
    broken.xmlBaselines->rows[0].work = 123;
    rejects([&] { validate(broken); }, "current merge head matches recorded after");
    auto body = json::encodeProject(p);
    auto set = body["xmlBaselines"].toObject();
    auto origins = set["origins"].toArray();
    origins.append(origins[0]);
    set["origins"] = origins;
    body["xmlBaselines"] = set;
    rejects([&] { json::decodeProject(body, 76); }, "duplicate stored origins rejected");
    rejects([&] { prepareXmlBaselineMerge(p, source, options(), links, false); },
            "existing external number requires replacement confirmation");
    rejects([&] { prepareXmlBaselineMerge(target, source, options({0}), mergeLinks(source, {0}), false); },
            "native number replacement explicit");
    auto invalidLinks = links;
    invalidLinks.pop_back();
    rejects([&] { prepareXmlBaselineMerge(target, source, options(), invalidLinks, false); },
            "incomplete mappings rejected");
    invalidLinks = links;
    invalidLinks.push_back(links[0]);
    rejects([&] { prepareXmlBaselineMerge(target, source, options(), invalidLinks, false); },
            "duplicate source mappings rejected");
    invalidLinks = links;
    std::string firstTask;
    for (auto &l : invalidLinks)
        if (l.kind == "task") {
            if (firstTask.empty())
                firstTask = l.targetId;
            else
                l.targetId = firstTask;
        }
    rejects([&] { prepareXmlBaselineMerge(target, source, options(), invalidLinks, false); },
            "many-to-one task mapping rejected");
    invalidLinks = links;
    invalidLinks[0].targetId = "does-not-exist";
    rejects([&] { prepareXmlBaselineMerge(target, source, options(), invalidLinks, false); },
            "missing current target rejected");
    auto noAssignment = target;
    noAssignment.assignments.clear();
    rejects([&] { prepareXmlBaselineMerge(noAssignment, source, options(), links, false); },
            "does not create missing assignment");
    auto incompatible = target;
    incompatible.resources[0].type = "nonlabor";
    rejects([&] { prepareXmlBaselineMerge(incompatible, source, options(), links, false); },
            "resource units preserved");
    auto foreign = target;
    foreign.currency = "EUR";
    rejects([&] { prepareXmlBaselineMerge(foreign, source, options(), links, false); },
            "different currencies rejected");
    auto changed = source;
    changed.xml.replace("PT30M", "PT31M");
    rejects([&] { prepareXmlBaselineMerge(target, changed, options(), links, false); },
            "source bytes tampering rejected");
    changed = source;
    changed.baselineCandidates[0].row.work = 1;
    rejects([&] { prepareXmlBaselineMerge(target, changed, options(), links, false); }, "candidate tampering rejected");
    auto excluded = links;
    for (auto &l : excluded)
        if (l.kind == "task" && l.sourceId == source.project.tasks[0].id) {
            l.exclude = true;
            l.targetId.clear();
        }
    auto partial = prepareXmlBaselineMerge(p, source, options(), excluded, true);
    auto partialProject = p;
    partialProject.xmlBaselines = mergeXmlBaselineSets(p.xmlBaselines, partial);
    ++partialProject.revision;
    {
        ScheduleFormulaScope scope(partialProject, nullptr, ScheduleFormulaMode::Active, false);
        check(get(partialProject, "TW").missing && get(partialProject, "TW", FieldScope::Task, 1).value == "0",
              "whole number replace: excluded task missing, explicit zero retained");
    }
    check(partialProject.xmlBaselines->origins.at(0) == p.xmlBaselines->origins.at(0),
          "unselected origin unchanged after replacement");
    auto replace = prepareXmlBaselineMerge(p, source, options(), links, true);
    auto stale = Document::prepareEdit(doc.makeEditRequest(XmlBaselineMergeEdit{replace}), {});
    doc.setDuration(0, 479);
    check(doc.applyPreparedEdit(*stale) == EditState::Stale && doc.project()->xmlBaselineMerges.size() == 1,
          "stale prepared merge not applied");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto cancelled = Document::prepareEdit(doc.makeEditRequest(XmlBaselineMergeEdit{replace}), control);
    check(cancelled->state() == EditState::Cancelled && doc.project()->xmlBaselineMerges.size() == 1,
          "cancel leaves document intact");
    JsonRecoveryStore recovery(tmp.filePath("recovery"));
    auto recoverySession = recovery.newSession();
    recoverySession->checkpoint(doc.recoverySnapshot("2026-11-15T09:03:00Z"));
    recoverySession.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid,
          "merge recovery checksum and schema76");
    doc.save();
    const auto reopenedPath = tmp.filePath("reopened.ppm").toStdString();
    store.save(reopenedPath, p, {});
    Document reopened(store);
    reopened.open(reopenedPath);
    check(reopened.project()->xmlBaselineMerges == p.xmlBaselineMerges, "merge audit survives save/open");
    reopened.recordXmlBaselineMerge(partial);
    check(reopened.project()->xmlBaselineMerges.size() == 2 && reopened.project()->baselines == target.baselines,
          "replacement appends before/after history without overwriting native approval");
    {
        ScheduleFormulaScope scope(*reopened.project(), nullptr, ScheduleFormulaMode::Active, false);
        check(get(*reopened.project(), "TW").missing, "explicit replacement does not retain omitted former row");
    }
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.recordXmlBaselineMerge(replace); }, "read-only target unchanged");
    CurrencyRate rate{"USD", "EUR", "2026-11-15", "merge currency"};
    rate.numerator = 1;
    rate.denominator = 2;
    auto converted = convertProjectCurrency(p, rate);
    check(converted.xmlBaselineMerges == p.xmlBaselineMerges, "historical original currency and mapping immutable");
    check(json::decodeProject(json::encodeProject(converted), 76) == converted, "converted merge history roundtrip");
    auto observed = p;
    observed.statusAt = "2026-11-16 18:00";
    observed.progress.push_back(actualEntry(observed, "merge-observed", observed.tasks[0].id, *observed.statusAt,
                                            "2026-11-16 09:00", {}, 60, 420));
    ++observed.revision;
    auto migrated = migrateLedgerIdentities(observed, {{"task", p.tasks[0].id, "NEW-MERGE-A"}});
    check(migrated.xmlBaselineMerges == p.xmlBaselineMerges &&
              json::decodeProject(json::encodeProject(migrated), 76) == migrated,
          "ID migration retains original merge audit and remaps active rows");
    verify(migrated);
    const auto observedPath = tmp.filePath("observed.ppm").toStdString();
    store.save(observedPath, observed, {});
    Document observedDoc(store);
    observedDoc.open(observedPath);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(*observedDoc.project());
    selection.adoptedAt = "2026-11-15T09:00:00Z";
    selection.reason = "보존할 평준화";
    selection.sourceDocumentId = observedDoc.project()->documentId;
    selection.sourceRevision = observedDoc.project()->revision;
    observedDoc.adoptLeveling(selection);
    const auto beforeMerge = *observedDoc.project();
    const auto scheduleBefore = calculate(beforeMerge);
    observedDoc.recordXmlBaselineMerge(partial);
    check(observedDoc.project()->progress == observed.progress && observedDoc.project()->statusAt == observed.statusAt,
          "nonempty observations and status retained by replacement");
    check(observedDoc.project()->planLeveling == beforeMerge.planLeveling &&
              observedDoc.project()->remainingLeveling == beforeMerge.remainingLeveling,
          "existing leveling selection retained");
    const auto scheduleAfter = calculate(*observedDoc.project());
    check(scheduleBefore.tasks == scheduleAfter.tasks && scheduleBefore.projectFinish == scheduleAfter.projectFinish,
          "baseline merge preserves adopted current execution schedule");
    auto output = exportProjectXml(p);
    auto imported = importProjectXml(output.xml, "merged-export.xml");
    verify(adoptXmlBaselines(imported, options()).project);
    const auto dir = QDir::currentPath() + "/xml-baseline-merge-qa";
    QDir().mkpath(dir);
    save(dir + "/source.xml", source.xml);
    QFile::remove(dir + "/merged.ppm");
    store.save((dir + "/merged.ppm").toStdString(), p, {});
    save(dir + "/merged.xml", output.xml);
    save(dir + "/mapping.json", QJsonDocument(output.report).toJson());
    for (const auto &name : {"fields.xlsx", "fields.pdf"})
        QFile::remove(dir + "/" + name);
    ReportOptions reportOptions;
    reportOptions.reportId = "baseline-merge";
    reportOptions.generatedAt = "2026-11-15T09:00:00Z";
    auto report = buildReport(p, reportOptions);
    exportXlsx(report, dir + "/fields.xlsx");
    check(exportPdf(report, dir + "/fields.pdf") > 0, "merged baseline reports");
    std::cout << "PASS baseline merge: explicit mappings/replacement, per-number origins/calendars, immutable "
                 "ledgers/history, schema76/recovery/currency/ID, cancel/stale/read-only\n";
}
void mergeUi() {
    auto input = fixture();
    auto target = mergeTarget(input);
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("ui.ppm").toStdString();
    store.save(path, target, {});
    Document doc(store);
    doc.open(path);
    bool inspected = false;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlBaselineMergeDialog" || inspected)
            return;
        inspected = true;
        auto *numbers = dialog->findChild<QTableWidget *>("xmlBaselineMergeSlots");
        auto *table = dialog->findChild<QTableWidget *>("xmlBaselineMergeLinks");
        for (int i = 0; i < numbers->rowCount(); ++i) {
            check(numbers->item(i, 0)->checkState() == Qt::Unchecked, "merge slots opt-in");
            if (numbers->item(i, 0)->data(Qt::UserRole).toInt() == 1)
                numbers->item(i, 0)->setCheckState(Qt::Checked);
        }
        const auto links = mergeLinks(input);
        check(table->rowCount() == int(links.size()), "one mapping per source object");
        for (int i = 0; i < table->rowCount(); ++i) {
            auto *combo = qobject_cast<QComboBox *>(table->cellWidget(i, 2));
            check(combo->currentIndex() == 0, "no guessed object matches");
            combo->setCurrentIndex(links[std::size_t(i)].kind == "project"
                                       ? 2
                                       : combo->findData(QString::fromStdString(links[std::size_t(i)].targetId)));
        }
        dialog->findChild<QLineEdit *>("xmlBaselineMergeReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlBaselineMergeReason")->setText("기존 문서 병합 검토");
        QTimer::singleShot(80, dialog, [dialog] {
            const auto dir = QDir::currentPath() + "/xml-baseline-merge-qa";
            QDir().mkpath(dir);
            check(dialog->grab().save(dir + "/review.png"), "merge UI screenshot");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    });
    timer.start();
    check(reviewXmlBaselineMerge(nullptr, doc, input) && inspected, "merge UI applies reviewed mapping");
    timer.stop();
    verify(*doc.project());
    const auto beforeCancel = *doc.project();
    QTimer cancel;
    QObject::connect(&cancel, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (d && d->objectName() == "xmlBaselineMergeDialog")
            d->reject();
    });
    cancel.start(25);
    check(!reviewXmlBaselineMerge(nullptr, doc, input) && *doc.project() == beforeCancel,
          "cancelled review no mutation");
    cancel.stop();
    std::cout << "PASS baseline merge UI: unchecked numbers, explicit links, value review, worker apply and cancel\n";
}
void ui() {
    auto input = fixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    Document doc(store);
    bool inspected = false;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlBaselineDialog")
            return;
        auto *table = dialog->findChild<QTableWidget *>("xmlBaselineSlots");
        auto *detail = dialog->findChild<QTableView *>("xmlBaselineValues");
        check(table && detail && detail->model()->rowCount() == 16, "virtual baseline detail preview");
        if (inspected)
            return;
        inspected = true;
        for (int row = 0; row < table->rowCount(); ++row) {
            check(table->item(row, 0)->checkState() == Qt::Unchecked, "default unchecked");
            if (table->item(row, 0)->data(Qt::UserRole).toInt() == 1)
                table->item(row, 0)->setCheckState(Qt::Checked);
        }
        dialog->findChild<QLineEdit *>("xmlBaselineReporter")->setText("PM");
        dialog->findChild<QLineEdit *>("xmlBaselineReason")->setText("UI 검토");
        const auto output = QDir::currentPath() + "/xml-baseline-exchange-qa";
        QDir().mkpath(output);
        check(dialog->grab().save(output + "/review.png"), "baseline review screenshot");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer.start();
    auto result = reviewXmlBaselines(nullptr, doc, input);
    timer.stop();
    check(inspected && result.has_value(), "UI adoption completed");
    verify(result->project);
    std::cout << "PASS external baseline UI: unchecked slots, actual-value preview, worker adoption\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--timephased"))
            timephased();
        else if (app.arguments().contains("--merge-ui"))
            mergeUi();
        else if (app.arguments().contains("--merge"))
            mergeContract();
        else if (app.arguments().contains("--ui"))
            ui();
        else if (app.arguments().contains("--native"))
            native();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &arg : error->diagnostic().arguments)
                std::cerr << ' ' << arg;
        return 1;
    }
}
