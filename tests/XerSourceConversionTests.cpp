#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/XerSourceRelink.h"
#include "formatting/DiagnosticText.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/XerSourceImport.h"
#include "ui/XerSourceRelinkDialog.h"
#include "ui/XerFinancialDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace ppm;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected conversion rejection");
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write evidence");
}
QByteArray envelope(const QJsonObject &body, int schema) {
    return QJsonDocument(
               QJsonObject{{"format", "PPM"},
                           {"schemaVersion", schema},
                           {"payload", body},
                           {"sha256", QString::fromLatin1(
                                          QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                                   QCryptographicHash::Sha256)
                                              .toHex())}})
        .toJson();
}
XerSourceConversion conversion(const std::string &target, std::int64_t n = 1, std::int64_t d = 1) {
    return {target, "2026-11-08", "검토 환율 및 단위 명세", n, d, {}};
}
XerRelinkAudit audit(const Project &p, const XerFinancialSnapshot &s) {
    XerRelinkAudit a;
    a.incoming = s;
    a.projectName = "통화·종류가 바뀐 후속 원본";
    a.sourceStatusAt = s.statusAt;
    a.confirmed = true;
    a.previousLinks = suggestXerLinks(*activeXerFinancials(p), p);
    // Explicit identity confirmation; type mismatches must never be auto-guessed.
    for (const auto &t : s.tasks)
        a.incomingLinks.push_back({"task", t.id, t.id});
    for (const auto &r : s.resources)
        a.incomingLinks.push_back({"resource", r.id, r.id});
    return a;
}
XerRelinkSubmission submission(XerRelinkAudit a) {
    return {std::move(a), {{}, "검토자", "원본 환산 및 원장 대사", "2026-11-08T05:00:00Z"}};
}
void immutable(const Project &a, const Project &b) {
    auto x = a, y = b;
    x.revision = y.revision;
    x.xerFinancialRevisions = y.xerFinancialRevisions;
    check(x == y, "conversion changed native/source facts or plan");
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "conversion UI timeout");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { std::_Exit(3); });
    guard.start(150000);
    try {
        QDir().mkpath("xer-conversion-qa");
        const auto bytes = readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE));
        const auto p = importPrimaveraXer(bytes, "original.xer").project;
        const auto original = *p.xerFinancials;
        // Independent Python oracle consumes every case, including overflow rejections.
        QJsonArray cases;
        for (const std::string from : {"KRW", "JPY", "USD", "EUR"})
            for (const std::string to : {"KRW", "JPY", "USD", "EUR"})
                for (const auto &[n, d] : {std::pair{1LL, 1LL},
                                           {1LL, 2LL},
                                           {2LL, 3LL},
                                           {1350LL, 1LL},
                                           {1LL, 1350LL},
                                           {999999937LL, 999999929LL}}) {
                    if (from == to && n != d)
                        continue;
                    for (const auto amount :
                         {0LL, 1LL, 2LL, 3LL, 99LL, 100LL, 101LL, 900719925LL, 8999999999999LL, 9000000000000LL}) {
                        auto s = original;
                        s.currency = from;
                        s.assignments.clear();
                        s.periods.clear();
                        s.resources.clear();
                        s.tasks = {{"T", "精密 / 정밀", amount}};
                        QJsonObject row{{"from", QString::fromStdString(from)},
                                        {"to", QString::fromStdString(to)},
                                        {"n", n},
                                        {"d", d},
                                        {"amount", amount}};
                        try {
                            row["result"] = qint64(*convertXerSource(s, conversion(to, n, d)).tasks[0].actualMinor);
                        } catch (const AppError &) {
                            row["result"] = QJsonValue::Null;
                        }
                        cases.append(row);
                    }
                }
        write("xer-conversion-qa/numeric-cases.json", QJsonDocument(cases).toJson());
        auto forward = conversion("EUR", 2, 1);
        forward.resources = {{"labor", "equipment", 2, 1}, {"equipment", "labor", 2, 1}};
        auto foreign = convertXerSource(original, forward);
        foreign.sourceName = "foreign.xer";
        auto inverse = conversion("USD", 1, 2);
        inverse.resources = {{"labor", "equipment", 1, 2}, {"equipment", "labor", 1, 2}};
        auto roundtrip = convertXerSource(foreign, inverse);
        roundtrip.sourceName = original.sourceName;
        check(roundtrip == original && original == *p.xerFinancials, "inverse/source preservation");
        auto a = audit(p, foreign);
        rejects([&] { projectForXerRelink(p, a); });
        a.incomingConversion = inverse;
        auto candidate = projectForXerRelink(p, a);
        check(mergeXerFinancials(candidate).ready(), "converted merge not ready");
        QTemporaryDir temp;
        JsonProjectStore store;
        const auto path = (temp.path() + "/convert.ppm").toStdString();
        store.save(path, p, {});
        Document doc(store);
        doc.open(path);
        auto conflict = a;
        conflict.incomingConversion->numerator = 2;
        rejects([&] { doc.recordXerRelink(submission(conflict), xerFinancialProjectionHash); });
        check(*doc.project() == p, "unresolved currency conversion changed native facts");
        auto prepared =
            Document::prepareEdit(doc.makeEditRequest(XerRelinkEdit{submission(a), xerFinancialProjectionHash}),
                                  std::make_shared<CalculationControl>());
        if (prepared->failure())
            std::cerr << prepared->error() << '\n';
        check(prepared->state() == EditState::Ready && *doc.project() == p, "conversion preparation mutated source");
        check(doc.applyPreparedEdit(*prepared) == EditState::Applied, "converted commit");
        immutable(p, *doc.project());
        check(nativeSchemaFor(*doc.project()) == 31 && xerFinancialsCurrent(*doc.project()) && !doc.canUndo(),
              "schema31/live audit");
        check(doc.project()->xerFinancialRevisions.back().relink->incoming == foreign, "original incoming lost");
        doc.save();
        check(store.load(path).project == *doc.project(), "schema31 roundtrip/replay");
        const auto body = json::encodeProject(*doc.project());
        rejects([&] { decodeProjectFile(envelope(body, 30)); });
        check(decodeProjectFile(envelope(body, 52)) == json::decodeProject(body, 52), "supported schema52");
        rejects([&] { decodeProjectFile(envelope(body, maximumNativeSchema + 1)); });
        for (int i = 0; i < 12; ++i) {
            auto bad = body;
            auto history = bad["xerFinancialRevisions"].toArray();
            auto r = history[0].toObject();
            auto relink = r["relink"].toObject();
            auto c = relink["incomingConversion"].toObject();
            if (i == 0)
                c["numerator"] = 0;
            if (i == 1)
                c["denominator"] = QJsonValue::Null;
            if (i == 2)
                c["denominator"] = 1.5;
            if (i == 3)
                c["numerator"] = 1000000001;
            if (i == 4)
                c["targetCurrency"] = "JPY";
            if (i == 5)
                c["effectiveDate"] = "2026-02-30";
            if (i == 6)
                c["reference"] = " ";
            if (i == 7)
                c["policy"] = "unknown";
            if (i == 8)
                c["unexpected"] = true;
            if (i == 9)
                c.remove("reference");
            if (i == 10) {
                auto rules = c["resources"].toArray();
                rules.append(rules[0]);
                c["resources"] = rules;
            }
            if (i == 11) {
                auto rules = c["resources"].toArray();
                auto rule = rules[1].toObject();
                rule["workNumerator"] = 2;
                rules[1] = rule;
                c["resources"] = rules;
            }
            relink["incomingConversion"] = c;
            r["relink"] = relink;
            history[0] = r;
            bad["xerFinancialRevisions"] = history;
            rejects([&] { json::decodeProject(bad, 31); });
            write(QString("xer-conversion-qa/malformed-%1.ppm").arg(i), envelope(bad, 31));
        }
        auto badRatio = inverse;
        badRatio.resources[1].workDenominator = 7;
        rejects([&] { convertXerSource(foreign, badRatio); });
        auto tooLarge = conversion("JPY", 1000000000, 1);
        rejects([&] { convertXerSource(original, tooLarge); });
        auto unknown = original;
        unknown.tasks[0].actualMinor.reset();
        unknown.assignments.clear();
        unknown.periods.clear();
        check(!convertXerSource(unknown, conversion("KRW")).tasks[0].actualMinor, "unknown became zero");
        auto summaryOnly = original;
        summaryOnly.resources.clear();
        summaryOnly.assignments.clear();
        std::erase_if(summaryOnly.periods, [](const auto &e) { return !e.resourceId.empty(); });
        auto classify = conversion("USD");
        classify.resources = {{"labor", "equipment", 1, 1}};
        const auto summaryResult = convertXerSource(summaryOnly, classify);
        check(!summaryResult.periods[0].laborMinor &&
                  summaryResult.periods[0].equipmentMinor == summaryOnly.periods[0].laborMinor,
              "summary-only kind transition");
        // Half-up of detail can exceed the rounded total: reject rather than alter an observation.
        auto rounding = original;
        rounding.tasks = {{"T", "rounding", 1}};
        rounding.assignments = {{"T", original.resources[0].id, {}, {}, 1, {}},
                                {"T", original.resources[1].id, {}, {}, 0, {}}};
        rounding.periods.clear();
        auto one = rounding.assignments[0];
        one.resourceId = original.resources[1].id;
        rounding.assignments[1] = one;
        rounding.tasks[0].actualMinor = 2;
        XerFinancialPeriod roundingPeriod;
        roundingPeriod.taskId = "T";
        roundingPeriod.resourceId = original.resources[0].id;
        roundingPeriod.start = "2026-10-26 00:00";
        roundingPeriod.finish = "2026-10-26 23:59";
        roundingPeriod.actualMinor = 1;
        rounding.periods = {roundingPeriod};
        roundingPeriod.resourceId = original.resources[1].id;
        rounding.periods.push_back(roundingPeriod);
        rejects([&] { convertXerSource(rounding, conversion("EUR", 1, 2)); });
        auto changed = p;
        for (auto &r : changed.resources)
            if (r.type == "equipment")
                r.type = "labor";
        validate(changed);
        auto typeAudit = audit(changed, original);
        typeAudit.incomingConversion = conversion("USD");
        typeAudit.incomingConversion->resources = {{"equipment", "labor", 1, 1}};
        typeAudit.previousConversion = typeAudit.incomingConversion;
        for (auto &link : typeAudit.previousLinks)
            link.targetId = link.sourceId;
        check(mergeXerFinancials(projectForXerRelink(changed, typeAudit)).ready(), "previous source kind conversion");
        const auto typePath = (temp.path() + "/types.ppm").toStdString();
        store.save(typePath, changed, {});
        Document types(store);
        types.open(typePath);
        types.recordXerRelink(submission(typeAudit), xerFinancialProjectionHash);
        immutable(changed, *types.project());
        types.save();
        check(store.load(typePath).project == *types.project(), "previous conversion replay");
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        doc.renameTask(0, "복구 검증 편집");
        auto session = recovery.newSession();
        session->checkpoint(doc.recoverySnapshot("2026-11-08T06:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema31 recovery");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        auto recovered = *restored.project();
        check(recovered.documentId != doc.project()->documentId && recovered.derivedFrom == doc.project()->documentId,
              "recovery copy lineage");
        recovered.documentId = doc.project()->documentId;
        recovered.derivedFrom = doc.project()->derivedFrom;
        check(recovered == *doc.project(), "recovery conversion loss");
        doc.undo();
        doc.saveAs((temp.path() + "/copy.ppm").toStdString());
        check(xerFinancialsCurrent(*doc.project()), "copy conversion binding");
        auto pending =
            Document::prepareEdit(doc.makeEditRequest(XerRelinkEdit{submission(a), xerFinancialProjectionHash}),
                                  std::make_shared<CalculationControl>());
        doc.renameTask(0, "changed");
        check(doc.applyPreparedEdit(*pending) == EditState::Stale, "stale conversion committed");
        doc.undo();
        auto control = std::make_shared<CalculationControl>();
        control->cancel();
        auto cancelled = Document::prepareEdit(
            doc.makeEditRequest(XerRelinkEdit{submission(a), xerFinancialProjectionHash}), control);
        check(cancelled->state() == EditState::Cancelled, "conversion cancellation");
        Document ro(store);
        ro.open(doc.path(), true);
        rejects([&] { ro.recordXerRelink(submission(a), xerFinancialProjectionHash); });
        doc.recordXerFinancialMerge({{}, "검토자", "환산 후 추가 병합", "2026-11-08T06:00:00Z"},
                                    xerFinancialProjectionHash);
        check(xerFinancialsCurrent(*doc.project()), "subsequent merge conversion loss");
        auto exported = exportPrimaveraXer(*doc.project());
        check(exported.report["actualCosts"].toObject()["sourceConversionCount"].toInt() == 1,
              "exchange conversion receipt absent");
        auto back = importPrimaveraXer(exported.xer, "converted-export.xer").project;
        check(back.currency == "USD" && back.xerFinancials->assignments.size() == original.assignments.size() &&
                  back.xerFinancials->assignments[0].regularWork == original.assignments[0].regularWork &&
                  back.xerFinancials->assignments[0].regularMinor == original.assignments[0].regularMinor,
              "converted export values");
        write("xer-conversion-qa/source.json", QJsonDocument(encodeXerFinancials(original)).toJson());
        write("xer-conversion-qa/foreign.json", QJsonDocument(encodeXerFinancials(foreign)).toJson());
        write("xer-conversion-qa/before.json", QJsonDocument(json::encodeProject(p)).toJson());
        write("xer-conversion-qa/converted.ppm", envelope(body, 31));
        write("xer-conversion-qa/export.xer", exported.xer);
        bool inspected = false, inspectedPreview = false;
        QTimer historyTimer;
        historyTimer.setInterval(20);
        QObject::connect(&historyTimer, &QTimer::timeout, [&] {
            auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!child)
                return;
            auto *table = child->findChild<QTableWidget *>("xerSourceConversionHistory");
            if (table && !inspected) {
                inspected = true;
                check(table->rowCount() == 3, "saved conversion history metadata absent");
                child->findChild<QTabWidget *>("xerFinancialTabs")->setCurrentWidget(table->parentWidget());
                child->grab().save("xer-conversion-qa/history.png");
                QTimer::singleShot(20, [&] {
                    auto *preview = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(preview && preview->objectName() == "xerConversionPreview", "saved original preview absent");
                    inspectedPreview = true;
                    preview->reject();
                });
                child->findChild<QPushButton *>("xerInspectSourceConversion")->click();
                child->reject();
            }
        });
        historyTimer.start();
        showXerFinancials(nullptr, *doc.project());
        historyTimer.stop();
        check(inspected && inspectedPreview, "saved original/conversion inspector absent");
        // Native dialog exercises load, explicit rules, preview, invalidation and final merge submission.
        auto foreignBytes = bytes;
        foreignBytes.replace("\tUSD", "\tEUR");
        foreignBytes.replace("RT_Labor", "RT_TEMP");
        foreignBytes.replace("RT_Equip", "RT_Labor");
        foreignBytes.replace("RT_TEMP", "RT_Equip");
        const auto uiPath = temp.path() + "/foreign.xer";
        write(uiPath, foreignBytes);
        const auto uiDocPath = (temp.path() + "/ui.ppm").toStdString();
        store.save(uiDocPath, p, {});
        Document uiDoc(store);
        uiDoc.open(uiDocPath);
        auto runUi = [&](bool small, bool publish) {
            const auto before = *uiDoc.project();
            XerSourceRelinkDialog dialog(uiDoc);
            dialog.show();
            QApplication::processEvents();
            if (small)
                dialog.resize(800, 600);
            dialog.loadSource(uiPath);
            wait([&] { return dialog.findChild<QPushButton *>("xerRelinkOpen")->isEnabled(); });
            auto *table = dialog.findChild<QTableView *>("xerRelinkLinks");
            for (int i = 0; i < table->model()->rowCount(); ++i) {
                auto id = table->model()->index(i, 2).data().toString();
                check(table->model()->setData(table->model()->index(i, 4), id, Qt::EditRole), "UI explicit identity");
            }
            dialog.findChild<QCheckBox *>("xerIncomingConversionEnabled")->setChecked(true);
            dialog.findChild<QComboBox *>("xerIncomingConversionLaborType")->setCurrentIndex(1);
            dialog.findChild<QComboBox *>("xerIncomingConversionEquipmentType")->setCurrentIndex(0);
            auto *reference = dialog.findChild<QLineEdit *>("xerIncomingConversionReference");
            reference->setText("환율 1/1 · 단위 1/1 검토");
            auto *confirm = dialog.findChild<QCheckBox *>("xerRelinkConfirm");
            auto *review = dialog.findChild<QPushButton *>("xerRelinkReview");
            confirm->setChecked(true);
            reference->setText(reference->text() + " 완료");
            check(!confirm->isChecked() && !review->isEnabled(), "conversion edit retained confirmation");
            bool previewSeen = false;
            QTimer previewTimer;
            previewTimer.setInterval(20);
            QObject::connect(&previewTimer, &QTimer::timeout, [&] {
                auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (child && child->objectName() == "xerConversionPreview") {
                    previewSeen = true;
                    check(child->findChild<QTableView *>("xerConversionValues")->model()->rowCount() > 20,
                          "preview values absent");
                    if (small)
                        child->resize(800, 600);
                    child->grab().save("xer-conversion-qa/preview.png");
                    child->reject();
                }
            });
            previewTimer.start();
            dialog.findChild<QPushButton *>("xerRelinkConversionPreview")->click();
            previewTimer.stop();
            check(previewSeen && *uiDoc.project() == before, "preview mutation");
            confirm->setChecked(true);
            dialog.grab().save(small ? "xer-conversion-qa/editor-small.png" : "xer-conversion-qa/editor.png");
            bool seen = false, started = false;
            QTimer timer;
            timer.setInterval(20);
            QObject::connect(&timer, &QTimer::timeout, [&] {
                auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!child || child->objectName() != "xerFinancialMergeDialog")
                    return;
                seen = true;
                if (!started) {
                    started = true;
                    child->findChild<QLineEdit *>("xerMergeReporter")->setText("UI 검토자");
                    child->findChild<QLineEdit *>("xerMergeReason")->setText("통화·인력/장비 전환 확인");
                    child->findChild<QPushButton *>("xerMergeAnalyze")->click();
                    return;
                }
                auto *submit = child->findChild<QPushButton *>("xerMergeSubmit");
                if (submit->isEnabled()) {
                    child->grab().save("xer-conversion-qa/merge.png");
                    if (publish)
                        submit->click();
                    else
                        child->reject();
                }
            });
            timer.start();
            review->click();
            timer.stop();
            check(seen, "conversion merge UI absent");
            if (publish) {
                immutable(before, *uiDoc.project());
                check(nativeSchemaFor(*uiDoc.project()) == 31, "UI conversion not committed");
            } else {
                dialog.reject();
                check(*uiDoc.project() == before, "cancel committed conversion");
            }
        };
        runUi(true, false);
        runUi(false, true);
        std::cout
            << "PASS: explicit XER currency/type conversions, exact work, half-up/overflow oracle cases, immutable "
               "sources/native ledger, schema31/replay/recovery/copy, export, stale/cancel/read-only and native UI\n";
    } catch (const std::exception &e) {
        std::cerr << errorText(e) << '\n';
        return 1;
    }
    return 0;
}
