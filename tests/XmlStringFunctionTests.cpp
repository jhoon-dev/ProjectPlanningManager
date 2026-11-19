#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlStringFunctions.h"
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
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <limits>
namespace {
using namespace ppm;
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
QString output() {
    return QDir::currentPath() + "/xml-string-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-string";
    p.name = "XML 문자열 함수";
    p.customFields.push_back({"inputString", "문자입력", CustomFieldType::Text, "", {}});
    p.customFields.push_back({"needle", "찾을문자", CustomFieldType::Text, "", {}});
    for (int i = 0; i < 2; ++i) {
        p.tasks[i].attributes.push_back({"field:inputString", i ? "abcabc" : "가Ab가AB"});
        p.tasks[i].attributes.push_back({"field:needle", i ? "ab" : "가"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "replaced", "치환문자", CustomFieldType::Text, "Replace([문자입력],[찾을문자],\"X\")");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedString", true};
    add(FieldScope::Task, "reversed", "역순문자", CustomFieldType::Text, "StrReverse([문자입력])");
    add(FieldScope::Task, "index", "역검색위치", CustomFieldType::Number, "InStrRev([문자입력],[찾을문자])");
    add(FieldScope::Task, "limited", "시작횟수", CustomFieldType::Text, "Replace([문자입력],[찾을문자],\"X\",2,1)");
    add(FieldScope::Task, "partial", "끝경계검색", CustomFieldType::Number, "InStrRev(\"ababa\",\"aba\",4)");
    add(FieldScope::Task, "folded", "ASCII치환", CustomFieldType::Text, "Replace(\"AaAa\",\"aa\",\"가\",1,-1,1)");
    add(FieldScope::Task, "stringCost", "금액치환", CustomFieldType::Text, "Replace(CStr([입력금액]),\".\",\",\")");
    add(FieldScope::Task, "stringError", "문자열오류", CustomFieldType::Text, "StrReverse(Null)");
    add(FieldScope::Task, "stringNull", "문자열미설정", CustomFieldType::Number, "InStrRev(Null,\"x\")");
    add(FieldScope::Task, "stringSummary", "문자열요약", CustomFieldType::Text, "StrReverse(\"약요\")");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"stringNative", "문자열혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:index]*2";
    add(FieldScope::Project, "stringProject", "프로젝트역순", CustomFieldType::Text, "StrReverse(CStr([프로젝트수]))");
    add(FieldScope::Resource, "stringResource", "자원치환", CustomFieldType::Text, "Replace(\"a-a\",\"-\",\"/\")");
    p.assignmentFields.push_back({"receivedString", "문자열전파", CustomFieldType::Text, "", {}});
    add(FieldScope::Assignment, "stringAssignment", "배정역순", CustomFieldType::Text, "StrReverse([Task.문자입력])");
    p.tasks[0].attributes.push_back({"field:replaced", "stale"});
    p.tasks[0].attributes.push_back({"field:stringError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:replaced", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:replaced", &p.tasks[0]);
}
void verify(const Project &p) {
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> expected = {
        {"replaced", {"XAbXAB", "XcXc"}},
        {"reversed", {"BA가bA가", "cbacba"}},
        {"index", {"4", "4"}},
        {"limited", {"AbXAB", "bcXc"}},
        {"partial", {"1", "1"}},
        {"folded", {"가가", "가가"}}};
    for (const auto &[id, values] : expected)
        check(value(p, id) == values.first && value(p, id, 1) == values.second, "three string functions");
    check(value(p, "stringCost") == (p.currency == "EUR" ? "6,17" : "12,34") && value(p, "stringNative") == "8",
          "FX recompute and native chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:stringError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:stringNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error and Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:stringProject") == "61" &&
              resolvedAttributeValue(p, p.resources[0], "field:stringResource") == "a/a",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:stringAssignment") == "BA가bA가" &&
              roll.value(p.assignments[1], "field:stringAssignment") == "cbacba" &&
              roll.value(p.assignments[1], "field:receivedString") == "XcXc",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:stringSummary").value == "요약", "summary");
}
void vectors() {
    QJsonArray rows;
    std::uint64_t state = 941387;
    for (int i = 0; i < 1024; ++i) {
        const auto next = [&] {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return state;
        };
        const bool ascii = i % 2;
        const std::vector<std::string> alphabet = ascii
                                                      ? std::vector<std::string>{"A", "a", "B", "b", "-", " "}
                                                      : std::vector<std::string>{"가", "나", "é", "e", "́", "中", "Ａ"};
        const auto make = [&](int n) {
            std::string out;
            for (int j = 0; j < n; ++j)
                out += alphabet[next() % alphabet.size()];
            return out;
        };
        const auto source = make(int(next() % 40)), needle = make(int(next() % 4)), replacement = make(int(next() % 5));
        const int compare = ascii ? int(next() % 2) : 0;
        const auto start = std::int64_t(next() % 45 + 1), count = std::int64_t(next() % 6) - 1;
        const auto reverseStart = i % 3 == 0 ? -1 : std::int64_t(next() % 45 + 1);
        const auto reversed = xmlStrReverse(source);
        check(xmlStrReverse(reversed) == source, "BMP reverse involution");
        rows.append(QJsonObject{
            {"source", QString::fromStdString(source)},
            {"find", QString::fromStdString(needle)},
            {"replacement", QString::fromStdString(replacement)},
            {"start", int(start)},
            {"count", int(count)},
            {"reverseStart", int(reverseStart)},
            {"compare", compare},
            {"replaced", QString::fromStdString(xmlReplace(source, needle, replacement, start, count, compare))},
            {"position", int(xmlInStrRev(source, needle, reverseStart, compare))},
            {"reversed", QString::fromStdString(reversed)}});
    }
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlStrReverse("abc");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"Replace(\"abcabc\",\"ab\",\"X\")", "XcXc"},
        {"Replace(\"abcabc\",\"ab\",\"X\",3)", "cXc"},
        {"Replace(\"abcabc\",\"ab\",\"X\",3,0)", "cabc"},
        {"Replace(\"abcabc\",\"\",\"X\",3)", "cabc"},
        {"Replace(\"abc\",\"a\",\"X\",4)", ""},
        {"Replace(\"\",\"a\",\"X\")", ""},
        {"Replace(\"abc\",\"b\",\"\")", "ac"},
        {"Replace(\"aaaaa\",\"aa\",\"X\")", "XXa"},
        {"Replace(\"aaaa\",\"a\",\"aa\",1,2)", "aaaaaa"},
        {"Replace(\"ABCabc\",\"ab\",\"X\",1,-1,1)", "XCXc"},
        {"Replace(\"가나가\",\"가\",\"中\",2)", "나中"},
        {"Replace(\"abcabc\",\"ab\",\"X\",1,1)", "Xcabc"},
        {"InStrRev(\"ababa\",\"aba\")", "3"},
        {"InStrRev(\"ababa\",\"aba\",4)", "1"},
        {"InStrRev(\"ababa\",\"aba\",2)", "0"},
        {"InStrRev(\"가나가나\",\"가나\",3)", "1"},
        {"InStrRev(\"가나가나\",\"가나\")", "3"},
        {"InStrRev(\"abc\",\"\")", "3"},
        {"InStrRev(\"abc\",\"\",2)", "2"},
        {"InStrRev(\"abc\",\"\",4)", "0"},
        {"InStrRev(\"\",\"\")", "0"},
        {"InStrRev(\"ABab\",\"aB\",-1,1)", "3"},
        {"InStrRev(\"ab\",\"x\")", "0"},
        {"StrReverse(\"가Ab中\")", "中bA가"},
        {"StrReverse(\"\")", ""},
        {"StrReverse(\"é\")", "́e"},
        {"StrReverse(StrReverse(\"가나\"))", "가나"},
        {"strreverse(\"AbC\")", "CbA"},
        {"Replace(CStr(12),\"1\",\"2\")", "22"},
        {"IsNull(InStrRev(Null,\"a\"))", "True"},
        {"InStrRev(\"aaa\",\"aa\",2147483647)", "0"},
        {"Replace(\"a\",\"a\",\"X\",2147483647,2147483647)", ""}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected [" << expected << "] got [" << r.value.value_or(r.issue) << "]\n";
        check(r.value == expected, "string scalar case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"InStrRev(Null,\"a\")", "InStrRev(\"a\",Null)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "InStrRev Null strings");
    }
    for (const auto *source : {"Replace(Null,\"a\",\"b\")",
                               "Replace(\"a\",Null,\"b\")",
                               "Replace(\"a\",\"a\",Null)",
                               "StrReverse(Null)",
                               "Replace(\"a\",\"a\",\"b\",0)",
                               "Replace(\"a\",\"a\",\"b\",1,-2)",
                               "Replace(\"a\",\"a\",\"b\",1.5)",
                               "Replace(\"a\",\"a\",\"b\",1,Null)",
                               "Replace(\"a\",\"a\",\"b\",1,-1,2)",
                               "InStrRev(Null,\"a\",0)",
                               "InStrRev(\"a\",\"a\",Null)",
                               "InStrRev(\"a\",\"a\",-2)",
                               "InStrRev(\"a\",\"a\",1.5)",
                               "InStrRev(\"a\",\"a\",-1,-1)",
                               "Replace(\"가\",\"가\",\"a\",1,-1,1)",
                               "InStrRev(\"가\",\"가\",-1,1)",
                               "StrReverse(12)",
                               "InStrRev(12,\"a\")",
                               "Replace(\"a\",1,\"b\")",
                               "IIf(True,1,StrReverse(Null))"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "domain/type/control/eager error");
    }
    for (const auto *source : {"Replace(\"a\",\"b\")", "InStrRev(\"a\")", "StrReverse()", "StrReverse(\"a\",1)"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.issue.empty(), "invalid arity");
    }
    for (const auto &bad : {std::string("\xc0\xaf"), std::string("\xe0\x80\x80"), std::string("\xed\xa0\x80"),
                            std::string("\xf0\x9f\x98\x80"), std::string("\xe3\x81"), std::string("\x80")})
        rejects([&] { xmlStrReverse(bad); });
    check(xmlStrReverse(std::string(4096, 'a')).size() == 4096, "exact input boundary");
    check(xmlReplace(std::string(2048, 'a'), "a", "bb").size() == 4096, "exact output boundary");
    rejects([&] { xmlReplace(std::string(2049, 'a'), "a", "bb"); });
    rejects([&] { xmlStrReverse(std::string(4097, 'a')); });
    check(!xmlStringFormula({"CStr(\"Replace(1)\")", {}, true}) && !xmlStringFormula({"[StrReverse(1)]", {}, true}),
          "scanner ignores literal/ref");
    check(nativeSchemaFor(p) == 59 && json::decodeProject(json::encodeProject(p), 59) == p, "schema59");
    rejects([&] { json::decodeProject(json::encodeProject(p), 58); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlStringFormula(*f.xmlFormula))
                    f.xmlFormula->enabled = false;
                else
                    f.xmlFormula.reset();
            }
    check(nativeSchemaFor(legacy) == 44 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "disabled legacy44");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("numbers.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "StrReverse(\"가나다\")");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:replaced", changed);
    check(value(*doc.project(), "replaced") == "다나가", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "replaced") == "다나가", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:inputString");
    renamed.name = "명시 문자열 입력";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("변환 동결", "PM", "원본 보존", "2026-11-14T03:00:00Z");
    doc.saveScenario("변환 사본", "task-order", 260, "2026-11-14T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-14", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "변환 원본";
    fx.recordedAt = "2026-11-14T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 59) == *doc.project(), "FX archives59");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 59 && json::decodeProject(json::encodeProject(frozen), 59) == frozen,
          "frozen-only59");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 58); });
    auto observed = *doc.project();
    clearTransitionHistories(observed);
    observed.statusAt = "2026-11-14 18:00";
    observed.costEntries = {{"AC",
                             "A",
                             *observed.statusAt,
                             "2026-11-14T03:03:00Z",
                             "PM",
                             "ID 이관",
                             {},
                             observed.documentId,
                             observed.currency,
                             observed.revision,
                             100,
                             {}}};
    const auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 59) == migrated, "ID archives59");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery59");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:replaced", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "numbers.xml");
    check(hasXmlStringFunctions(imported.project, true), "XML active");
    for (const auto &f : p.customFields) {
        const auto it = std::find_if(imported.project.customFields.begin(), imported.project.customFields.end(),
                                     [&](const auto &v) { return v.name == f.name; });
        check(it != imported.project.customFields.end(), "XML field");
        for (int i = 0; i < 2; ++i)
            check(value(p, f.id, i) == value(imported.project, it->id, i), "XML recalc");
    }
    QDir().mkpath(output());
    save(output() + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(output() + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(output() + "/numbers.xml", xml.xml);
    save(output() + "/numbers.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-string";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Replace/InStrRev/StrReverse: BMP/ASCII/suffix/count/end-boundary/Null/errors, "
                 "scopes/archives/59/XML/XER/reports; 1024 vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("numbers.ppm").toStdString();
    store.save(path, fixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:replaced"))).toString() == "XAbXAB",
          "string table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlStringFunction");
            check(fn && fn->count() == 4, "string examples");
            source->clear();
            fn->setCurrentIndex(3);
            d->findChild<QPushButton *>("insertXmlStringFormula")->click();
            check(source->toPlainText() == "StrReverse(\"가나다\")", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "다나가")
                        found = true;
            check(found, "string preview");
            QDir().mkpath(output());
            check(d->grab().save(output() + "/editor.png"), "editor capture");
            d->resize(800, 650);
            if (auto *scroll = d->findChild<QScrollArea *>())
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            check(d->grab().save(output() + "/editor-small.png"), "small capture");
            timer.stop();
            done = true;
            d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:replaced", nullptr);
    check(done && value(*doc.project(), "replaced") == "다나가", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("StrReverse(Null)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:replaced", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS string UI: examples/async preview/save/undo/cancel and small layout\n";
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
