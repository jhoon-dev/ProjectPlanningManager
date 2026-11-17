#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlPartition.h"
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
#include <tuple>
#include <chrono>
#include "domain/XmlDateFunctions.h"
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
    return QDir::currentPath() + "/xml-partition-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-partition";
    p.name = "XML 수치 구간";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "partition", "수치구간", CustomFieldType::Text, "Partition([입력수],0,99,3)");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedPartition", true};
    add(FieldScope::Task, "roundedPartition", "반올림구간", CustomFieldType::Text, "Partition(2.5,0,9,2.5)");
    add(FieldScope::Task, "outsidePartition", "범위전구간", CustomFieldType::Text, "Partition(-1,0,99,5)");
    add(FieldScope::Task, "lastPartition", "마지막구간", CustomFieldType::Text, "Partition(1010,100,1010,20)");
    add(FieldScope::Task, "partitionCost", "금액구간", CustomFieldType::Text, "Partition([입력금액],0,99,5)");
    add(FieldScope::Task, "partitionError", "구간오류", CustomFieldType::Text, "Partition(1,0,99,0)");
    add(FieldScope::Task, "partitionNull", "구간미설정", CustomFieldType::Text, "Partition([미설정],0,99,5)");
    add(FieldScope::Task, "partitionSummary", "구간요약", CustomFieldType::Text, "Partition(101,0,99,5)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    add(FieldScope::Task, "partitionLength", "구간길이", CustomFieldType::Number, "Len(Partition([입력수],0,99,5))");
    p.customFields.push_back(
        {"partitionNative", "구간혼합식", CustomFieldType::Number, "", {}, "[field:partitionLength]*2"});
    add(FieldScope::Project, "partitionProject", "프로젝트구간", CustomFieldType::Text,
        "Partition([프로젝트수],0,99,5)");
    add(FieldScope::Resource, "partitionResource", "자원구간", CustomFieldType::Text, "Partition(Len([Name]),0,9,2)");
    p.assignmentFields.push_back({"receivedPartition", "구간전파", CustomFieldType::Text, "", {}});
    add(FieldScope::Assignment, "partitionAssignment", "배정구간", CustomFieldType::Text,
        "Partition([Task.입력수],0,99,3)");
    p.tasks[0].attributes.push_back({"field:partition", "원본 보존"});
    p.tasks[0].attributes.push_back({"field:partitionError", "원본 오류 보존"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:partition", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:partition", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"partition", "  0:  2", "  3:  5"},
                                                                        {"roundedPartition", " 2: 3", " 2: 3"},
                                                                        {"outsidePartition", "   : -1", "   : -1"},
                                                                        {"lastPartition", "1000:1010", "1000:1010"},
                                                                        {"partitionLength", "7", "7"},
                                                                        {"partitionNative", "14", "14"}})
        check(value(p, id) == a && value(p, id, 1) == b, "partition values including exact spaces/native chain");
    check(value(p, "partitionCost") == (p.currency == "EUR" ? "  5:  9" : " 10: 14") &&
              value(p, "partitionCost", 1) == "  0:  4",
          "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:partitionError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:partitionNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:partitionProject") == " 15: 19" &&
              resolvedAttributeValue(p, p.resources[0], "field:partitionResource") == " 2: 3",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:partitionAssignment") == "  0:  2" &&
              roll.value(p.assignments[1], "field:partitionAssignment") == "  3:  5" &&
              roll.value(p.assignments[1], "field:receivedPartition") == "  3:  5",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:partitionSummary").value == "100:   ", "summary retains trailing spaces");
}
void vectors() {
    QJsonArray rows;
    const auto add = [&](std::int64_t n, std::int64_t first, std::int64_t last, std::int64_t step) {
        rows.append(QJsonObject{{"number", double(n)},
                                {"start", double(first)},
                                {"stop", double(last)},
                                {"interval", double(step)},
                                {"value", QString::fromStdString(xmlPartition(n, first, last, step))}});
    };
    for (std::int64_t first : {0LL, 1LL, 9LL, 20LL, 100LL, 2147483646LL, 8999999999998LL})
        for (std::int64_t span : {1LL, 2LL, 9LL, 99LL, 1010LL}) {
            const auto last = std::min(9000000000000LL, first + span);
            for (std::int64_t step : {1LL, 2LL, 5LL, 20LL, 2147483647LL, 9000000000000LL})
                for (auto n : {first - 1, first, first + 1, last - 1, last, last + 1})
                    if (n <= 9000000000000LL)
                        add(n, first, last, step);
        }
    std::uint64_t state = 971533;
    for (int i = 0; i < 4096; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto first = std::int64_t(state % 10000), last = first + 1 + std::int64_t((state >> 16) % 10000);
        add(std::int64_t((state >> 32) % 30000) - 1000, first, last, 1 + std::int64_t((state >> 8) % 20000));
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
            (void)xmlPartition(1, 0, 99, 5);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"Partition(12,0,99,10)", " 10: 19"},
        {"Partition(-1,0,99,5)", "   : -1"},
        {"Partition(0,0,99,5)", "  0:  4"},
        {"Partition(99,0,99,5)", " 95: 99"},
        {"Partition(100,0,99,5)", "100:   "},
        {"Partition(1010,100,1010,20)", "1000:1010"},
        {"Partition(2.5,0,9,2.5)", " 2: 3"},
        {"Partition(3.5,0,9,2.5)", " 4: 5"},
        {"Partition(-0.5,-0.5,9.5,1.5)", " 0: 1"},
        {"Partition(-1.5,0,9,2)", "  :-1"},
        {"Partition(1,0,1,9000000000000)", " 0: 1"},
        {"Partition(2,1,20,1)", " 2: 2"},
        {"Partition(0,1,20,1)", "  : 0"},
        {"Partition(21,1,20,1)", "21:  "},
        {"Partition(True,0,9,1)", "  :-1"},
        {"Partition(False,0,9,1)", " 0: 0"},
        {"Partition(CDbl(\"12\"),0,99,10)", " 10: 19"},
        {"Partition(9000000000000,0,9000000000000,9000000000000)", "9000000000000:9000000000000"},
        {"Partition(-9000000000000,0,1,1)", "  :-1"},
        {"pArTiTiOn(12;0;99;10)", " 10: 19"},
        {"Len(Partition(12,0,99,10))", "7"},
        {"IIf(True,Partition(12,0,99,10),\"x\")", " 10: 19"},
        {"Trim(Partition(12,0,99,10))", "10: 19"},
        {"Partition(1.499999,0,9,1.499999)", " 1: 1"},
        {"Partition(1.5,0,9,1.5)", " 2: 3"},
        {"Partition(1,0,99,5)&Partition(Null,0,99,5)", "  0:  4"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "partition scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source :
         {"Partition(1,-1,99,5)", "Partition(1,5,5,1)", "Partition(1,5,4,1)", "Partition(1,0,99,0)",
          "Partition(1,0,99,0.5)", "Partition(1,0,99,-1)", "Partition(\"1\",0,99,1)", "Partition(#2024-01-01#,0,99,1)",
          "Partition(1,0,99)", "Partition(1,0,99,1,2)", "Partition1(1,0,99,1)", "IIf(True,1,Partition(1,0,99,0))",
          "Partition(Null,1/0,99,5)"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "partition type/range/arity/eager error");
    }
    for (const auto *source :
         {"Partition(Null,0,99,5)", "Partition(1,Null,99,5)", "Partition(1,0,Null,5)", "Partition(1,0,99,Null)",
          "Partition(Null,-1,0,0)", "Partition(Null,0,99,5)&Partition(Null,0,99,5)"})
        check(eval(probe, source).missing, "Null propagates before domain validation");
    rejects([&] { xmlPartition(1, -1, 99, 1); });
    rejects([&] { xmlPartition(1, 0, 99, 0); });
    rejects([&] { xmlPartition(1, 0, 9000000000001LL, 1); });
    check(!xmlPartitionFormula({"\"Partition(1,0,99,5)\"", {}, true}) &&
              !xmlPartitionFormula({"[field:Partition\\]X]", {}, true}) &&
              !xmlPartitionFormula({"Partition1(1,0,99,5)", {}, true}) &&
              xmlPartitionFormula({"pArTiTiOn (1,0,99,5)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 71 && json::decodeProject(json::encodeProject(p), 71) == p, "schema71");
    rejects([&] { json::decodeProject(json::encodeProject(p), 70); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlPartitionFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "Partition(9,0,99,5)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:partition", changed);
    check(value(*doc.project(), "partition") == "  5:  9", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "partition") == "  5:  9", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 71) == *doc.project(), "FX archives71");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 71 && json::decodeProject(json::encodeProject(frozen), 71) == frozen,
          "frozen-only71");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 70); });
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
    check(json::decodeProject(json::encodeProject(migrated), 71) == migrated, "ID archives71");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery71");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:partition", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlPartitionFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-partition";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Partition: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema71/archives/recovery/XML/XER/reports; boundary and mixed partition vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:partition"))).toString() == "  0:  2",
          "partition table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlPartitionFunction");
            check(fn && fn->count() == 4, "partition examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlPartitionFormula")->click();
            check(source->toPlainText() == "Partition(12,0,99,10)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == " 10: 19")
                        found = true;
            check(found, "partition preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:partition", nullptr);
    check(done && value(*doc.project(), "partition") == " 10: 19", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Partition(1,0,99,0)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:partition", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS Partition UI: examples/async preview/save/undo/cancel and small layout\n";
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
