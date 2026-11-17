#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlDateFunctions.h"
#include "domain/XmlFormatting.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/TaskTableModel.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDate>
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
#include <iostream>
namespace {
using namespace jobs;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "expected rejection");
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
QString output() {
    return QDir::currentPath() + "/xml-date-functions-qa";
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-date-functions";
    p.name = "XML 날짜 계산 확장";
    p.customFields.push_back({"interval", "동적간격", CustomFieldType::Text, "", {}});
    for (int i = 0; i < 2; ++i) {
        for (auto &a : p.tasks[i].attributes)
            if (a.key == "field:when")
                a.value = i ? "2023-12-31T23:59:59.654321" : "2024-01-31T09:30:12.123456";
        p.tasks[i].attributes.push_back({"field:interval", i ? "w" : "m"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "quarter", "날짜분기", CustomFieldType::Number, "DatePart(\"q\",[입력일시])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedDate", true};
    add(FieldScope::Task, "weekday", "날짜요일", CustomFieldType::Number, "Weekday([입력일시],2)");
    add(FieldScope::Task, "week", "날짜주차", CustomFieldType::Number, "DatePart(\"ww\",[입력일시],2,2)");
    add(FieldScope::Task, "shift", "월말이동", CustomFieldType::Finish, "DateAdd(\"m\",1,[입력일시])");
    add(FieldScope::Task, "difference", "월경계", CustomFieldType::Number,
        "DateDiff(\"m\",[입력일시],DateAdd(\"m\",1,[입력일시]))");
    add(FieldScope::Task, "dynamic", "동적이동", CustomFieldType::Finish, "DateAdd([동적간격],1,[입력일시])");
    add(FieldScope::Task, "dateError", "날짜범위오류", CustomFieldType::Finish, "DateAdd(\"yyyy\",1,#9999-12-31#)");
    add(FieldScope::Task, "dateNull", "날짜미설정", CustomFieldType::Number, "Weekday(Null)");
    add(FieldScope::Task, "dateSummary", "날짜요약", CustomFieldType::Number, "DatePart(\"q\",#2024-11-13#)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"dateNative", "날짜혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:quarter]*10";
    add(FieldScope::Project, "dateProject", "프로젝트연도", CustomFieldType::Number, "DatePart(\"yyyy\",#2024-02-29#)");
    add(FieldScope::Resource, "dateResource", "자원요일", CustomFieldType::Number, "Weekday(#2024-02-29#)");
    p.assignmentFields.push_back({"receivedDate", "분기전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "dateAssignment", "배정분기이동", CustomFieldType::Finish,
        "DateAdd(\"q\",-1,[Task.입력일시])");
    p.tasks[0].attributes.push_back({"field:shift", "1999-01-01T00:00:00"});
    p.tasks[0].attributes.push_back({"field:dateError", "2000-01-01T00:00:00"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source, CustomFieldType type = CustomFieldType::Number) {
    auto *f = const_cast<CustomField *>(customField(p, "field:quarter", FieldScope::Task));
    f->type = type;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:quarter", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "quarter") == "1" && value(p, "quarter", 1) == "4" && value(p, "weekday") == "3" &&
              value(p, "weekday", 1) == "7",
          "quarter and Monday weekday");
    check(value(p, "shift") == "2024-02-29T09:30:12.123456" && value(p, "shift", 1) == "2024-01-31T23:59:59.654321",
          "month clamp and micros");
    check(value(p, "difference") == "1" && value(p, "difference", 1) == "1" &&
              value(p, "dynamic", 1) == "2024-01-01T23:59:59.654321",
          "month boundary and late binding");
    check(value(p, "dateNative") == "10" && value(p, "dateNative", 1) == "40", "native numeric chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:dateError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:dateNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error and Null never snapshots");
    check(resolvedProjectAttributeValue(p, "field:dateProject") == "2024" &&
              resolvedAttributeValue(p, p.resources[0], "field:dateResource") == "5",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:dateAssignment") == "2023-10-31T09:30:12.123456" &&
              roll.value(p.assignments[1], "field:dateAssignment") == "2023-09-30T23:59:59.654321" &&
              roll.value(p.assignments[1], "field:receivedDate") == "4",
          "assignment and roll-down");
    check(RollUpValues(p).cell("", "field:dateSummary").value == "4", "summary date expression");
}
void vectors() {
    QJsonArray parts, adds, diffs;
    std::uint64_t state = 391287;
    const auto generate = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        int y = 100 + int(state % 9700), m = 1 + int(state / 9700 % 12);
        QDate date(y, m, 1);
        date = QDate(y, m, (state & 1) ? date.daysInMonth() : 1 + int(state / 116400 % 28));
        return date.toString("yyyy-MM-dd").toStdString() + "T23:59:59.123456";
    };
    for (int i = 0; i < 512; ++i) {
        const auto a = generate(), b = generate();
        const auto count = std::int64_t(state % 73) - 36;
        for (const auto *interval : {"yyyy", "q", "m", "y", "d", "w", "ww", "h", "n", "s"}) {
            adds.append(QJsonObject{{"interval", interval},
                                    {"count", qint64(count)},
                                    {"input", QString::fromStdString(a)},
                                    {"value", QString::fromStdString(xmlDateAdd(interval, count, a))}});
            for (int firstDay = 1; firstDay <= (std::string(interval) == "ww" ? 7 : 1); ++firstDay)
                diffs.append(QJsonObject{{"interval", interval},
                                         {"from", QString::fromStdString(a)},
                                         {"to", QString::fromStdString(b)},
                                         {"firstDay", firstDay},
                                         {"value", QString::number(xmlDateDifference(interval, a, b, firstDay))}});
            if (i >= 128)
                continue;
            for (int firstDay = 1; firstDay <= (std::string(interval) == "ww" || std::string(interval) == "w" ? 7 : 1);
                 ++firstDay)
                for (int firstWeek = 1; firstWeek <= (std::string(interval) == "ww" ? 3 : 1); ++firstWeek) {
                    const auto result = xmlDatePart(interval, a, firstDay, firstWeek);
                    if (std::string(interval) == "ww")
                        check(xmlFormat({XmlFormatKind::Date, 0, a}, "ww", 1033, firstDay, firstWeek) ==
                                  std::to_string(result),
                              "DatePart and Format week agreement");
                    parts.append(QJsonObject{{"interval", interval},
                                             {"input", QString::fromStdString(a)},
                                             {"firstDay", firstDay},
                                             {"firstWeek", firstWeek},
                                             {"value", QString::number(result)}});
                }
        }
    }
    save(output() + "/parts.json", QJsonDocument(parts).toJson());
    save(output() + "/adds.json", QJsonDocument(adds).toJson());
    save(output() + "/diffs.json", QJsonDocument(diffs).toJson());
}
void contract() {
    {
        auto cancelled = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancelled);
        cancelled->cancel();
        bool unwound = false;
        try {
            (void)xmlDatePart("ww", "2024-01-01", 2, 2);
        } catch (const CalculationCancelled &) {
            unwound = true;
        }
        check(unwound, "date functions propagate cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"DatePart(\"yyyy\",#2024-02-29#)", "2024"},
        {"DatePart(\"q\",#2024-12-31#)", "4"},
        {"DatePart(\"y\",#2024-12-31#)", "366"},
        {"DatePart(\"y\",#1900-12-31#)", "365"},
        {"DatePart(\"m\",#2024-02-29#)", "2"},
        {"DatePart(\"d\",#2024-02-29#)", "29"},
        {"DatePart(\"h\",#2024-02-29T23:59:58.999999#)", "23"},
        {"DatePart(\"n\",#2024-02-29T23:59:58.999999#)", "59"},
        {"DatePart(\"s\",#2024-02-29T23:59:58.999999#)", "58"},
        {"DatePart(\"ww\",#2021-01-01#,2,2)", "53"},
        {"DatePart(\"ww\",#2018-12-31#,2,2)", "53"},
        {"DatePart(\"ww\",#2000-12-31#,1,1)", "54"},
        {"Weekday(#2026-11-13#)", "6"},
        {"Weekday(#2026-11-13#,2)", "5"},
        {"Weekday(#2026-11-13#,0)", "6"},
        {"DateDiff(\"yyyy\",#2023-12-31T23:59:59#,#2024-01-01#)", "1"},
        {"DateDiff(\"q\",#2024-03-31#,#2024-04-01#)", "1"},
        {"DateDiff(\"m\",#2024-01-31#,#2024-02-01#)", "1"},
        {"DateDiff(\"m\",#2024-02-01#,#2024-01-31#)", "-1"},
        {"DateDiff(\"y\",#1969-12-31T23:59:59.999999#,#1970-01-01#)", "1"},
        {"DateDiff(\"w\",#2026-11-02T23:59:59#,#2026-11-09#)", "1"},
        {"DateDiff(\"w\",#2026-11-02#,#2026-11-08#)", "0"},
        {"DateDiff(\"w\",#2026-11-09#,#2026-11-02T23:59:59#)", "-1"},
        {"DateDiff(\"ww\",#2026-11-07T23:59:59#,#2026-11-08#,1,3)", "1"},
        {"DateDiff(\"ww\",#2026-11-07T23:59:59#,#2026-11-08#,2,2)", "0"},
        {"DateDiff(\"ww\",#2026-11-08#,#2026-11-07T23:59:59#,1)", "-1"},
        {"DateDiff(\"s\",#1969-12-31T23:59:59.999999#,#1970-01-01#)", "1"},
        {"DatePart(\"q\",PPMTimeZone(#2024-03-31T20:00:00#,\"UTC\",\"+09:00\"))", "2"},
        {"DatePart(\"Q\",CDate(\"2024-04-01\"))", "2"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " got " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected, "numeric date case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto &[source, expected] : std::vector<std::pair<std::string, std::string>>{
             {"DateAdd(\"m\",1,#2024-01-31T09:30:12.123456#)", "2024-02-29T09:30:12.123456"},
             {"DateAdd(\"yyyy\",1,#2024-02-29#)", "2025-02-28T00:00:00"},
             {"DateAdd(\"q\",-1,#2024-05-31#)", "2024-02-29T00:00:00"},
             {"DateAdd(\"m\",-1,#2024-01-31#)", "2023-12-31T00:00:00"},
             {"DateAdd(\"y\",1,#2024-12-31#)", "2025-01-01T00:00:00"},
             {"DateAdd(\"w\",1,#2026-11-13#)", "2026-11-14T00:00:00"},
             {"DateAdd(\"ww\",1,#2026-11-13#)", "2026-11-20T00:00:00"},
             {"DateAdd(\"yyyy\",-2023,#2024-02-29#)", "0001-02-28T00:00:00"},
             {"DateAdd(\"m\",0,#9999-12-31T23:59:59.999999#)", "9999-12-31T23:59:59.999999"}}) {
        const auto r = eval(probe, source, CustomFieldType::Finish);
        check(r.value == expected, "date result case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source :
         {"Weekday(Null)", "DatePart(\"q\",Null)", "DateDiff(\"m\",Null,#2024-01-01#)", "DateAdd(\"m\",1,Null)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "Null propagation");
    }
    for (const auto *source :
         {"Weekday(#2024-01-01#,8)", "Weekday(Null,8)", "DatePart(\"yyyy\",Null,1,4)", "DatePart(\"qq\",#2024-01-01#)",
          "DatePart(\"q\",\"2024-01-01\")", "DateDiff(\"ww\",Null,#2024-01-01#,1,4)", "DateAdd(\"m\",1.5,#2024-01-01#)",
          "DateAdd(\"m\",1,#9999-12-31#)", "DateAdd(\"yyyy\",-1,#0001-01-01#)", "DateAdd(\"ww\",10000000,#2024-01-01#)",
          "DateAdd(\"m\",10000001,#2024-01-01#)"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "invalid inputs explicit");
    }
    check(!xmlExtendedDateFormula({"CStr(\"DatePart(1)\")", {}, true}) &&
              !xmlExtendedDateFormula({"[Weekday(1)]", {}, true}),
          "capability scanner ignores literals/refs");
    check(!xmlExtendedDateFormula({"DateAdd((\"D\"),1,#2024-01-01#)", {}, true}) &&
              xmlExtendedDateFormula({"DateAdd(IIf(True,\"d\",\"m\"),1,#2024-01-01#)", {}, true}),
          "static legacy and dynamic writer rules");
    check(nativeSchemaFor(p) == 55 && json::decodeProject(json::encodeProject(p), 55) == p, "schema55");
    rejects([&] { json::decodeProject(json::encodeProject(p), 54); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.id != "shift")
                f.xmlFormula.reset();
    check(nativeSchemaFor(legacy) == 55 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "old three-argument interval documents still read, new writer55");
    auto *shift = const_cast<CustomField *>(customField(legacy, "field:shift", FieldScope::Task));
    shift->xmlFormula = bindXmlFormula(legacy, FieldScope::Task, "DateAdd(\"d\",1,[입력일시])");
    shift->xmlFormula->enabled = true;
    check(nativeSchemaFor(legacy) == 44 && value(legacy, "shift") == "2024-02-01T09:30:12.123456",
          "legacy static unchanged");
    shift->xmlFormula = bindXmlFormula(legacy, FieldScope::Task, "DateAdd(IIf(True,\"d\",\"m\"),1,[입력일시])");
    shift->xmlFormula->enabled = true;
    check(json::decodeProject(json::encodeProject(legacy), 44) == legacy && nativeSchemaFor(legacy) == 55,
          "legacy late binding readable");
    shift->xmlFormula->enabled = false;
    check(nativeSchemaFor(legacy) == 44 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "disabled raw preserved");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("dates.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "Weekday(#2024-02-29#)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:quarter", changed);
    check(value(*doc.project(), "quarter") == "5", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "quarter") == "5", "redo");
    doc.undo();
    doc.captureBaseline("날짜 동결", "PM", "날짜 원본", "2026-11-13T03:00:00Z");
    doc.saveScenario("날짜 사본", "task-order", 260, "2026-11-13T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "날짜 원본";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 55) == *doc.project(), "FX archives55");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 55 && json::decodeProject(json::encodeProject(frozen), 55) == frozen,
          "frozen-only55");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 54); });
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
    check(json::decodeProject(json::encodeProject(migrated), 55) == migrated, "ID archive55");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery55");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:quarter", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "dates.xml");
    check(hasXmlExtendedDateFunctions(imported.project, true), "XML active");
    for (const auto &f : p.customFields) {
        const auto it = std::find_if(imported.project.customFields.begin(), imported.project.customFields.end(),
                                     [&](const auto &v) { return f.name == v.name; });
        check(it != imported.project.customFields.end(), "XML field");
        for (int i = 0; i < 2; ++i)
            check(value(p, f.id, i) == value(imported.project, it->id, i), "XML recalc");
    }
    QDir().mkpath(output());
    save(output() + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(output() + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(output() + "/dates.xml", xml.xml);
    save(output() + "/dates.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-date-functions";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS extended dates: four scopes, leap/month end/micros/week boundaries, legacy44/new55, "
                 "archive/FX/ID/recovery, XML/XER/reports; 4608 parts/5120 additions/8192 differences\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("dates.ppm").toStdString();
    store.save(path, fixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:shift"))).toString().contains("2024-02-29"),
          "table date");
    bool done = false;
    int phase = 0;
    QTimer timer;
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d)
            return;
        auto *source = d->findChild<QPlainTextEdit *>("xmlFormulaSource");
        if (!phase) {
            auto *fn = d->findChild<QComboBox *>("xmlDateFunction");
            auto *fd = d->findChild<QComboBox *>("xmlDateFirstDay");
            auto *fw = d->findChild<QComboBox *>("xmlDateFirstWeek");
            check(fn && fn->count() == 4 && fd->count() == 7 && fw->count() == 3, "date controls");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlDateFormula")->click();
            check(source->toPlainText() == "DatePart(\"ww\", #2026-11-13#, 2, 2)", "week options inserted");
            source->clear();
            fn->setCurrentIndex(2);
            check(!fd->isEnabled() && !fw->isEnabled(), "unused date-add options disabled");
            d->findChild<QPushButton *>("insertXmlDateFormula")->click();
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text().contains("2024-02-29"))
                        found = true;
            check(found, "month clamp preview");
            QDir().mkpath(output());
            check(d->grab().save(output() + "/editor.png"), "editor capture");
            d->resize(800, 650);
            if (auto *scroll = d->findChild<QScrollArea *>())
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            check(d->grab().save(output() + "/editor-small.png"), "small preview capture");
            timer.stop();
            done = true;
            d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:shift", nullptr);
    check(done && value(*doc.project(), "shift", 1) == "2024-02-29T09:30:12.123456", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Weekday(Null,8)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:quarter", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout
        << "PASS date UI: four examples, week options, month-end async preview/save/undo/cancel and small layout\n";
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
            for (const auto &s : a->diagnostic().arguments)
                std::cerr << ' ' << s;
        return 1;
    }
}
