#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/PeriodActuals.h"
#include "calculation/Costs.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/ProjectJson.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "ui/MainWindow.h"
#include "ui/XerFinancialDialog.h"
#include <QApplication>
#include <QAction>
#include <QCryptographicHash>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <iostream>
#include <functional>
namespace {
using namespace ppm;
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
    check(rejected, "expected rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write QA output");
}
QByteArray envelope(const QJsonObject &body, int schema) {
    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return QJsonDocument(
               QJsonObject{{"format", "PPM"},
                           {"schemaVersion", schema},
                           {"payload", body},
                           {"sha256",
                            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}})
        .toJson();
}
void dialog(const Project &p, bool current, bool small, std::function<void()> launch = {}) {
    bool seen = false;
    QTimer timer;
    timer.setInterval(20);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "xerFinancialDialog" || seen)
            return;
        seen = true;
        const auto fail = [](bool b, const char *m) {
            if (!b) {
                std::cerr << m << '\n';
                std::_Exit(2);
            }
        };
        if (small)
            d->resize(800, 600);
        auto *label = d->findChild<QLabel *>("xerFinancialStatus");
        fail(label && label->textFormat() == Qt::PlainText, "source metadata interpreted as markup");
        fail(label && label->text().contains(current ? "현재 문서와 일치" : "문서 변경"), "snapshot state missing");
        auto *tabs = d->findChild<QTabWidget *>("xerFinancialTabs");
        auto *a = d->findChild<QTableView *>("xerFinancialTable0");
        auto *r = d->findChild<QTableView *>("xerFinancialTable1");
        auto *t = d->findChild<QTableView *>("xerFinancialTable2");
        fail(tabs && tabs->count() == 3 && a && r && t, "snapshot tables missing");
        fail(a->model()->rowCount() == 2 && r->model()->rowCount() == 2 && t->model()->rowCount() == 2,
             "periods not inspectable");
        fail(a->model()->data(a->model()->index(0, 5)).toString() == "0.500000", "undated work UI");
        fail(t->model()->data(t->model()->index(0, 7)).toString() == "9999.00",
             "external earned value not labeled independently");
        fail(a->editTriggers() == QAbstractItemView::NoEditTriggers, "reference snapshot editable");
        d->grab().save(small ? "xer-financial-qa/view-small.png" : "xer-financial-qa/view.png");
        tabs->setCurrentIndex(1);
        d->grab().save("xer-financial-qa/periods.png");
        tabs->setCurrentIndex(2);
        t->horizontalScrollBar()->setValue(t->horizontalScrollBar()->maximum());
        d->grab().save("xer-financial-qa/external-ev.png");
        auto *search = d->findChild<QLineEdit *>("xerFinancialSearch");
        fail(search, "search missing");
        search->setText("no-match");
        fail(a->model()->rowCount() == 0, "snapshot search");
        d->reject();
    });
    timer.start();
    if (launch)
        launch();
    else
        showXerFinancials(nullptr, p);
    check(seen, "snapshot dialog not opened");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] { std::_Exit(3); });
    watchdog.start(120000);
    try {
        const auto source = readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE));
        auto imported = importPrimaveraXer(source, "financial-periods.xer");
        const auto p = imported.project;
        check(p.xerFinancials && nativeSchemaFor(p) == 25 && xerFinancialsCurrent(p),
              "schema25/current snapshot capture");
        const auto &s = *p.xerFinancials;
        check(s.assignments.size() == 2 && s.periods.size() == 4 && s.periods[0].start == "2026-10-19 00:00",
              "multi-day financial range lost");
        check(s.assignments[0].regularWork == 19500 && s.assignments[0].overtimeWork == 3000 &&
                  s.assignments[0].regularMinor == 9012 && s.assignments[0].overtimeMinor == 2034,
              "regular/overtime source totals");
        check(p.periodActuals.size() == 2 && actualCostAllocations(p, *p.statusAt).at("xer-task-20").allocated == 3334,
              "aggregate was spread into native dated actuals");
        check(costAsOf(p, "xer-task-20", *p.statusAt)->actualMinor == 12280 && !analyzeCosts(p, "").tasks[0].evm.ev,
              "external EVM leaked into native values");
        auto out = exportPrimaveraXer(p);
        auto mismatched = p;
        mismatched.xerFinancials->tasks[0].actualMinor = 12000;
        validate(mismatched);
        check(!xerFinancialsCurrent(mismatched), "snapshot cumulative AC was not compared to native AC");
        auto mismatchedBack = importPrimaveraXer(exportPrimaveraXer(mismatched).xer, "mismatch.xer");
        check(costAsOf(mismatchedBack.project, mismatchedBack.project.tasks[0].id, *mismatchedBack.project.statusAt)
                      ->actualMinor == 12280,
              "inconsistent reference snapshot changed native cumulative AC");
        auto back = importPrimaveraXer(out.xer, "portable.xer");
        check(out.report["actualCosts"].toObject()["preservedFinancials"] == "reexported" && back.project.xerFinancials,
              "snapshot reexport unavailable");
        check(back.project.xerFinancials->periods[0].start == s.periods[0].start &&
                  back.project.xerFinancials->assignments[0].overtimeWork == 3000 &&
                  back.project.xerFinancials->assignments[0].regularMinor == 9012,
              "XER aggregate component/range roundtrip");
        check(back.project.periodActuals.size() == 2 &&
                  costAsOf(back.project, back.project.tasks[0].id, *back.project.statusAt)->actualMinor == 12280,
              "XER reconciliation changed AC");
        QTemporaryDir tmp;
        check(tmp.isValid(), "temp directory");
        JsonProjectStore store;
        const auto path = tmp.filePath("financial.ppm").toStdString();
        store.save(path, p, {});
        check(store.load(path).project == p, "native snapshot roundtrip");
        Document doc(store);
        doc.open(path);
        doc.renameTask(0, "Changed task");
        check(doc.dirty() && !xerFinancialsCurrent(*doc.project()) && doc.project()->xerFinancials == p.xerFinancials,
              "stale snapshot was dropped");
        auto stale = exportPrimaveraXer(*doc.project());
        check(stale.report["actualCosts"].toObject()["preservedFinancials"] == "stale-source-only",
              "stale aggregate silently reexported");
        doc.undo();
        check(!doc.dirty() && xerFinancialsCurrent(*doc.project()), "undo did not restore snapshot applicability");
        doc.redo();
        const auto changed = *doc.project();
        JsonRecoveryStore recovery(tmp.filePath("recovery"));
        auto session = recovery.newSession();
        session->checkpoint(doc.recoverySnapshot("2026-11-07T00:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema25 recovery scan");
        auto claim = recovery.claim(scan.candidates[0]);
        check(claim && claim->snapshot.project == changed, "schema25 recovery lost reference snapshot");
        doc.undo();
        doc.saveAs(tmp.filePath("copy.ppm").toStdString());
        check(doc.project()->documentId != p.documentId && xerFinancialsCurrent(*doc.project()),
              "independent native copy invalidated unchanged source evidence");
        auto body = json::encodeProject(p);
        check(decodeProjectFile(envelope(body, 25)) == p, "schema25 envelope");
        rejects([&] { decodeProjectFile(envelope(body, 24)); });
        check(decodeProjectFile(envelope(body, 30)) == p, "legacy data in schema30 envelope");
        check(decodeProjectFile(envelope(body, 52)) == json::decodeProject(body, 52), "supported schema52");
        rejects([&] { decodeProjectFile(envelope(body, maximumNativeSchema + 1)); });
        auto financial = body["xerFinancials"].toObject();
        auto periods = financial["periods"].toArray();
        auto row = periods[0].toObject();
        row["work"] = 1.5;
        periods[0] = row;
        financial["periods"] = periods;
        auto bad = body;
        bad["xerFinancials"] = financial;
        rejects([&] { decodeProjectFile(envelope(bad, 25)); });
        auto invalid = s;
        invalid.periods.push_back(invalid.periods[0]);
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[0].finish = "2026-11-01 23:59";
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[0].work = maxWork;
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[0].resourceId = "missing";
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[0].bcwp = 1;
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[0].actualMinor = -1;
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.sourceSha256 = "bad";
        rejects([&] { validateXerFinancials(invalid); });
        invalid = s;
        invalid.periods[1].start = "2026-10-25 00:00";
        rejects([&] { validateXerFinancials(invalid); });
        auto orphan = p;
        orphan.tasks.clear();
        orphan.assignments.clear();
        orphan.taskCosts.clear();
        orphan.progress.clear();
        orphan.costEntries.clear();
        orphan.periodActuals.clear();
        validate(orphan);
        check(!xerFinancialsCurrent(orphan) && json::decodeProject(json::encodeProject(orphan), 25) == orphan,
              "historic snapshot references were treated as current plan references");
        auto other = p;
        other.documentId = "second";
        std::vector<PrimaveraXerExchange> batch{out, exportPrimaveraXer(other)};
        const auto combined = combinePrimaveraXer(batch);
        for (const auto &info : listPrimaveraXerProjects(combined)) {
            const auto selected = importPrimaveraXer(combined, "two.xer", XerEncoding::Utf8, info.id);
            check(selected.project.xerFinancials && selected.project.xerFinancials->periods.size() == 4 &&
                      selected.project.xerFinancials->assignments[0].overtimeWork == 3000,
                  "combined financial references leaked across projects");
        }
        QDir().mkpath("xer-financial-qa");
        write("xer-financial-qa/source.xer", source);
        write("xer-financial-qa/export.xer", out.xer);
        write("xer-financial-qa/combined.xer", combined);
        write("xer-financial-qa/snapshot.json", QJsonDocument(encodeXerFinancials(s)).toJson());
        write("xer-financial-qa/stale-report.json", QJsonDocument(stale.report).toJson());
        const auto artifact = QDir::current().filePath("xer-financial-qa/project.ppm").toStdString();
        store.save(artifact, p,
                   QFileInfo::exists(QString::fromStdString(artifact)) ? store.load(artifact).fingerprint : "");
        dialog(p, true, false);
        dialog(p, true, true);
        dialog(changed, false, false);
        MainWindow window(doc);
        auto *action = window.findChild<QAction *>("viewXerFinancials");
        check(action && action->isEnabled(), "main window snapshot action unavailable");
        dialog(*doc.project(), true, false, [&] { action->trigger(); });
        check(p == imported.project, "read-only source mutated");
        std::cout
            << "PASS: schema25 XER financial snapshots, preserved intervals/undated regular+OT, AC/EVM isolation, "
               "stale/undo/copy/recovery, strict validation, combined references, virtual tables and search\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (const auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &v : a->diagnostic().arguments)
                std::cerr << v << '\n';
        return 1;
    }
}
