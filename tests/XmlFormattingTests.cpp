#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlFormatting.h"
#include "domain/FormulaFields.h"
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
#include <sstream>
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
const auto output = [] { return QDir::currentPath() + "/xml-formatting-qa"; };
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-format-strconv";
    p.name = "XML 표시·문자 변환";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, const std::string &source) {
        customFields(p, scope).push_back({id, name, CustomFieldType::Text, "", {}});
        auto &f = customFields(p, scope).back();
        f.xmlFormula = bindXmlFormula(p, scope, source);
        f.xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "formatted", "표시금액", "Format([입력금액],\"#,##0.00\")");
    add(FieldScope::Task, "localized", "한국날짜", "PPMFormat([입력일시],\"Long Date\",1042)");
    add(FieldScope::Task, "converted", "문자변환", "StrConv(\"ｶﾞ hello WORLD\",7,1041)");
    add(FieldScope::Task, "missingFormatted", "빈값표시", "Format(Null,\"0;0;0;미설정\")");
    add(FieldScope::Project, "display", "프로젝트표시", "Format([프로젝트수],\"0.0\")");
    add(FieldScope::Resource, "display", "자원표시", "StrConv(\"éCOLE\",3,1042)");
    add(FieldScope::Assignment, "display", "배정표시", "Format([Task.바이트변환],\"0.00\")");
    p.tasks[0].attributes.push_back({"field:formatted", "stale"});
    return p;
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:formatted", FieldScope::Task));
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:formatted", &p.tasks[0]);
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
void contract() {
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    check(value(p, "formatted") == "12.34" && value(p, "converted") == "ガ　Ｈｅｌｌｏ　Ｗｏｒｌｄ" &&
              value(p, "missingFormatted") == "미설정",
          "computed text, fullwidth proper and Null section");
    check(resolvedProjectAttributeValue(p, "field:display") == "16.0" &&
              resolvedAttributeValue(p, p.resources[0], "field:display") == "École" &&
              RollDownValues(p).value(p.assignments[0], "field:display") == "2.00",
          "all four scopes");
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"Format(1.25,\"0.0\")", "1.3"},
        {"Format(-1.25,\"0.0\")", "-1.3"},
        {"Format(2.5,\"0\")", "3"},
        {"Format(0,\"#.##\")", "."},
        {"Format(123,\"0.##\")", "123."},
        {"Format(0.125,\"Percent\")", "12.50%"},
        {"Format(-1234.5,\"Currency\")", "($1,234.50)"},
        {"PPMFormat(-1234.5,\"Currency\",1042)", "-₩1,235"},
        {"Format(123456789,\"#,##0,,\")", "123"},
        {"Format(123456789,\"0,,.00\")", "123.46"},
        {"Format(1234567000,\"000,0,,\")", "1,235"},
        {"Format(-123,\"0;;ZERO;NULL\")", "-123"},
        {"Format(0,\"0;;ZERO;NULL\")", "ZERO"},
        {"Format(Null,\"0;;ZERO;NULL\")", "NULL"},
        {"Format(9999,\"Scientific\")", "1.00E+04"},
        {"Format(0.000001,\"Scientific\")", "1.00E-06"},
        {"Format(9000000000000,\"0.00%\")", "900000000000000.00%"},
        {"Format(True)", "True"},
        {"Format(True,\"0\")", "-1"},
        {"Format(False,\"Yes/No\")", "No"},
        {"Format(1.234567)", "1.234567"},
        {"Format(\"abc\",\"@@-@@\")", " a-bc"},
        {"Format(\"abc\",\"&&-&&\")", "a-bc"},
        {"Format(\"abc\",\"!@@-@@\")", "ab-c "},
        {"Format(\"abcdef\",\"@@-@@\")", "ab-cdef"},
        {"Format(\"abc\",\">@@@\")", "ABC"},
        {"Format(\"\",\"@@@;빈값\")", "빈값"},
        {"Format(#2026-11-13T09:30:12.123456#,\"yyyy-mm-dd hh:nn:ss\")", "2026-11-13 09:30:12"},
        {"Format(#2026-11-13T13:04:05#,\"hh:mm AM/PM\")", "01:04 PM"},
        {"Format(#2026-11-13T13:04:05#,\"hh:mm am/pm\")", "01:04 pm"},
        {"Format(#2026-11-13T13:04:05#,\"h:nn a/P\")", "1:04 p"},
        {R"(Format(#2026-11-13T13:04:05#,"hh \A\M/\P\M"))", "13 AM/PM"},
        {"Format(#2026-11-13T13:04:05#,\"ww w y q\")", "46 6 317 4"},
        {"Format(#2021-01-01#,\"ww\",2,2)", "53"},
        {"PPMFormat(#2026-11-13#,\"Long Date\",1042)", "2026년 11월 13일 금요일"},
        {"PPMFormat(#2026-11-13#,\"dddd mmmm\",1041)", "金曜日 11月"},
        {R"(Format(#2026-11-13#,"yyyy \Y"))", "2026 Y"},
        {"StrConv(\"straße ΟΣ\",1)", "STRASSE ΟΣ"},
        {"StrConv(\"ΟΣ ΟΣΑ\",2)", "ος οσα"},
        {"StrConv(\"hello-world o'NEIL 123ABC\",3)", "Hello-world O'neil 123abc"},
        {"StrConv(\"ｶﾞﾊﾟABC\",36,1041)", "がぱＡＢＣ"},
        {"StrConv(\"がガ\",24,1041)", "ｶﾞｶﾞ"},
        {"StrConv(\"ㄱﾡ\",4,1042)", "ㄱㄱ"},
        {"StrConv(\"ㄱﾡ\",8,1042)", "ﾡﾡ"},
        {"StrConv(\"😀𐐀\",2)", "😀𐐨"},
        {"StrConv(\"ｶﾞ\",32,1041)", "ｶﾞ"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " actual " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected && r.issue.empty() && !r.missing, "format/StrConv case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto &source : {"Format(Null)", "StrConv(Null,1)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "Null distinct");
    }
    for (const auto &source : {"Format(1,0)",
                               "Format(1,\"0.0000000\")",
                               "Format(1,\"0.00,,\")",
                               "Format(1,\"0.,,\")",
                               "Format(1,\"EUR 0.00\")",
                               "Format(1,\"[Red]0\")",
                               "Format(1,\"yyyy\")",
                               "Format(1,\"Long Date\")",
                               "Format(1,\"0\",8)",
                               "Format(Null,\"0\",1,4)",
                               "PPMFormat(1,\"0\",0)",
                               "PPMFormat(1,\"0\",999)",
                               "StrConv(1,1)",
                               "StrConv(Null,64)",
                               "StrConv(\"a\",128)",
                               "StrConv(\"a\",12,1041)",
                               "StrConv(\"a\",48,1041)",
                               "StrConv(\"a\",4)",
                               "StrConv(\"a\",16,1042)",
                               "StrConv(\"a\",1.5)",
                               "StrConv(\"a\",1,0)",
                               "Format(\"abcdef\",\"!@@\")",
                               "StrConv(String(2048,\"İ\"),2)"}) {
        try {
            const auto r = eval(probe, source);
            if (r.value || r.missing || r.issue.empty())
                std::cerr << "should fail " << source << '\n';
            check(!r.value && !r.missing && !r.issue.empty(), "unsupported controls do not use snapshot");
        } catch (const AppError &) { /* syntax/compile rejection is also explicit */
        }
    }
    rejects([&] { xmlStrConv(std::string("\xf0\x80\x80\x80", 4), 0); });
    check(!xmlFormattingFormula(XmlFormula{"CStr(\"Format(1)\")", {}, true}) &&
              !xmlFormattingFormula(XmlFormula{"[Format(1)]", {}, true}),
          "schema scanner ignores quoted names/references");
    check(nativeSchemaFor(p) == 54 && json::decodeProject(json::encodeProject(p), 54) == p, "schema54 roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 53); });
    auto disabled = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(disabled, scope))
            if (f.xmlFormula && xmlFormattingFormula(*f.xmlFormula))
                f.xmlFormula->enabled = false;
    check(nativeSchemaFor(disabled) == 52 && json::decodeProject(json::encodeProject(disabled), 52) == disabled,
          "disabled source survives old schema");
    auto legacy = disabled;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.id != "formatted")
                f.xmlFormula.reset();
    check(nativeSchemaFor(legacy) == 44 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "disabled formatting source readable in legacy44");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("format.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "Format(2.5,\"0\")");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:formatted", changed);
    check(value(*doc.project(), "formatted") == "3", "document recalculation");
    doc.undo();
    check(value(*doc.project(), "formatted") == "12.34", "undo");
    doc.redo();
    check(value(*doc.project(), "formatted") == "3", "redo");
    doc.undo();
    doc.captureBaseline("서식 기준선", "PM", "원본 보존", "2026-11-13T03:00:00Z");
    doc.saveScenario("서식 시나리오", "task-order", 260, "2026-11-13T03:00:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "표시 계산 검증";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(value(*doc.project(), "formatted") == "6.17" &&
              json::decodeProject(json::encodeProject(*doc.project()), 54) == *doc.project(),
          "FX converts original Cost and recalculates Text, schema54 archives");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 54 && json::decodeProject(json::encodeProject(frozen), 54) == frozen,
          "archives retain schema54");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 53); });
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
    check(json::decodeProject(json::encodeProject(migrated), 54) == migrated && value(migrated, "formatted") == "6.17",
          "ledger ID archive54");
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "schema54 recovery");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:formatted", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "format.xml");
    check(hasXmlFormattingFunctions(imported.project, true), "XML supported functions activated");
    for (const auto &f : p.customFields) {
        if (!f.xmlFormula)
            continue;
        const auto it = std::find_if(imported.project.customFields.begin(), imported.project.customFields.end(),
                                     [&](const auto &x) { return f.name == x.name; });
        check(it != imported.project.customFields.end(), "XML field");
        check(value(p, f.id) == value(imported.project, it->id), "XML recalculated formatting");
    }
    QDir().mkpath(output());
    save(output() + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(output() + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(output() + "/format.xml", xml.xml);
    save(output() + "/format.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    QJsonArray numbers;
    std::uint64_t state = 712683;
    for (int i = 0; i < 512; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto n = std::int64_t(state % 18000000000000001ULL) - 9000000000000000LL;
        for (const auto *mask : {"0.00", "#,##0.000", "0.00%", "0,,.00", "0.00E+00"}) {
            const auto r = xmlFormat({XmlFormatKind::Number, n, {}}, mask);
            check(r.has_value(), "numeric vectors");
            numbers.append(
                QJsonObject{{"scaled", QString::number(n)}, {"mask", mask}, {"value", QString::fromStdString(*r)}});
        }
    }
    save(output() + "/numbers.json", QJsonDocument(numbers).toJson());
    QJsonArray dates;
    for (int i = 0; i < 64; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto date = QString("%1-%2-%3T13:04:05")
                              .arg(1 + state % 9999, 4, 10, QChar('0'))
                              .arg(1 + (state / 9999) % 12, 2, 10, QChar('0'))
                              .arg(1 + (state / 119988) % 28, 2, 10, QChar('0'))
                              .toStdString();
        for (int firstDay = 1; firstDay <= 7; ++firstDay)
            for (int firstWeek = 1; firstWeek <= 3; ++firstWeek) {
                const auto r = xmlFormat({XmlFormatKind::Date, 0, date}, "yyyy-mm-dd hh:nn:ss w ww y q", 1033, firstDay,
                                         firstWeek);
                dates.append(QJsonObject{{"input", QString::fromStdString(date)},
                                         {"firstDay", firstDay},
                                         {"firstWeek", firstWeek},
                                         {"value", QString::fromStdString(*r)}});
            }
    }
    save(output() + "/dates.json", QJsonDocument(dates).toJson());
    QJsonArray unicodeVectors;
    QFile table(QString::fromUtf8(JOBS_UNICODE_DATA) + "/unicode-16.0.0.inc");
    check(table.open(QIODevice::ReadOnly), "Unicode input scalars");
    std::set<std::uint32_t> points;
    for (std::uint32_t cp = 0x3040; cp <= 0x30ff; ++cp)
        points.insert(cp);
    for (const auto &line : table.readAll().split('\n')) {
        if (!line.startsWith("C ") && !line.startsWith("W "))
            continue;
        auto cols = line.split(' ');
        points.insert(cols[1].toUInt(nullptr, 16));
        if (line.startsWith("W "))
            points.insert(cols[2].toUInt(nullptr, 16));
    }
    for (auto cp : points) {
        const char32_t scalar = cp;
        const auto s = QString::fromUcs4(&scalar, 1).toUtf8().toStdString();
        for (int flags : {1, 2, 3, 4, 8, 16, 32, 36, 24})
            unicodeVectors.append(QJsonObject{{"input", QString::fromUtf8(s)},
                                              {"flags", flags},
                                              {"value", QString::fromUtf8(xmlStrConv(s, flags, 1041))}});
    }
    save(output() + "/unicode.json", QJsonDocument(unicodeVectors).toJson());
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + name);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-format-strconv";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Format/PPMFormat/StrConv: four scopes, decimals/sections/date/Unicode/null/errors, "
                 "schema54/archives/recovery, XML/XER/reports; "
              << numbers.size() << " numeric and " << unicodeVectors.size() << " Unicode vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("format.ppm").toStdString();
    store.save(path, fixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:formatted"))).toString() == "12.34",
          "table formatted text");
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
            auto *combo = d->findChild<QComboBox *>("xmlFormattingFunction");
            auto *locale = d->findChild<QComboBox *>("xmlFormattingLocale");
            check(combo && combo->count() == 7 && locale && locale->count() == 3, "format examples and profiles");
            source->clear();
            combo->setCurrentIndex(3);
            d->findChild<QPushButton *>("insertXmlFormattingFormula")->click();
            check(source->toPlainText() == "StrConv(\"hello WORLD\", 3, 1042)", "explicit locale insertion");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            check(preview && preview->rowCount() >= 2, "async preview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "Hello World")
                        found = true;
            check(found, "preview text");
            QDir().mkpath(output());
            check(d->grab().save(output() + "/editor.png"), "editor screenshot");
            d->resize(760, 600);
            if (auto *scroll = d->findChild<QScrollArea *>())
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            check(d->grab().save(output() + "/editor-small.png"), "small editor screenshot");
            timer.stop();
            done = true;
            d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:formatted", nullptr);
    check(done && value(*doc.project(), "formatted") == "Hello World", "UI save");
    doc.undo();
    check(value(*doc.project(), "formatted") == "12.34", "UI undo");
    std::cout << "PASS formatting UI: locale examples, asynchronous preview/save/undo, table text and small layout\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Korean font");
    const auto japanese =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/msgothic.ttc"));
    QFont reportFont;
    QStringList families{QFontDatabase::applicationFontFamilies(font).front()};
    // Supplemental Japanese fonts are optional on Windows CI. When installed,
    // explicitly load them because the offscreen platform omits OS discovery.
    if (japanese >= 0)
        families.push_back(QFontDatabase::applicationFontFamilies(japanese).front());
    reportFont.setFamilies(families);
    reportFont.setPointSize(9);
    QApplication::setFont(reportFont);
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
