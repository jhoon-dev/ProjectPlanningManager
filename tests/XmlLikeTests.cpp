#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlLike.h"
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
using namespace jobs;
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
    return QDir::currentPath() + "/xml-like-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-like";
    p.name = "XML 패턴 비교";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "match", "문자패턴", CustomFieldType::Flag, "[입력문자] Like \"*가*\"");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedLike", true};
    add(FieldScope::Task, "digit", "숫자패턴", CustomFieldType::Flag, "CStr([입력수]) Like \"#.#\"");
    add(FieldScope::Task, "costMatch", "금액패턴", CustomFieldType::Flag, "CStr([입력금액]) Like \"12.*\"");
    add(FieldScope::Task, "matchNumber", "패턴숫자", CustomFieldType::Number, "IIf([입력문자] Like \"*가*\",1,0)");
    add(FieldScope::Task, "escaped", "리터럴패턴", CustomFieldType::Flag, "\"a*b\" Like \"a[*]b\"");
    add(FieldScope::Task, "likeError", "패턴오류", CustomFieldType::Text, "\"x\" Like \"[\"");
    add(FieldScope::Task, "likeNull", "패턴미설정", CustomFieldType::Text, "[미설정] Like \"*\"");
    add(FieldScope::Task, "likeSummary", "패턴요약", CustomFieldType::Flag, "\"A1\" Like \"[A-Z]#\"");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"likeNative", "패턴혼합식", CustomFieldType::Number, "", {}, "[field:matchNumber]*2"});
    add(FieldScope::Project, "likeProject", "프로젝트패턴", CustomFieldType::Flag, "CStr([프로젝트수]) Like \"##\"");
    add(FieldScope::Resource, "likeResource", "자원패턴", CustomFieldType::Flag, "[Name] Like \"*비\"");
    p.assignmentFields.push_back({"receivedLike", "패턴전파", CustomFieldType::Flag, "", {}});
    add(FieldScope::Assignment, "likeAssignment", "배정패턴", CustomFieldType::Flag, "[Task.입력문자] Like \"*가*\"");
    p.tasks[0].attributes.push_back({"field:match", "0"});
    p.tasks[0].attributes.push_back({"field:likeError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:match", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:match", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "match") == "1" && value(p, "match", 1) == "0", "BMP match");
    check(value(p, "digit") == "1" && value(p, "digit", 1) == "1" && value(p, "escaped") == "1", "digit/escape");
    check(value(p, "costMatch") == (p.currency == "EUR" ? "0" : "1") && value(p, "likeNative") == "2",
          "FX recompute/native chain");
    auto e = xmlFormulaValue(p, FieldScope::Task, "field:likeError", &p.tasks[0]),
         n = xmlFormulaValue(p, FieldScope::Task, "field:likeNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error/Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:likeProject") == "1" &&
              resolvedAttributeValue(p, p.resources[0], "field:likeResource") == "1",
          "project/resource");
    RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:likeAssignment") == "1" &&
              roll.value(p.assignments[1], "field:likeAssignment") == "0" &&
              roll.value(p.assignments[1], "field:receivedLike") == "0",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:likeSummary").value == "1", "summary");
}
void vectors() {
    QJsonArray rows;
    const auto add = [&](const std::string &text, const std::string &pattern) {
        rows.append(QJsonObject{{"text", QString::fromStdString(text)},
                                {"pattern", QString::fromStdString(pattern)},
                                {"matched", xmlLikeMatch(text, pattern)}});
    };
    std::vector<std::string> texts{""}, patterns{""}, layer{""};
    for (int n = 0; n < 4; ++n) {
        std::vector<std::string> next;
        for (const auto &s : layer)
            for (const auto *c : {"a", "b"})
                next.push_back(s + c);
        texts.insert(texts.end(), next.begin(), next.end());
        layer = std::move(next);
    }
    layer = {""};
    for (int n = 0; n < 3; ++n) {
        std::vector<std::string> next;
        for (const auto &s : layer)
            for (const auto *c : {"a", "b", "?", "*", "#"})
                next.push_back(s + c);
        patterns.insert(patterns.end(), next.begin(), next.end());
        layer = std::move(next);
    }
    for (const auto &s : texts)
        for (const auto &p : patterns)
            add(s, p);
    std::uint64_t state = 941387;
    const auto next = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    };
    const std::vector<std::string> alphabet = {"a", "b", "0", "9", "가", "나", "*", "?", "#",
                                               "[", "]", "!", "-", "Z",  "é",  " ", "\t"};
    const std::vector<std::string> tokens = {"a",    "b",      "0",     "가",      "?",   "*",      "#",
                                             "[ab]", "[!0-9]", "[a-z]", "[가-힣]", "[]",  "[*]",    "[?]",
                                             "[#]",  "[[]",    "]",     "[!]",     "[-]", "[a-b-c]"};
    const auto escape = [](const std::string &c) {
        return c == "[" ? "[[]" : c == "*" ? "[*]" : c == "?" ? "[?]" : c == "#" ? "[#]" : c;
    };
    for (int i = 0; i < 4096; ++i) {
        std::string text, pattern;
        const auto length = next() % 13;
        for (std::uint64_t j = 0; j < length; ++j)
            text += alphabet[next() % alphabet.size()];
        if (i % 2 == 0) {
            pattern = "*"; // Build a positive pattern from BMP slices.
            const auto q = QString::fromUtf8(text);
            for (auto c : q)
                pattern += next() % 3 == 0 ? "?" : escape(QString(c).toUtf8().toStdString());
            pattern += "*";
        } else {
            const auto count = next() % 7;
            for (std::uint64_t j = 0; j < count; ++j)
                pattern += tokens[next() % tokens.size()];
        }
        add(text, pattern);
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
            (void)xmlLikeMatch("a", "*");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"\"abc\" Like \"a*\"", "True"},       {"\"abc\" Like \"a?c\"", "True"},
        {"\"a2a\" Like \"a#a\"", "True"},      {"\"a٢a\" Like \"a#a\"", "False"},
        {"\"A\" Like \"a\"", "False"},         {"\"가나다\" Like \"가?다\"", "True"},
        {"\"힣\" Like \"[가-힣]\"", "True"},   {"\"a\" Like \"[!0-9]\"", "True"},
        {"\"\" Like \"*\"", "True"},           {"\"\" Like \"[]\"", "True"},
        {"\"x\" Like \"[]x\"", "True"},        {"\"!\" Like \"[!]\"", "True"},
        {"\"-\" Like \"[-]\"", "True"},        {"\"*\" Like \"[*]\"", "True"},
        {"\"?\" Like \"[?]\"", "True"},        {"\"#\" Like \"[#]\"", "True"},
        {"\"[\" Like \"[[]\"", "True"},        {"\"]\" Like \"[]]\"", "True"},
        {"\"b\" Like \"[a-b-c]\"", "True"},    {"\"-\" Like \"[a-b-c]\"", "True"},
        {"\".\" Like \"[--0]\"", "True"},      {"\"b\" Like \"[a-cx-z]\"", "True"},
        {"Not \"abc\" Like \"a*\"", "False"},  {"\"a\" & \"b\" Like \"a?\"", "True"},
        {"\"a\" Like \"a\" Eqv True", "True"}, {"\"a\" Like \"a\" = True", "True"},
        {"True Or True Xor True", "True"},     {"(True Or True Xor True) Eqv (\"a\" Like \"b\")", "True"},
        {"CStr(2.5) Like \"#.#\"", "True"},    {"IsNull(Null Like \"[\")", "True"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " got " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected, "Like scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"Null Like \"*\"", "\"a\" Like Null", "Null Like Null"}) {
        auto r = eval(probe, source);
        check(!r.value && r.missing, "Null propagation");
    }
    for (const auto *source : {"\"x\" Like \"[\"", "\"\" Like \"[\"", "\"x\" Like \"a[\"", "\"x\" Like \"[z-a]\"",
                               "2 Like \"#\"", "True Like \"*\"", "#2024-01-01# Like \"*\"", "Like(\"a\",\"*\")",
                               "\"a\" Like1", "\"a\" Like_2", "IIf(True,1,\"x\" Like \"[\")"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "pattern/type/eager/token error");
    }
    for (const auto *bad : {"[", "[a", "[z-a]", "[a--]"})
        rejects([&] { xmlLikeMatch("x", bad); });
    rejects([&] { xmlLikeMatch(std::string(4097, 'a'), "*"); });
    rejects([&] { xmlLikeMatch("😀", "?"); });
    rejects([&] { xmlLikeMatch("a", std::string("\xc0\xaf", 2)); });
    check(xmlLikeMatch(std::string(4096, 'a'), std::string(4096, 'a')) && xmlLikeMatch(std::string(4096, 'a'), "***"),
          "linear and star limits");
    rejects([&] { xmlLikeMatch(std::string(4096, 'a'), "*" + std::string(1000, '?')); });
    check(!xmlLikeFormula({"CStr(\"Like\")", {}, true}) && !xmlLikeFormula({"[field:Like\\]X]", {}, true}) &&
              !xmlLikeFormula({"Likely(1)", {}, true}) && !xmlLikeFormula({"\"a\" Like1", {}, true}),
          "scanner boundaries/escapes");
    check(nativeSchemaFor(p) == 65 && json::decodeProject(json::encodeProject(p), 65) == p, "schema65");
    rejects([&] { json::decodeProject(json::encodeProject(p), 64); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlLikeFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "\"x\" Like \"y\"");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:match", changed);
    check(value(*doc.project(), "match") == "0", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "match") == "0", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:q");
    renamed.name = "명시 숫자 입력";
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 65) == *doc.project(), "FX archives65");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 65 && json::decodeProject(json::encodeProject(frozen), 65) == frozen,
          "frozen-only65");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 64); });
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
    check(json::decodeProject(json::encodeProject(migrated), 65) == migrated, "ID archives65");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery65");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:match", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlLikeOperator(imported.project, true), "XML active");
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
    save(output() + "/operators.xml", xml.xml);
    save(output() + "/operators.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-like";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Like: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema65/archives/recovery/XML/XER/reports; 8932 vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:match"))).toString() == "예", "Like table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlLikeOperator");
            check(fn && fn->count() == 4, "math examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlLikeFormula")->click();
            check(source->toPlainText() == "\"ABC123\" Like \"[A-Z]*###\"", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "예")
                        found = true;
            check(found, "math preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:match", nullptr);
    check(done && value(*doc.project(), "match") == "1", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("\"x\" Like \"[\"");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:match", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS Like UI: examples/async preview/save/undo/cancel and small layout\n";
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
