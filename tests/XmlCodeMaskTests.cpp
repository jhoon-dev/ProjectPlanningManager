#include "XmlCodeFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/XmlCodeDialogs.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected code-mask rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
const CustomField &field(const PlanInput &p, FieldScope scope) {
    return customFields(p, scope).front();
}
void contract() {
    auto p = xmlCodeFixture();
    validate(p);
    check(nativeSchemaFor(p) == 41, "schema41");
    check(nextXmlLookupCode(*field(p, FieldScope::Task).xmlLookup, {}) == "AB", "fixed uppercase next");
    check(nextXmlLookupCode(*field(p, FieldScope::Task).xmlLookup, "root") == "02", "numeric next");
    check(nextXmlLookupCode(*field(p, FieldScope::Assignment).xmlLookup, {}) == "b", "lowercase next");
    rejects([&] { nextXmlLookupCode(*field(p, FieldScope::Task).xmlLookup, "branch"); });
    auto mask = *field(p, FieldScope::Resource).xmlLookup;
    mask.values[0].value = "99";
    rejects([&] { nextXmlLookupCode(mask, {}); });
    mask.masks = {{1, 1, 0, "."}};
    mask.values[0].value = "Z";
    check(nextXmlLookupCode(mask, {}) == "AA", "variable uppercase carry");
    mask.masks[0].length = 1;
    rejects([&] { nextXmlLookupCode(mask, {}); });
    mask.masks = {{1, 3, 1, "."}};
    mask.values[0].value = "한";
    validateXmlLookupMask(mask);
    mask.values[0].value = "한글";
    rejects([&] { validateXmlLookupMask(mask); });
    mask.values[0].value = "😀";
    validateXmlLookupMask(mask);
    rejects([&] { validateXmlMasks({{2, 0, 2, "."}}); });
    rejects([&] { validateXmlMasks({{1, 0, 256, "."}}); });
    rejects([&] { validateXmlMasks({{1, 4, 2, "."}}); });
    rejects([&] { validateXmlMasks({{1, 0, 0, ""}, {2, 0, 0, "."}}); });
    rejects([&] { validateXmlMasks({{1, 0, 0, ".."}}); });
    rejects([&] { (void)choiceValue(field(p, FieldScope::Task), {"branch"}); });
    auto modified = p;
    auto &l = *modified.customFields[0].xmlLookup;
    l.values[1].value = "bad";
    rejects([&] { validate(modified); });
    modified = p;
    modified.xmlWbs->values[1].code = "PR-AA";
    rejects([&] { validate(modified); });
    modified.xmlWbs->verifyUnique = false;
    validate(modified);
    auto defaultCodes = p;
    defaultCodes.xmlWbs = XmlWbsCodes{};
    defaultCodes.xmlWbs->generate = true;
    synchronizeXmlWbsCodes(defaultCodes, nullptr, true);
    check(xmlWbsCode(defaultCodes, "task", "T") == "1.1.1" && xmlWbsCode(defaultCodes, "task", "B") == "1.2",
          "default numeric WBS generation");
    PlanInput deep;
    deep.xmlWbs = XmlWbsCodes{};
    deep.xmlWbs->generate = true;
    for (int level = 1; level <= 32; ++level)
        deep.wbsNodes.push_back({"G" + std::to_string(level), "수준 " + std::to_string(level),
                                 level == 1 ? "" : "G" + std::to_string(level - 1)});
    synchronizeXmlWbsCodes(deep, nullptr, true);
    check(deep.xmlWbs->values.size() == 32, "empty level32 WBS summary boundary");
    auto xml = exportProjectXml(p);
    auto imported = importProjectXml(xml.xml, "codes.xml").project;
    validate(imported);
    check(imported.xmlWbs && imported.xmlWbs->values.size() == 4 && imported.xmlWbs->masks == p.xmlWbs->masks,
          "WBS XML import");
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        const auto &f = field(imported, scope);
        check(f.xmlLookup->masks == field(p, scope).xmlLookup->masks && f.xmlLookup->allLevelsRequired,
              "four scope masks");
    }
    check(attributeText(imported, "field:" + field(imported, FieldScope::Task).id,
                        imported.tasks[0].attributes[0].value) == "AA.01/공정",
          "masked code display");
    auto local = p;
    local.projectFields.clear();
    local.projectAttributes.clear();
    local.resourceFields.clear();
    local.assignmentFields.clear();
    for (auto &r : local.resources)
        r.attributes.clear();
    for (auto &a : local.assignments)
        a.attributes.clear();
    local.customFields[0].id = "xml-udf-TASK-188744096";
    local.customFields[0].xmlIdentity.reset();
    local.tasks[0].attributes[0].key = "field:xml-udf-TASK-188744096";
    auto localXml = exportProjectXml(local).xml;
    const auto begin = localXml.indexOf("<ExtendedAttributes>");
    const auto end = localXml.indexOf("</ExtendedAttributes>", begin);
    check(begin >= 0 && end > begin, "local definitions fixture");
    localXml.remove(begin, end - begin + QByteArray("</ExtendedAttributes>").size());
    const auto localRead = importProjectXml(localXml, "local-mask.xml").project;
    check(localRead.customFields.size() == 1 && localRead.customFields[0].xmlLookup->masks.size() == 3,
          "root-only local OutlineCode masks");
    check(exportProjectXml(localRead).xml.contains("<FieldID>188744096</FieldID>"), "local masked field ID retained");
    auto reexport = exportProjectXml(imported);
    auto again = importProjectXml(reexport.xml, "again.xml").project;
    check(again.xmlWbs->masks == imported.xmlWbs->masks && again.xmlWbs->values == imported.xmlWbs->values,
          "WBS reexport stable");
    auto bad = xml.xml;
    bad.replace("<Length>2</Length>", "<Length>256</Length>");
    rejects([&] { importProjectXml(bad, "bad.xml"); });
    bad = xml.xml;
    bad.replace("<WBSLevel>a</WBSLevel>", "<WBSLevel>z</WBSLevel>");
    rejects([&] { importProjectXml(bad, "conflict.xml"); });
    bad = xml.xml;
    bad.replace("<Level>1</Level>", "<Level>2</Level>");
    rejects([&] { importProjectXml(bad, "duplicate.xml"); });
    const auto body = json::encodeProject(p);
    check(json::decodeProject(body, 41) == p, "native mask roundtrip");
    rejects([&] { json::decodeProject(body, 40); });
    modified = p;
    modified.xmlWbs.reset();
    rejects([&] { json::decodeProject(json::encodeProject(modified), 40); });
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(modified, scope)) {
            f.xmlLookup->masks.clear();
            f.xmlLookup->allLevelsRequired = false;
        }
    check(nativeSchemaFor(modified) < 41, "old minimum writer unchanged");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("codes.ppm").toStdString();
    store.save(path, p, {});
    check(store.load(path).project == p, "schema41 save load");
    Document d(store);
    d.open(path);
    auto before = *d.project();
    rejects([&] { d.setXmlLookupMasks(FieldScope::Task, "field:code0", {{1, 0, 2, "."}}, true); });
    check(*d.project() == before, "atomic mask rejection");
    auto branch = d.createXmlLookupCode(FieldScope::Task, "field:code0", "root", {}, "다음 공정");
    check(d.project()->customFields[0].xmlLookup->values.back().value == "02" &&
              d.project()->projectFields[0].xmlLookup->values.back().optionId == branch,
          "shared code creation");
    auto leaf = d.createXmlLookupCode(FieldScope::Project, "field:code1", branch, "검토", "직접 문자열");
    d.setTaskAttribute({"B"}, "field:code0", leaf);
    check(attributeText(*d.project(), "field:code0", leaf) == "AA.02/검토", "new leaf selection");
    d.undo();
    d.undo();
    d.undo();
    check(static_cast<const PlanInput &>(*d.project()) == static_cast<const PlanInput &>(before),
          "generated dictionary undo");
    d.redo();
    d.redo();
    d.redo();
    check(attributeValue(d.project()->tasks[1], "field:code0") == leaf, "redo stable generated IDs");
    before = *d.project();
    rejects([&] { d.createXmlLookupCode(FieldScope::Task, "field:code0", "root", "02", {}); });
    check(*d.project() == before, "duplicate code atomic");
    auto added = d.insertTasks({"새 작업"}, {}, "H");
    check(xmlWbsCode(*d.project(), "task", added[0]) == "PR-AA.001-b", "new task auto code");
    d.undo();
    check(!xmlWbsCode(*d.project(), "task", added[0]), "task insert code undo");
    d.redo();
    check(xmlWbsCode(*d.project(), "task", added[0]) == "PR-AA.001-b", "task insert redo");
    d.removeTasks(added);
    check(!xmlWbsCode(*d.project(), "task", added[0]), "removed code cleanup");
    d.undo();
    auto control = std::make_shared<CalculationControl>();
    auto request = d.makeEditRequest(InsertTasksEdit{{"worker task"}, {}, "H"});
    auto prepared = Document::prepareEdit(std::move(request), control);
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "insert worker with codes");
    check(d.project()->xmlWbs->values.back().code == "PR-AA.001-c", "worker code sequence");
    d.undo();
    prepared = Document::prepareEdit(d.makeEditRequest(InsertTasksEdit{{"cancelled"}, {}, "H"}), control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "code worker cancel");
    prepared = Document::prepareEdit(d.makeEditRequest(InsertTasksEdit{{"stale"}, {}, "H"}),
                                     std::make_shared<CalculationControl>());
    d.renameTask(0, "마스크 검증");
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "code worker stale");
    auto manual = *d.project()->xmlWbs;
    manual.generate = false;
    d.setXmlWbsCodes(manual);
    added = d.insertTasks({"자동 생성 꺼짐"}, {}, "H");
    check(!xmlWbsCode(*d.project(), "task", added[0]), "generate false missing preserved");
    auto fill = *d.project()->xmlWbs;
    d.setXmlWbsCodes(fill, true);
    check(xmlWbsCode(*d.project(), "task", added[0]).has_value(), "explicit fill missing");
    d.undo();
    check(!xmlWbsCode(*d.project(), "task", added[0]), "fill missing undo");
    for (std::size_t i = 0; i < d.project()->tasks.size(); ++i)
        if (!d.project()->tasks[i].durationMinutes)
            d.setDuration(i, 60);
    d.captureBaseline("마스크 동결", "PM", "검증", "2026-11-11T01:00:00Z");
    d.saveScenario("코드 사본", "task-order", 260, "2026-11-11T01:01:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto frozen = d.project()->baselines[0].input;
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-11", "마스크·코드 보존"};
    fx.reporter = "PM";
    fx.reason = "코드 검증";
    fx.recordedAt = "2026-11-11T01:02:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(d.project()->baselines[0].input.xmlWbs == frozen.xmlWbs &&
              d.project()->baselines[0].input.customFields == frozen.customFields,
          "FX preserves codes");
    check(json::decodeProject(json::encodeProject(*d.project()), 41) == *d.project(),
          "archive and transition roundtrip");
    auto archived = *d.project();
    archived.xmlWbs.reset();
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(archived, scope)) {
            f.xmlLookup->masks.clear();
            f.xmlLookup->allLevelsRequired = false;
        }
    check(nativeSchemaFor(archived) == 41, "snapshot only schema41");
    auto observed = p;
    observed.costEntries = {{"fact",
                             "T",
                             "2026-11-11 09:00",
                             "2026-11-11T01:00:00Z",
                             "PM",
                             "코드 소유 ID 검증",
                             {},
                             p.documentId,
                             p.currency,
                             1,
                             100,
                             {}}};
    validate(observed);
    auto migrated = migrateLedgerIdentities(observed, {{"task", "T", "NEW"}});
    check(xmlWbsCode(migrated, "task", "NEW") == "PR-AA.001-a", "ledger ID remaps code owner");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-11T01:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema41 recovery");
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.setXmlWbsCodes(*p.xmlWbs, true); });
    rejects([&] { readonly.createXmlLookupCode(FieldScope::Task, "field:code0", "root", {}, {}); });
    const auto output = QDir::currentPath() + "/xml-code-masks-qa";
    QDir().mkpath(output);
    write(output + "/export.xml", xml.xml);
    write(output + "/reexport.xml", reexport.xml);
    write(output + "/native.json", QJsonDocument(body).toJson());
    write(output + "/mapping.json", QJsonDocument(xml.report).toJson());
    const auto generated = exportProjectXml(*d.project());
    write(output + "/generated.xml", generated.xml);
    write(output + "/generated.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    auto xer = exportPrimaveraXer(p);
    write(output + "/export.xer", xer.xer);
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output + '/' + name);
    store.save((output + "/source.ppm").toStdString(), *d.project(), {});
    ReportOptions options;
    options.reportId = "xml-code-masks";
    options.generatedAt = "2026-11-11T01:04:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, output + "/fields.xlsx");
    check(exportPdf(report, output + "/fields.pdf") > 0, "mask PDF");
    std::cout << "PASS XML code masks: four scopes/shared GUID, types/lengths/Unicode/separators, automatic "
                 "sequences/exhaustion, WBS flags/stable codes, schema41/archives/recovery/undo, worker/cancel/stale, "
                 "reports\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = xmlCodeFixture();
    auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto output = QDir::currentPath() + "/xml-code-masks-qa";
    QDir().mkpath(output);
    auto run = [&](const std::function<void(QDialog *)> &action) {
        QTimer::singleShot(0, [&, action] {
            try {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(dialog, "modal");
                action(dialog);
            } catch (const std::exception &e) {
                std::cerr << e.what();
                std::exit(2);
            }
        });
    };
    run([&](QDialog *dialog) {
        check(dialog->findChild<QTableWidget *>("xmlCodeMasks")->rowCount() == 3, "mask editor rows");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(output + "/mask-small.png"), "mask screenshot");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlLookupMask(d, FieldScope::Task, "field:code0", nullptr);
    run([&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>("xmlCodeParent");
        parent->setCurrentIndex(parent->findData("root"));
        check(dialog->findChild<QLineEdit *>("xmlCodeSegment")->text() == "02", "automatic preview");
        check(dialog->findChild<QLabel *>("xmlCodePreview")->text().endsWith("AA.02"), "full code preview");
        check(dialog->grab().save(output + "/generate.png"), "generate screenshot");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    createXmlLookupCode(d, FieldScope::Task, "field:code0", nullptr);
    check(d.project()->customFields[0].xmlLookup->values.size() == 4, "UI created shared item");
    auto before = *d.project();
    run([&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>("xmlCodeParent");
        parent->setCurrentIndex(parent->findData("root"));
        d.renameTask(0, "문서 변경");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!dialog->findChild<QLabel *>("xmlCodeError")->text().isEmpty(), "stale UI blocked");
        dialog->reject();
    });
    createXmlLookupCode(d, FieldScope::Task, "field:code0", nullptr);
    check(d.project()->customFields == before.customFields, "stale does not add code");
    d.insertTasks({"미설정 작업"}, {}, "H"); // Existing GenerateCodes=1 produces the next code.
    auto config = *d.project()->xmlWbs;
    config.values.pop_back();
    config.generate = false;
    d.setXmlWbsCodes(config);
    run([&](QDialog *dialog) {
        auto *prefix = dialog->findChild<QLineEdit *>("xmlWbsPrefix");
        auto unicode = QString::fromUtf8("😀").repeated(30);
        prefix->setText(unicode);
        check(prefix->text() == unicode, "Unicode prefix not truncated by UTF-16 length");
        prefix->setText("PR-");
        dialog->findChild<QPushButton *>("previewXmlWbsCodes")->click();
        auto *table = dialog->findChild<QTableWidget *>("xmlWbsValues");
        check(!table->item(table->rowCount() - 1, 1)->text().isEmpty(), "WBS preview fills missing");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(output + "/wbs-small.png"), "WBS small screenshot");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlWbsCodes(d, nullptr);
    check(d.project()->xmlWbs->values.size() == 5, "WBS UI save");
    before = *d.project();
    run([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>("xmlWbsPrefix")->setText("INVALID-");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!dialog->findChild<QLabel *>("xmlCodeError")->text().isEmpty(), "invalid WBS UI blocked");
        dialog->reject();
    });
    editXmlWbsCodes(d, nullptr);
    check(*d.project() == before, "invalid UI atomic");
    std::cout << "PASS XML code-mask UI: mask rows, automatic preview/create/shared dictionary, stale guard, WBS "
                 "preview/save, 800x600 and invalid atomic rejection\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << ' ' << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
