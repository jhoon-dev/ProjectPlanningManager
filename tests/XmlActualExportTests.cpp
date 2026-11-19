#include "XmlActualExportFixture.h"
#include "application/Document.h"
#include "domain/CalculationControl.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ResourceCurveExchange.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>
#include <algorithm>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error(message);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "save evidence");
}
struct Node {
    QString name, text;
    std::vector<Node> children;
    QString value(const QString &key) const {
        for (const auto &n : children)
            if (n.name == key)
                return n.text;
        return {};
    }
    bool has(const QString &key) const {
        for (const auto &n : children)
            if (n.name == key)
                return true;
        return false;
    }
};
Node read(QXmlStreamReader &r) {
    Node n;
    n.name = r.name().toString();
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement())
            n.children.push_back(read(r));
        else if (r.isCharacters())
            n.text += r.text();
        else if (r.isEndElement())
            break;
    }
    return n;
}
Node parse(const QByteArray &bytes) {
    QXmlStreamReader r(bytes);
    check(r.readNextStartElement(), "XML root");
    auto n = read(r);
    check(!r.hasError(), "XML valid");
    return n;
}
Node row(const Node &root, const QString &container, const QString &uid) {
    for (const auto &n : root.children)
        if (n.name == container)
            for (const auto &e : n.children)
                if (e.value("UID") == uid)
                    return e;
    throw std::runtime_error("XML UID missing");
}
void contract() {
    const auto p = xmlActualExportFixture();
    const auto original = p;
    const auto plan = exportProjectXml(p), out = exportProjectXmlActuals(p);
    check(p == original && out.project == p && out.exportingActuals && !out.importing, "immutable actual export");
    check(exportProjectXmlActuals(p).xml == out.xml && exportProjectXmlActuals(p).report == out.report,
          "deterministic actual export");
    check(!parse(plan.xml).has("StatusDate") && !plan.exportingActuals && !plan.report.contains("actualExport"),
          "default remains plan only");
    const auto xml = parse(out.xml);
    check(xml.value("StatusDate") == "2026-11-12T18:00:00" && xml.value("EditableActualCosts") == "1" &&
              xml.value("SpreadActualCost") == "0",
          "status and manual cost policy");
    const auto &a = row(xml, "Tasks", "1"), &b = row(xml, "Tasks", "2"), &c = row(xml, "Tasks", "3"),
               &d = row(xml, "Tasks", "4");
    check(a.value("ActualStart") == "2026-11-10T09:00:00" && !a.has("ActualFinish") &&
              a.value("ActualDuration") == "PT2H0M0S" && a.value("RemainingDuration") == "PT3H0M0S" &&
              a.value("Duration") == "PT5H0M0S",
          "latest task progress with observed total duration");
    check(a.value("ActualCost") == "10000" && a.value("ActualWork") == curveWorkText(24501),
          "authoritative task cost and observed work");
    check(b.value("ActualFinish") == "2026-11-10T18:00:00" && b.value("RemainingDuration") == "PT0H0M0S" &&
              b.value("ActualCost") == "4000",
          "completed task");
    check(!c.has("ActualCost") && !c.has("ActualWork") && !c.has("ActualDuration"), "unreported omitted");
    check(d.value("ActualCost") == "0" && d.value("ActualDuration") == "PT0H0M0S" &&
              d.value("RemainingDuration") == "PT0H0M0S" && !d.has("ActualStart") && !d.has("ActualWork"),
          "explicit zero differs from unknown");
    const auto &ar = row(xml, "Assignments", "1");
    check(ar.value("ActualWork") == curveWorkText(12501) && ar.value("ActualCost") == "1850" && !ar.has("Work") &&
              !ar.has("RemainingWork") && !ar.has("ActualStart"),
          "assignment actual sums without invented plan or dates");
    check(!row(xml, "Assignments", "4").has("ActualWork") && !row(xml, "Assignments", "4").has("ActualCost"),
          "missing assignment totals omitted");
    check(row(xml, "Resources", "1").value("ActualWork") == curveWorkText(14001) &&
              !row(xml, "Resources", "3").has("ActualWork") && !row(xml, "Resources", "1").has("ActualCost"),
          "resource observed work only");
    int workDays = 0, costDays = 0;
    for (const auto &n : xml.children)
        if (n.name == "Assignments")
            for (const auto &assignment : n.children)
                for (const auto &day : assignment.children)
                    if (day.name == "TimephasedData") {
                        check(day.value("Unit") == "2" && day.value("Start").endsWith("T00:00:00") &&
                                  day.value("Finish").endsWith("T00:00:00"),
                              "civil date bucket boundaries");
                        workDays += day.value("Type") == "2";
                        costDays += day.value("Type") == "6";
                        check(day.value("Type") == "2" || day.value("Type") == "6", "no planned TD in actual mode");
                    }
    check(workDays == 5 && costDays == 5, "exact daily count including explicit zero");
    const auto receipt = out.report["actualExport"].toObject();
    check(receipt["assignmentDays"].toArray().size() == 5 && receipt["taskProgress"].toArray().size() == 3 &&
              receipt["taskCost"].toArray().size() == 3,
          "as of selection ignores future history");
    check(projectXmlPreview(out).contains("61.25") && projectXmlPreview(out).contains("9501"),
          "preview exact allocation balance and work");
    const auto roundtrip = importProjectXml(out.xml, "actuals.xml");
    check(roundtrip.project.progress.empty() && roundtrip.project.periodActuals.empty() &&
              roundtrip.periodCandidates.size() == 5,
          "read preserves opt in adoption");
    std::vector<XmlPeriodMapping> mapping;
    for (const auto &r : roundtrip.periodCandidates) {
        check(r.issue.empty() && r.dateBucket && r.timedFinish.empty(), "valid date bucket candidates");
        mapping.push_back({r.key, r.taskUid == "1" ? "TN-A" : "TN-B", r.resourceUid == "1" ? "NR-1" : "NR-2"});
    }
    check(!prepareXmlPeriodMerge(xmlPeriodTarget(), roundtrip, xmlPeriodOptions(), mapping).empty(),
          "same status civil day bucket can be adopted");
    auto late = out.xml;
    late.replace("<Finish>2026-11-13T00:00:00</Finish>", "<Finish>2026-11-12T20:00:00</Finish>");
    rejects(
        [&] {
            prepareXmlPeriodMerge(xmlPeriodTarget(), importProjectXml(late, "late.xml"), xmlPeriodOptions(), mapping);
        },
        "timed report after status accepted");
    auto mixed = out.xml;
    const auto from = mixed.indexOf("<Start>2026-11-12T00:00:00</Start>");
    const auto to = mixed.indexOf("<Finish>2026-11-13T00:00:00</Finish>", from);
    mixed.replace(to, QString("<Finish>2026-11-13T00:00:00</Finish>").size(), "<Finish>2026-11-12T20:00:00</Finish>");
    rejects(
        [&] {
            prepareXmlPeriodMerge(xmlPeriodTarget(), importProjectXml(mixed, "mixed.xml"), xmlPeriodOptions(), mapping);
        },
        "full bucket must not mask a future timed fragment");
    auto unknown = p;
    PeriodActualSubmission clear;
    clear.reporter = "PM";
    clear.reason = "미보고 정정";
    clear.recordedAt = "2026-11-12T10:00:00Z";
    auto entry = *periodActualsAsOf(p, *p.statusAt).at({"TN-A", "NR-1", "2026-11-12"});
    entry.workUnitMinutes.reset();
    entry.actualMinor.reset();
    clear.entries = {entry};
    clear.totals = {{"TN-A", 10000}};
    int next = 0;
    appendPeriodActuals(unknown, clear, [&] { return "clear-" + std::to_string(++next); });
    ++unknown.revision;
    const auto unknownOut = exportProjectXmlActuals(unknown);
    const auto unknownXml = parse(unknownOut.xml);
    check(!row(unknownXml, "Assignments", "1").has("ActualWork") &&
              !row(unknownXml, "Assignments", "1").has("ActualCost") &&
              !row(unknownXml, "Tasks", "1").has("ActualWork") && !row(unknownXml, "Resources", "1").has("ActualWork"),
          "unknown correction suppresses incomplete scalars");
    auto unknownCost = p;
    auto cost = *costAsOf(p, "TN-A", *p.statusAt);
    cost.id = "unknown-cost";
    cost.previousEntryId = p.costEntries.back().id;
    cost.recordedRevision = ++unknownCost.revision;
    cost.actualMinor.reset();
    unknownCost.costEntries.push_back(cost);
    check(!row(parse(exportProjectXmlActuals(unknownCost).xml), "Tasks", "1").has("ActualCost"),
          "unknown current cumulative never falls back to older amount");
    auto remaining = p;
    remaining.progress[1].remainingWorkUnitMinutes = 6000;
    check(row(parse(exportProjectXmlActuals(remaining).xml), "Tasks", "1").value("RemainingWork") ==
              curveWorkText(6000),
          "explicit remaining work preserved");
    auto curves = p;
    curves.assignments[0].periods = {{0, 480, 50}, {480, 960, 100}};
    const auto plannedAssignment = row(parse(exportProjectXml(curves).xml), "Assignments", "1");
    const auto actualAssignment = row(parse(exportProjectXmlActuals(curves).xml), "Assignments", "1");
    const auto hasPlan = [](const Node &n) {
        return std::any_of(n.children.begin(), n.children.end(),
                           [](const Node &d) { return d.name == "TimephasedData" && d.value("Type") == "1"; });
    };
    check(hasPlan(plannedAssignment) && !hasPlan(actualAssignment), "plan curves only in default mode");
    auto krw = p;
    krw.currency = "KRW";
    for (auto &e : krw.costEntries)
        e.currency = "KRW";
    for (auto &e : krw.periodActuals)
        e.currency = "KRW";
    const auto krwXml = parse(exportProjectXmlActuals(krw).xml);
    check(row(krwXml, "Tasks", "1").value("ActualCost") == "1000000" &&
              row(krwXml, "Assignments", "1").value("ActualCost") == "185000",
          "zero decimal currency exact");
    auto futureBucket = out.xml;
    futureBucket.replace("<Start>2026-11-12T00:00:00</Start>", "<Start>2026-11-13T00:00:00</Start>");
    futureBucket.replace("<Finish>2026-11-13T00:00:00</Finish>", "<Finish>2026-11-14T00:00:00</Finish>");
    const auto futureSource = importProjectXml(futureBucket, "future-bucket.xml");
    std::vector<XmlPeriodMapping> futureMapping;
    for (const auto &e : futureSource.periodCandidates)
        if (e.date == "2026-11-13")
            futureMapping.push_back({e.key, "TN-A", "NR-1"});
    rejects([&] { prepareXmlPeriodMerge(xmlPeriodTarget(), futureSource, xmlPeriodOptions(), futureMapping); },
            "future civil bucket accepted");
    auto noStatus = p;
    noStatus.statusAt.reset();
    rejects([&] { exportProjectXmlActuals(noStatus); }, "missing status accepted");
    auto orphan = p;
    orphan.assignments.erase(orphan.assignments.begin());
    rejects([&] { exportProjectXmlActuals(orphan); }, "orphan observed assignment silently dropped");
    auto direct = p;
    auto directEntry = entry;
    directEntry.resourceId.clear();
    directEntry.workUnitMinutes.reset();
    directEntry.actualMinor = 1;
    clear.entries = {directEntry};
    next = 0;
    appendPeriodActuals(direct, clear, [&] { return "direct-" + std::to_string(++next); });
    ++direct.revision;
    rejects([&] { exportProjectXmlActuals(direct); }, "task direct amount silently dropped");
    QTemporaryDir tmp;
    const auto destination = tmp.filePath("actuals");
    const auto folder = publishProjectXml(out, destination);
    JsonProjectStore store;
    check(store.load((folder + "/source.ppm").toStdString()).project == p, "full native history companion");
    check(readProjectXml(folder + "/project.xml") == out.xml, "published XML exact");
    rejects([&] { publishProjectXml(out, destination); }, "existing destination overwritten");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    const auto cancelledPath = tmp.filePath("cancelled");
    try {
        EvaluationScope scope(control);
        publishProjectXml(out, cancelledPath);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && !QFileInfo::exists(cancelledPath), "cancelled publish has no output");
    for (int kind = 0; kind < 5; ++kind) {
        auto tampered = out;
        if (kind == 0)
            tampered.xml.replace("<ActualCost>10000</ActualCost>", "<ActualCost>9900</ActualCost>");
        if (kind == 1)
            tampered.report["diagnostics"] = QJsonArray{};
        if (kind == 2)
            tampered.report["actualExport"] = QJsonObject{};
        if (kind == 3)
            tampered.exportingActuals = false;
        if (kind == 4)
            tampered.project.progress.back().reason = "미래 기록 변조";
        const auto bad = tmp.filePath(QString("bad-%1").arg(kind));
        rejects([&] { publishProjectXml(tampered, bad); }, "tampered publication accepted");
        check(!QFileInfo::exists(bad), "failed publish leaves no output");
    }
    const auto qa = QDir::currentPath() + "/xml-standard-actual-export-qa";
    QDir().mkpath(qa);
    save(qa + "/project.xml", out.xml);
    save(qa + "/plan.xml", plan.xml);
    save(qa + "/conversion.json", QJsonDocument(out.report).toJson());
    save(qa + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(qa + "/roundtrip.json", QJsonDocument(json::encodeProject(roundtrip.project)).toJson());
    save(qa + "/unknown.xml", unknownOut.xml);
    QFile::remove(qa + "/source.ppm");
    store.save((qa + "/source.ppm").toStdString(), p, {});
    std::cout << "PASS standard XML actuals: deterministic as-of task/assignment/resource projection, exact daily "
                 "work/cost/zero/unknown, future history, roundtrip date buckets, no guessed facts, strict unsupported "
                 "shapes, replay/atomic publication\n";
}
void ui() {
    const auto p = xmlActualExportFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("native.ppm").toStdString();
    store.save(path, p, {});
    Document document(store);
    document.open(path);
    MainWindow window(document);
    window.show();
    auto *action = window.findChild<QAction *>("exportProjectXmlActuals");
    check(action && action->isEnabled(), "actual export action");
    for (int mode = 0; mode < 3; ++mode) {
        bool checked = false, errorSeen = false;
        QTimer timer;
        timer.setInterval(20);
        const auto destination = tmp.filePath(QString("published-%1").arg(mode));
        QObject::connect(&timer, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errorSeen = true;
                box->accept();
                return;
            }
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "projectXmlPreview" || checked)
                return;
            auto *details = dialog->findChild<QPlainTextEdit *>("xmlDiagnostics");
            check(details && details->toPlainText().contains("9501") && dialog->windowTitle().contains("실적"),
                  "actual preview details");
            dialog->findChild<QLineEdit *>("xmlDestination")->setText(destination);
            checked = true;
            if (mode == 0) {
                const auto qa = QDir::currentPath() + "/xml-standard-actual-export-qa";
                QDir().mkpath(qa);
                dialog->resize(800, 650);
                dialog->grab().save(qa + "/actuals-preview-small.png");
            }
            if (mode == 2)
                document.renameTask(0, "미리보기 이후 편집");
            if (mode == 1)
                dialog->reject();
            else
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        timer.start();
        action->trigger();
        timer.stop();
        check(checked, "preview shown");
        check(QFileInfo::exists(destination) == (mode == 0), "publish cancel or stale guard");
        check(errorSeen == (mode == 2), "stale error visible");
        check(store.load(path).project == p && document.path() == path,
              "original native file and document remain open");
        if (mode < 2)
            check(*document.project() == p && !document.dirty(), "export does not mutate or dirty document");
    }
    check(document.project()->tasks[0].name == "미리보기 이후 편집" && document.dirty(),
          "stale export preserves edits");
    std::cout << "PASS standard XML actuals UI: explicit action, actual preview, create-only publication, original "
                 "preserved, cancel and stale edit guard, 800px review\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
