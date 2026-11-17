#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlProjectConversion.h"
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
#include <chrono>
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
    return QDir::currentPath() + "/xml-project-conversion-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-project-conversion";
    p.name = "XML Project 변환";
    p.customFields.push_back({"dateInput", "날짜문자", CustomFieldType::Text, "", {}});
    p.customFields.push_back({"periodInput", "기간문자", CustomFieldType::Text, "", {}});
    for (int i = 0; i < 2; ++i) {
        p.tasks[i].attributes.push_back(
            {"field:dateInput", i ? "2024-02-29T23:05:06.123456" : "2026-11-14T09:30:12.123456"});
        p.tasks[i].attributes.push_back({"field:periodInput", i ? "2ed" : "1.5h?"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fs = customFields(p, scope);
        fs.push_back({id, name, type, "", {}});
        fs.back().xmlFormula = bindXmlFormula(p, scope, source);
        fs.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "dateValue", "Project날짜값", CustomFieldType::Date, "ProjDateValue([날짜문자])");
    add(FieldScope::Task, "shortDate", "Project날짜표시", CustomFieldType::Text, "ProjDateConv([날짜문자],20)");
    add(FieldScope::Task, "defaultDate", "Project기본표시", CustomFieldType::Text, "ProjDateConv([날짜문자])");
    add(FieldScope::Task, "hours", "Project시간표시", CustomFieldType::Text, "ProjDurConv([기간문자],5)");
    add(FieldScope::Task, "elapsed", "Project경과표시", CustomFieldType::Text, "ProjDurConv([기간문자],40)");
    add(FieldScope::Task, "months", "Project월표시", CustomFieldType::Text, "ProjDurConv([기간문자],11)");
    add(FieldScope::Task, "minutes", "Project분추출", CustomFieldType::Number, "Val(ProjDurConv([기간문자],3))");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedProjectConversion", true};
    add(FieldScope::Task, "constant", "Project숫자표시", CustomFieldType::Text, "ProjDurConv([입력수],3)");
    add(FieldScope::Task, "projectConversionError", "Project변환오류", CustomFieldType::Text,
        "ProjDurConv(\"9000000000000w\")");
    add(FieldScope::Task, "projectConversionNull", "Project미설정", CustomFieldType::Date, "ProjDateValue(\"NA\")");
    add(FieldScope::Task, "projectConversionSummary", "Project변환요약", CustomFieldType::Text,
        "ProjDateConv(#2026-11-14#,20)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"projectConversionNative", "Project변환혼합", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:minutes]*2";
    add(FieldScope::Project, "projectConversionProject", "프로젝트날짜표시", CustomFieldType::Text,
        "ProjDateConv(#2026-11-14#,1)");
    add(FieldScope::Resource, "projectConversionResource", "자원기간표시", CustomFieldType::Text,
        "ProjDurConv(\"1w\",7)");
    p.assignmentFields.push_back({"receivedProjectConversion", "Project변환전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "projectConversionAssignment", "배정기간표시", CustomFieldType::Text,
        "ProjDurConv([Task.기간문자],6)");
    p.tasks[0].attributes.push_back({"field:shortDate", "stale"});
    p.tasks[0].attributes.push_back({"field:projectConversionError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:shortDate", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:shortDate", &p.tasks[0]);
}
void verify(const Project &p) {
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> expected = {
        {"dateValue", {"2026-11-14T09:30:12.123456", "2024-02-29T23:05:06.123456"}},
        {"shortDate", {"11/14/2026", "2/29/2024"}},
        {"defaultDate", {"11/14/26 9:30 AM", "2/29/24 11:05 PM"}},
        {"hours", {"1.5 h", "48 h"}},
        {"elapsed", {"0.0625 ed?", "2 ed?"}},
        {"months", {"0.009375 mo", "0.3 mo"}},
        {"minutes", {"90", "2880"}},
        {"constant", {"2.5 m", "3.5 m"}},
        {"projectConversionNative", {"180", "5760"}}};
    for (const auto &[id, v] : expected) {
        if (value(p, id) != v.first || value(p, id, 1) != v.second)
            std::cerr << id << " = " << value(p, id).value_or("Null") << " / " << value(p, id, 1).value_or("Null")
                      << "\n";
        check(value(p, id) == v.first && value(p, id, 1) == v.second, "project conversion values");
    }
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:projectConversionError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:projectConversionNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error/NA never snapshot");
    check(resolvedProjectAttributeValue(p, "field:projectConversionProject") == "11/14/26" &&
              resolvedAttributeValue(p, p.resources[0], "field:projectConversionResource") == "5 d",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:projectConversionAssignment") == "1.5 eh" &&
              roll.value(p.assignments[1], "field:projectConversionAssignment") == "48 eh" &&
              roll.value(p.assignments[1], "field:receivedProjectConversion") == "2880",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:projectConversionSummary").value == "11/14/2026", "summary");
}
void vectors() {
    QJsonArray dates, durations;
    std::uint64_t state = 941387;
    for (int i = 0; i < 128; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto day = std::chrono::year_month_day(std::chrono::sys_days(std::chrono::year(1970) / 1 / 1) +
                                                     std::chrono::days(state % 47847));
        const auto pad = [](int n, int width) {
            auto s = std::to_string(n);
            return std::string(width - s.size(), '0') + s;
        };
        const auto iso = pad(int(day.year()), 4) + "-" + pad(unsigned(day.month()), 2) + "-" +
                         pad(unsigned(day.day()), 2) + "T" + pad(state % 24, 2) + ":" + pad(state / 24 % 60, 2) + ":" +
                         pad(state / 1464 % 60, 2) + ".123456";
        check(xmlProjectDateValue(" " + iso + " ") == iso, "ISO normalization/microseconds");
        for (int format = 0; format <= 21; ++format) {
            const auto id = format == 21 ? 255 : format;
            dates.append(QJsonObject{{"input", QString::fromStdString(iso)},
                                     {"format", id},
                                     {"output", QString::fromStdString(xmlProjectDateFormat(iso, id))}});
        }
    }
    for (int i = 0; i < 512; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        auto n = std::int64_t(state % 900000000000000001ULL);
        if (state & (1ULL << 63))
            n = -n;
        for (int format = 3; format <= 12; ++format)
            for (int extra : {0, 32}) {
                const auto text = xmlProjectDurationFormat(n, format + extra);
                durations.append(QJsonObject{{"input", QString::number(n)},
                                             {"format", format + extra},
                                             {"output", QString::fromStdString(text)},
                                             {"parsed", QString::number(xmlProjectDurationValue(text))}});
            }
    }
    save(output() + "/dates.json", QJsonDocument(dates).toJson());
    save(output() + "/durations.json", QJsonDocument(durations).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlProjectDurationValue("1h");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"ProjDateValue(\"2024-02-29\")", "2024-02-29T00:00:00"},
        {"ProjDateValue(#2024-02-29T09:30:12.123456#)", "2024-02-29T09:30:12.123456"},
        {"ProjDateConv(#2026-11-14T09:30:12.123456#)", "11/14/26 9:30 AM"},
        {"ProjDateConv(#2026-11-14#,20)", "11/14/2026"},
        {"ProjDateConv(#2026-11-14#,3)", "November 14, 2026"},
        {"ProjDateConv(#2026-11-14#,5)", "Nov 14, '26"},
        {"ProjDateConv(#2026-11-14#,17)", "Sat 14"},
        {"ProjDurConv(90,5)", "1.5 h"},
        {"ProjDurConv(\"2w\",7)", "10 d"},
        {"ProjDurConv(\"2ed?\",40)", "2 ed?"},
        {"ProjDurConv(\"1mo\",7)", "20 d"},
        {"ProjDurConv(\"1emo\",8)", "30 ed"},
        {"ProjDurConv(-1,7)", "-0.002083 d"},
        {"ProjDurConv(0.000001)", "0.000001 m"},
        {"ProjDurConv(ProjDurValue(\"90 em?\"),6)", "1.5 eh"},
        {"ProjDurConv(9000000000000,3)", "9000000000000 m"},
        {"IsNull(ProjDateValue(\" na \"))", "True"},
        {"IsNull(ProjDateConv(\"NA\",20))", "True"},
        {"Year(ProjDateValue(\"2024-02-29\"))", "2024"},
        {"ProjDateAdd(#2026-11-13T16:00:00#,ProjDurConv(\"1ed\",8))", "2026-11-14T16:00:00"}};
    QJsonArray records;
    for (const auto &[source, want] : cases) {
        const auto r = eval(probe, source);
        if (r.value != want)
            std::cerr << source << " wanted " << want << " got " << r.value.value_or(r.issue) << "\n";
        check(r.value == want, "scalar project conversion");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source :
         {"ProjDateValue(Null)", "ProjDateConv(Null)", "ProjDurConv(Null)", "ProjDateValue(\"NA\")"}) {
        auto r = eval(probe, source);
        check(!r.value && r.missing, "Null/NA");
    }
    for (const auto *source :
         {"ProjDateValue(\"Today\")", "ProjDateValue(\"11/14/2026\")", "ProjDateValue(12)", "ProjDateConv(Null,21)",
          "ProjDurConv(Null,0)", "ProjDurConv(12,Null)", "ProjDateConv(Null,Null)", "ProjDurConv(True)",
          "ProjDurConv(\"1e3h\")", "ProjDurConv(\"1.0000001h\")", "ProjDurConv(\"9000000000000w\")",
          "ProjDurConv(12,5.5)", "IIf(True,1,ProjDurConv(12,0))"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "invalid format/input/overflow/eager");
    }
    for (const auto *source : {"ProjDateValue()", "ProjDateValue(1,2)", "ProjDateConv(1,2,3)", "ProjDurConv()"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.issue.empty(), "arity");
    }
    rejects([&] { xmlProjectDurationValue("1.5.2h"); });
    check(!xmlProjectConversionFormula({"CStr(\"ProjDurConv(1)\")", {}, true}) &&
              !xmlProjectConversionFormula({"[ProjDateConv(1)]", {}, true}),
          "scanner ignores literal/ref");
    check(nativeSchemaFor(p) == 61 && json::decodeProject(json::encodeProject(p), 61) == p, "schema61");
    rejects([&] { json::decodeProject(json::encodeProject(p), 60); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlProjectConversionFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "ProjDurConv(90,5)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:shortDate", changed);
    check(value(*doc.project(), "shortDate") == "1.5 h", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "shortDate") == "1.5 h", "redo");
    doc.undo();
    const auto *renameInput = customField(*doc.project(), "field:dateInput");
    check(renameInput != nullptr, "rename fixture input exists");
    auto renamed = *renameInput;
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 61) == *doc.project(), "FX archives61");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 61 && json::decodeProject(json::encodeProject(frozen), 61) == frozen,
          "frozen-only61");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 60); });
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
    check(json::decodeProject(json::encodeProject(migrated), 61) == migrated, "ID archives61");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery61");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:shortDate", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "numbers.xml");
    check(hasXmlProjectConversionFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-project-conversion";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS ProjDateValue/ProjDateConv/ProjDurConv: fixed "
                 "profiles/ISO/NA/units/scopes/archives/61/XML/XER/reports\n";
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
    check(model.data(model.index(0, model.columnForKey("field:shortDate"))).toString() == "11/14/2026",
          "conversion table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlProjectConversionFunction");
            check(fn && fn->count() == 4, "conversion examples");
            source->clear();
            fn->setCurrentIndex(2);
            d->findChild<QPushButton *>("insertXmlProjectConversionFormula")->click();
            check(source->toPlainText() == "ProjDurConv(90, 5)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "1.5 h")
                        found = true;
            check(found, "conversion preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:shortDate", nullptr);
    check(done && value(*doc.project(), "shortDate") == "1.5 h", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("ProjDurConv(12,0)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:shortDate", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS conversion UI: examples/async preview/save/undo/cancel and small layout\n";
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
