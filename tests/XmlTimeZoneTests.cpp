#include "XmlTimeZoneFixture.h"
#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/NativeSchema.h"
#include "domain/XmlTimeZone.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/TaskTableModel.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <algorithm>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    check(bad, "expected rejection");
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source, CustomFieldType type = CustomFieldType::Start) {
    auto it = std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &f) { return f.id == "probe"; });
    if (it == p.customFields.end()) {
        p.customFields.push_back({"probe", "검사", type, "", {}});
        it = std::prev(p.customFields.end());
    }
    it->type = type;
    it->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    it->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:probe", &p.tasks[0]);
}
void verify(const Project &p) {
    EvaluationScope scope;
    check(value(p, "utc") == "2026-11-13T00:30:12.123456" && value(p, "utc", 1) == "2026-11-13T03:45:12.123456",
          "source zones and micros");
    check(value(p, "local") == "2026-11-12T19:30:12.123456" && value(p, "local", 1) == "2026-11-13T12:45:12.123456",
          "target civil values");
    check(value(p, "wire") == "2026-11-12T19:30:12.123456-05:00" &&
              value(p, "wire", 1) == "2026-11-13T12:45:12.123456+09:00",
          "offset-bearing strings");
    check(value(p, "offset") == "32400" && value(p, "offset", 1) == "20700" && value(p, "native") == "540" &&
              value(p, "native", 1) == "345",
          "integer offset seconds and native chain");
    check(value(p, "parsed") == value(p, "utc") && value(p, "parsed", 1) == value(p, "utc", 1) &&
              value(p, "elapsed") == "0",
          "roundtrip absolute instants");
    check(value(p, "date") == "2026-11-13" && value(p, "flag") == "1", "date/Flag outputs");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:error", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:null", &p.tasks[0]);
    check(!e.value && !e.missing && e.issue.find("존재하지") != std::string::npos && !n.value && n.missing,
          "gap/error/null distinct; no snapshot fallback");
    check(resolvedProjectAttributeValue(p, "field:project") == "2026-11-13T11:00:00" &&
              resolvedAttributeValue(p, p.resources[0], "field:resource") == "-14400",
          "project/resource zones");
    const RollDownValues roll(p);
    for (int i = 0; i < 2; ++i)
        check(roll.value(p.assignments[i], "field:assignment") == value(p, "local", i) &&
                  roll.value(p.assignments[i], "field:text") == value(p, "wire", i) &&
                  roll.value(p.assignments[i], "field:received") == value(p, "wire", i),
              "assignment references and roll-down");
    check(RollUpValues(p).cell("", "field:summary").value == "2026-11-13T09:00:00+09:00", "summary conversion");
}
void transitions(const QString &out) {
    QFile source(QString::fromUtf8(PPM_TIMEZONE_DATA) + "/tzdb-2026f.inc");
    check(source.open(QIODevice::ReadOnly), "embedded transition source");
    std::map<int, std::vector<qint64>> points;
    std::map<int, std::string> representatives;
    std::vector<std::string> names;
    for (auto line : source.readAll().split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.startsWith("R\"PPMTZ("))
            continue;
        line = line.mid(8);
        line.chop(8); // )PPMTZ",
        const auto cols = line.split('\t');
        if (cols[0] == "P") {
            for (const auto &part : cols[2].split(','))
                points[cols[1].toInt()].push_back(part.left(part.indexOf(':')).toLongLong());
        } else {
            const auto id = cols[2].toInt();
            const auto name = "2026f:" + cols[1].toStdString();
            representatives.try_emplace(id, name);
            names.push_back(name);
        }
    }
    check(names.size() == 597 && points.size() == 306 && representatives.size() == 306, "all named zones and patterns");
    QByteArray rows = "zone\tutc\toffset\tcivil\n";
    std::size_t count = 0;
    const auto record = [&](const std::string &zone, const std::string &utc) {
        const auto a = xmlTimeZoneAtUtc(utc, zone);
        rows += QByteArray::fromStdString(zone + '\t' + utc + '\t' + std::to_string(a.offsetSeconds) + '\t' + a.civil +
                                          '\n');
        ++count;
    };
    for (const auto &[id, values] : points)
        for (const auto t : values)
            for (const auto delta : {-1, 0, 1}) {
                if (t == 0 && delta == -1)
                    continue;
                const auto whole = t - (delta == -1 ? 1 : 0);
                const auto utc = QDateTime::fromSecsSinceEpoch(whole, QTimeZone::UTC)
                                     .toString("yyyy-MM-dd'T'HH:mm:ss")
                                     .toStdString() +
                                 (delta == -1  ? ".999999"
                                  : delta == 1 ? ".000001"
                                               : "");
                record(representatives.at(id), utc);
            }
    for (const auto &name : names) {
        record(name, "2026-07-01T00:00:00.123456");
        record(name, "2026-12-01T00:00:00.654321");
    }
    check(count == 92079, "transition vector coverage");
    save(out + "/transitions.tsv", rows);
}
void contract() {
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    bool stopped = false;
    try {
        EvaluationScope scope(cancel);
        xmlTimeZoneConvert("2026-11-13", "2026f:Asia/Seoul", "UTC");
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "cancel before data work");
    const auto p = xmlTimeZoneFixture();
    validate(p);
    verify(p);
    check(maximumNativeSchema >= 53 && nativeSchemaFor(p) == 53 && json::decodeProject(json::encodeProject(p), 53) == p,
          "schema53");
    rejects([&] { json::decodeProject(json::encodeProject(p), 52); });
    check(nativeSchemaFor(xmlAdditionalFunctionFixture()) == 52, "previous additional functions keep52");
    auto disabled = p;
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(disabled, scope))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(nativeSchemaFor(disabled) == 44 && json::decodeProject(json::encodeProject(disabled), 44) == disabled,
          "legacy disabled timezone expressions read without activation");
    for (const auto &source :
         {"\"PPMTimeZone(x)\"", "[field:PPMParseUtc(]", "NotPPMTimeZone(1)", "\"x\"\"PPMTimeZone(\""}) {
        XmlFormula f;
        f.source = source;
        check(!xmlTimeZoneFormula(f), "schema scanner ignores literals/references/prefixes");
    }
    auto probe = p;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"PPMTimeZone(#2026-01-01T00:15:00#,\"+14:00\",\"-12:00\")", "2025-12-30T22:15:00"},
        {"PPMParseUtc(\"2026-11-13T09:30:00.123456+09:00\")", "2026-11-13T00:30:00.123456"},
        {"PPMParseUtc(\"2026-11-13T00:30:00.1Z\")", "2026-11-13T00:30:00.1"},
        {"PPMTimeZone(#2026-11-01T01:30:00.123456#,\"2026f:America/New_York\",\"UTC\",\"earlier\")",
         "2026-11-01T05:30:00.123456"},
        {"PPMTimeZone(#2026-11-01T01:30:00.123456#,\"2026f:America/New_York\",\"UTC\",\"later\")",
         "2026-11-01T06:30:00.123456"},
        {"PPMTimeZone(#2026-04-05T01:45:00#,\"2026f:Australia/Lord_Howe\",\"UTC\",\"earlier\")", "2026-04-04T14:45:00"},
        {"PPMTimeZone(#2026-04-05T01:45:00#,\"2026f:Australia/Lord_Howe\",\"UTC\",\"later\")", "2026-04-04T15:15:00"},
        {"PPMTimeZone(#2026-10-25T01:30:00#,\"2026f:Europe/Dublin\",\"UTC\",\"earlier\")", "2026-10-25T00:30:00"},
        {"PPMTimeZone(#2026-10-25T01:30:00#,\"2026f:Europe/Dublin\",\"UTC\",\"later\")", "2026-10-25T01:30:00"}};
    QJsonArray vectors;
    for (const auto &[source, expected] : cases) {
        check(eval(probe, source).value == expected, source.c_str());
        vectors.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(expected)}});
    }
    check(eval(probe, "PPMTimeZoneOffset(#1971-01-01T12:00:00#,\"2026f:Africa/Monrovia\")", CustomFieldType::Number)
                  .value == "-2670",
          "historical second offsets exact");
    check(eval(probe, "PPMTimeZoneOffset(#2026-11-13T12:00:00#,\"2026f:America/Havana\")", CustomFieldType::Number)
                  .value == "-14400",
          "2026f Cuban update");
    const auto early = xmlTimeZoneText("2026-11-01T05:30:00.123456", "UTC", "2026f:America/New_York");
    const auto late = xmlTimeZoneText("2026-11-01T06:30:00.123456", "UTC", "2026f:America/New_York");
    check(early == "2026-11-01T01:30:00.123456-04:00" && late == "2026-11-01T01:30:00.123456-05:00" &&
              xmlParseUtc(early) == "2026-11-01T05:30:00.123456" && xmlParseUtc(late) == "2026-11-01T06:30:00.123456",
          "fold distinctions survive text/UTC roundtrip");
    for (const auto &zone : {"UTC", "Z", "+00:00", "+14:00", "-14:00", "+05:45", "UTC+09:00"})
        check(xmlTimeZoneConvert(xmlTimeZoneConvert("2024-02-29T12:34:56.123456", "UTC", zone), zone, "UTC") ==
                  "2024-02-29T12:34:56.123456",
              "fixed offset inverse");
    check(xmlTimeZoneConvert("0001-01-01T00:00:00.000001", "UTC", "UTC") == "0001-01-01T00:00:00.000001" &&
              xmlTimeZoneConvert("9999-12-31T23:59:59.999999", "UTC", "UTC") == "9999-12-31T23:59:59.999999",
          "fixed full native range");
    for (const auto &source : {"PPMTimeZone(#2026-03-08T02:30:00#,\"2026f:America/New_York\",\"UTC\",\"later\")",
                               "PPMTimeZone(#2026-11-01T01:30:00#,\"2026f:America/New_York\",\"UTC\")",
                               "PPMTimeZone(#2026-10-04T02:15:00#,\"2026f:Australia/Lord_Howe\",\"UTC\")",
                               "PPMTimeZone(#2011-12-30T12:00:00#,\"2026f:Pacific/Apia\",\"UTC\")",
                               "PPMTimeZone(#2026-01-01#,\"Asia/Seoul\",\"UTC\")",
                               "PPMTimeZone(#2026-01-01#,\"2027a:Asia/Seoul\",\"UTC\")",
                               "PPMTimeZone(#2026-01-01#,\"2026f:Factory\",\"UTC\")",
                               "PPMTimeZone(#2026-01-01#,\"UTC\",\"UTC\",\"guess\")",
                               "PPMTimeZone(#2026-01-01#,\"UTC\",\"UTC\",1)",
                               "PPMTimeZone(#2026-01-01#,\"UTC\",\"+14:01\")",
                               "PPMTimeZone(#1969-01-01#,\"2026f:Etc/UTC\",\"UTC\")",
                               "PPMTimeZone(#2101-01-01#,\"UTC\",\"2026f:Asia/Seoul\")",
                               "PPMTimeZoneText(#1971-01-01T12:00:00#,\"UTC\",\"2026f:Africa/Monrovia\")",
                               "PPMTimeZone(#0001-01-01#,\"+14:00\",\"UTC\")",
                               "PPMTimeZone(#9999-12-31T23:59:59#,\"UTC\",\"+14:00\")",
                               "PPMParseUtc(\"2026-11-13T09:00:00-00:00\")",
                               "PPMParseUtc(\"2026-11-13T09:00:00\")",
                               "PPMParseUtc(\"2026-11-13T09:00:60Z\")",
                               "PPMParseUtc(\"2026-11-13T09:00:00.1234567Z\")",
                               "PPMParseUtc(\"2026-11-13T09:00:00+23:00\")",
                               "PPMParseUtc(#2026-11-13#)",
                               "PPMTimeZone(1,\"UTC\",\"UTC\")"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), source);
    }
    for (const auto &source :
         {"PPMTimeZone(Null,\"UTC\",\"UTC\")", "PPMParseUtc(Null)", "PPMTimeZoneOffset(Null,\"UTC\")"})
        check(eval(probe, source).missing, "Null propagation");
    for (const auto &source :
         {"PPMTimeZone(1,2)", "PPMParseUtc(1,2)", "PPMTimeZoneOffset(1)", "PPMTimeZoneText(1,2)"}) {
        auto bad = p;
        bad.customFields[4].xmlFormula = bindXmlFormula(bad, FieldScope::Task, source);
        bad.customFields[4].xmlFormula->enabled = true;
        rejects([&] { validate(bad); });
    }
    // Host TZ and locale cannot enter these deterministic values or their cache key.
    qputenv("TZ", "Pacific/Honolulu");
    verify(p);
    qputenv("TZ", "Europe/Paris");
    verify(p);
    qunsetenv("TZ");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("timezone.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto f = customField(*doc.project(), "field:utc")->xmlFormula;
    f->enabled = false;
    doc.setXmlFormula(FieldScope::Task, "field:utc", f);
    check(value(*doc.project(), "utc") == "1999-01-01T00:00:00", "disabled snapshot preserved");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:from");
    renamed.name = "명시 출발 시간대";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("시간대 동결", "PM", "원본 보존", "2026-11-13T03:00:00Z");
    doc.saveScenario("시간대 사본", "task-order", 260, "2026-11-13T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "시간대 정의 유지";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 53) == *doc.project(),
          "currency/baseline/scenario archives53");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &v : customFields(frozen, scope))
            v.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 53 && json::decodeProject(json::encodeProject(frozen), 53) == frozen,
          "frozen-only functions require53");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 52); });
    auto observed = *doc.project();
    clearTransitionHistories(observed);
    observed.statusAt = "2026-11-13 18:00";
    observed.costEntries = {{"AC",
                             "A",
                             *observed.statusAt,
                             "2026-11-13T03:03:00Z",
                             "PM",
                             "ID 이관",
                             {},
                             observed.documentId,
                             observed.currency,
                             observed.revision,
                             100,
                             {}}};
    const auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 53) == migrated, "ID archive53");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery53");
    doc.save();
    check(store.load(path).project == *doc.project(), "file53 roundtrip");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:utc", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "timezone.xml");
    check(hasXmlTimeZoneFunctions(imported.project, true), "XML imports PPM extension enabled");
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &field : customFields(p, scope)) {
            if (!field.xmlFormula)
                continue;
            const auto &fields = customFields(imported.project, scope);
            const auto it =
                std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return v.name == field.name; });
            check(it != fields.end() && it->xmlFormula && it->xmlFormula->enabled, "XML field-name bindings");
            if (scope == FieldScope::Task)
                for (int row = 0; row < 2; ++row)
                    check(resolvedAttributeValue(imported.project, imported.project.tasks[row], "field:" + it->id) ==
                              value(p, field.id, row),
                          "XML recalculation");
        }
    auto unknownXml = xml.xml;
    unknownXml.replace("PPMParseUtc(", "UnknownTimeZone(");
    const auto unsupported = importProjectXml(unknownXml, "unsupported.xml");
    check(std::any_of(unsupported.project.customFields.begin(), unsupported.project.customFields.end(),
                      [](const auto &v) {
                          return v.xmlFormula && !v.xmlFormula->enabled &&
                                 v.xmlFormula->source.find("UnknownTimeZone") != std::string::npos;
                      }),
          "unknown extension preserved/disabled");
    check(static_cast<const PlanInput &>(p).tasks == xmlTimeZoneFixture().tasks && p.periodActuals.empty() &&
              p.progress.empty(),
          "no schedule/actual timezone inference");
    const auto out = QDir::currentPath() + "/xml-timezone-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(out + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(out + "/timezone.xml", xml.xml);
    save(out + "/timezone.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(out + "/cases.json", QJsonDocument(vectors).toJson());
    transitions(out);
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-timezone";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "typed timezone reports");
    std::cout << "PASS timezone: explicit fixed/597 pinned IANA names, gaps/folds/half-hour/negative DST/skipped day, "
                 "exact microseconds/offset text/UTC, four scopes and cache/host independence, "
                 "schema53/legacy44/archives/recovery/XML/XER/reports; 92079 transition vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("timezone.ppm").toStdString();
    store.save(path, xmlTimeZoneFixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(!(model.flags(model.index(0, model.columnForKey("field:wire"))) & Qt::ItemIsEditable),
          "computed zone values read-only");
    int phase = 0;
    bool finished = false;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "xmlFormulaDialog" || d->property("xmlFormulaBusy").toBool())
            return;
        try {
            auto *table = d->findChild<QTableWidget *>("xmlFormulaPreview");
            auto *source = d->findChild<QPlainTextEdit *>("xmlFormulaSource");
            if (phase == 0) {
                check(table->rowCount() == 2 && table->item(0, 1)->text().endsWith("-05:00"), "offset text preview");
                auto *fn = d->findChild<QComboBox *>("xmlTimeZoneFunction");
                check(fn && fn->count() == 4 &&
                          d->findChild<QLabel *>("xmlFormulaHelp")->text().contains("2026f:Asia/Seoul"),
                      "explicit extension help");
                fn->setCurrentIndex(3);
                check(!d->findChild<QComboBox *>("xmlTimeZoneFrom")->isEnabled() &&
                          !d->findChild<QComboBox *>("xmlTimeZoneTo")->isEnabled(),
                      "ParseUtc has its own explicit offset input");
                fn->setCurrentIndex(1);
                d->findChild<QComboBox *>("xmlTimeZoneFrom")->setCurrentText("2026f:America/New_York");
                d->findChild<QComboBox *>("xmlTimeZoneTo")->setCurrentText("UTC");
                d->findChild<QComboBox *>("xmlTimeZoneOverlap")->setCurrentIndex(2);
                source->clear();
                d->findChild<QPushButton *>("insertXmlTimeZoneFormula")->click();
                check(source->toPlainText() ==
                          "PPMTimeZoneText([Start], \"2026f:America/New_York\", \"UTC\", \"later\")",
                      "explicit zone/policy insertion");
                source->setPlainText(
                    "PPMTimeZoneText(#2026-11-01T01:30:00.123456#,\"2026f:America/New_York\",\"UTC\",\"later\")");
                ++phase;
                d->findChild<QPushButton *>("previewXmlFormula")->click();
            } else {
                timer.stop();
                check(table->rowCount() == 2 && table->item(0, 1)->text() == "2026-11-01T06:30:00.123456Z",
                      "later-fold async preview");
                d->resize(800, 850);
                const auto out = QDir::currentPath() + "/xml-timezone-qa";
                QDir().mkpath(out);
                d->grab().save(out + "/timezone-small.png");
                d->findChild<QScrollArea *>("editorScrollArea")
                    ->verticalScrollBar()
                    ->setValue(d->findChild<QScrollArea *>("editorScrollArea")->verticalScrollBar()->maximum());
                d->grab().save(out + "/timezone-values-small.png");
                finished = true;
                d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            }
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:wire", nullptr);
    check(finished && value(*doc.project(), "wire") == "2026-11-01T06:30:00.123456Z", "timezone expression saved");
    doc.undo();
    verify(*doc.project());
    const auto before = *doc.project();
    QTimer::singleShot(0, [&] { qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject(); });
    editXmlFormula(doc, FieldScope::Task, "field:wire", nullptr);
    check(*doc.project() == before, "cancel preserved source");
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        doc.renameTask(0, "미리보기 후 변경");
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!d->findChild<QLabel *>("xmlFormulaError")->text().isEmpty(), "stale save blocked");
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:wire", nullptr);
    check(doc.project()->tasks[0].name == "미리보기 후 변경" && value(*doc.project(), "wire") == value(before, "wire"),
          "stale edit retained");
    std::cout << "PASS timezone UI: explicit zones/policy/parse controls, fold text preview, save/undo/cancel/stale, "
                 "800px scroll views\n";
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
        if (const auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &v : a->diagnostic().arguments)
                std::cerr << ' ' << v;
        return 1;
    }
}
