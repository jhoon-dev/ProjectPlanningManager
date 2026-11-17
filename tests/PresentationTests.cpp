#include "application/Document.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/SchedulePrint.h"
#include "reporting/NetworkPrint.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPrinter>
#include <QTemporaryDir>
#include <iostream>
#include <tuple>
namespace {
using namespace jobs;
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool no = false;
    try {
        f();
    } catch (const std::exception &) {
        no = true;
    }
    check(no, "expected rejection");
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Project p;
    p.documentId = "presentation-contract";
    p.name = "인쇄·네트워크 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"W", "시설", ""}, {"S", "설계", "W"}};
    p.tasks = {{"TA", "기초 설계", 480, {}, "S"}, {"TB", "상세 설계", 480, {"TA"}, "S"}};
    p.tasks[1].dependencyRules = {{"TA", DependencyType::FS, 60}};
    store.save(temp.filePath("input.ppm").toStdString(), p, {});
    Document d(store);
    d.open(temp.filePath("input.ppm").toStdString());
    const auto baseline = d.captureBaseline("승인", "담당", "확인", "2026-11-05T03:00:00Z");
    const auto frozen = d.project()->baselines;
    d.applyCalculation(calculate(*d.project()));
    PrintPreferences pref;
    pref.paper = 1;
    pref.margin = 12;
    pref.relations = true;
    pref.wbs = true;
    pref.networkFit = false;
    d.setPrintPreferences(pref);
    d.setNetworkPositions({{"TA", 20, 40}, {"TB", 800, 50}});
    check(d.dirty() && d.calculation()->status == CalculationStatus::Valid && d.project()->baselines == frozen,
          "presentation changed schedule/history");
    d.undo();
    check(d.project()->networkPositions.empty(), "layout undo");
    d.redo();
    check(d.project()->networkPositions.size() == 2, "layout redo");
    check(nativeSchemaFor(*d.project()) == 18, "schema18 writer");
    const auto json = json::encodeProject(*d.project());
    check(json::decodeProject(json, 18) == *d.project(), "schema18 round trip");
    rejects([&] { json::decodeProject(json, 17); });
    for (const auto &badValue :
         {QJsonValue(true), QJsonValue(0.5), QJsonValue("20"), QJsonValue(QJsonValue::Null), QJsonValue(1000001)}) {
        auto bad = json;
        auto a = bad["networkPositions"].toArray();
        auto v = a[0].toObject();
        v["x"] = badValue;
        a[0] = v;
        bad["networkPositions"] = a;
        rejects([&] { json::decodeProject(bad, 18); });
    }
    auto bad = *d.project();
    bad.networkPositions.push_back(bad.networkPositions[0]);
    rejects([&] { validate(bad); });
    bad = *d.project();
    bad.networkPositions[0].taskId = "missing";
    rejects([&] { validate(bad); });
    bad = *d.project();
    bad.printPreferences->margin = 0;
    rejects([&] { validate(bad); });
    const auto out = QDir::currentPath() + "/print-network-qa";
    QDir().mkpath(out);
    for (const auto *file :
         {"presentation.ppm", "schedule.pdf", "network-fit.pdf", "network-tiles.pdf", "network-printer.pdf"})
        QFile::remove(out + "/" + file);
    d.saveAs((out + "/presentation.ppm").toStdString());
    SchedulePrintSource source;
    source.project = d.snapshot();
    source.scope = "표시 2작업";
    source.generatedAt = "2026-11-05T03:00:00Z";
    source.printId = "print-network-contract";
    source.baselineId = QString::fromStdString(baseline);
    source.columns = {
        {"number", "원본 번호", 36}, {"name", "작업", 140}, {"start", "시작", 100}, {"finish", "완료", 100}};
    source.rows = {{"TA", 1, {"1", "기초 설계", "", ""}}, {"TB", 2, {"2", "상세 설계", "", ""}}};
    source.selected = {"TA"};
    SchedulePrintOptions options;
    options.font = QGuiApplication::font();
    options.wbs = true;
    options.relations = true;
    const auto layout = buildSchedulePrint(source, options);
    check(layout.taskCount == 2 && layout.summaryCount == 2 && layout.relations.size() == 1, "summary/relations");
    check(layout.rows[0].summary && layout.rows[0].cells[1].contains("2작업") &&
              layout.rows[0].bars[0].start == "2026-11-02 09:00",
          "scoped WBS envelope");
    const std::vector<std::tuple<DependencyType, std::string, std::string>> ports{
        {DependencyType::FS, "2026-11-02 18:00", "2026-11-03 09:00"},
        {DependencyType::SS, "2026-11-02 09:00", "2026-11-02 09:00"},
        {DependencyType::FF, "2026-11-02 18:00", "2026-11-02 18:00"},
        {DependencyType::SF, "2026-11-02 09:00", "2026-11-02 18:00"}};
    for (const auto &[type, from, to] : ports) {
        auto variant = *source.project;
        variant.tasks[1].dependencyRules =
            type == DependencyType::FS ? std::vector<Dependency>{} : std::vector<Dependency>{{"TA", type, 0}};
        auto input = source;
        input.project = std::make_shared<const Project>(variant);
        auto opts = options;
        opts.wbs = false;
        const auto view = buildSchedulePrint(input, opts);
        if (view.relations.size() != 1 || view.relations[0].start.toStdString() != from ||
            view.relations[0].finish.toStdString() != to) {
            std::cerr << "type=" << int(type) << " edges=" << view.relations.size();
            if (!view.relations.empty())
                std::cerr << " from=" << view.relations[0].start.toStdString()
                          << " to=" << view.relations[0].finish.toStdString();
            std::cerr << '\n';
        }
        check(view.relations.size() == 1 && view.relations[0].start.toStdString() == from &&
                  view.relations[0].finish.toStdString() == to,
              "S/F print ports");
    }
    auto forecastProject = p;
    forecastProject.documentId = "presentation-forecast";
    const auto forecastPath = temp.filePath("forecast.ppm").toStdString();
    store.save(forecastPath, forecastProject, {});
    Document forecastDoc(store);
    forecastDoc.open(forecastPath);
    forecastDoc.setStatusAt("2026-11-02 12:00");
    ProgressEntry fact;
    fact.taskId = "TB";
    fact.statusAt = *forecastDoc.project()->statusAt;
    fact.recordedAt = "2026-11-05T03:00:00Z";
    fact.reporter = "현장";
    fact.reason = "재개 확인";
    fact.actualStart = "2026-11-02 09:00";
    fact.actualDurationMinutes = 180;
    fact.remainingMinutes = 480;
    forecastDoc.recordProgress(fact);
    auto futureInput = source;
    futureInput.project = forecastDoc.snapshot();
    futureInput.baselineId.clear();
    auto futureOptions = options;
    futureOptions.wbs = false;
    const auto futurePrint = buildSchedulePrint(futureInput, futureOptions);
    check(futurePrint.relations.size() == 1 && futurePrint.relations[0].finish == "2026-11-03 14:00",
          "retained relationship must target remaining resume");
    fact.actualFinish = "2026-11-02 12:00";
    fact.remainingMinutes = 0;
    forecastDoc.recordProgress(fact);
    futureInput.project = forecastDoc.snapshot();
    const auto completedPrint = buildSchedulePrint(futureInput, futureOptions);
    check(completedPrint.relations.size() == 1 && completedPrint.relations[0].inactive &&
              completedPrint.relations[0].label.contains("완료 실적 참고"),
          "completed incoming relationship applicability");
    options.selectedOnly = true;
    const auto selection = buildSchedulePrint(source, options);
    check(selection.taskCount == 1 && selection.summaryCount == 2 && selection.relations.empty() &&
              selection.omittedRelations == 1 && selection.rows[0].cells[1].contains("1작업"),
          "selection scope expanded");
    check(exportSchedulePdf(layout, out + "/schedule.pdf") == int(layout.pages.size()), "schedule export");
    QPicture picture;
    QPainter painter(&picture);
    painter.setFont(QGuiApplication::font());
    painter.fillRect(QRectF(0, 0, 300, 150), QColor("#d0efff"));
    painter.drawText(QRectF(10, 20, 280, 50), "기초 설계 · TA");
    painter.drawLine(300, 75, 2800, 75);
    painter.drawText(QRectF(1500, 80, 200, 40), "FS +60m");
    painter.fillRect(QRectF(2800, 0, 300, 150), QColor("#ffe0d0"));
    painter.drawText(QRectF(2810, 20, 280, 50), "상세 설계 · TB");
    painter.end();
    pref.paper = 0;
    pref.margin = 10;
    pref.networkFit = true;
    const auto fit = buildNetworkPrint(picture, QRectF(-10, -10, 3120, 180), pref, "네트워크 검증",
                                       "문서 presentation-contract · 2카드 1선 · 전체 계산 · rev " +
                                           QString::number(d.project()->revision));
    check(fit.pageCount() == 1 && exportNetworkPdf(fit, out + "/network-fit.pdf") == 1, "network fit");
    pref.networkFit = false;
    const auto tiled = buildNetworkPrint(picture, fit.bounds, pref, fit.title, fit.provenance);
    check(tiled.pageCount() > 1 && exportNetworkPdf(tiled, out + "/network-tiles.pdf") == tiled.pageCount(),
          "network tiles");
    QFile::remove(out + "/network-notes.pdf");
    const auto withNotes =
        buildNetworkPrint(picture, fit.bounds, pref, fit.title, fit.provenance,
                          {"전체 작업명/ID · TA · 기초 설계", "전체 관계 · TA → TB · FS +60m", QString(5000, 'x')});
    check(withNotes.pageCount() > tiled.pageCount() &&
              exportNetworkPdf(withNotes, out + "/network-notes.pdf") == withNotes.pageCount(),
          "network full source appendix");
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setResolution(144);
    printer.setOutputFileName(out + "/network-printer.pdf");
    check(paintNetwork(printer, tiled) == tiled.pageCount(), "network printer pages");
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool stopped = false;
    try {
        exportNetworkPdf(tiled, out + "/cancelled.pdf", cancelled);
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped && !QFile::exists(out + "/cancelled.pdf"), "network cancel published");
    rejects([&] { exportNetworkPdf(fit, out + "/network-fit.pdf"); });
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    d.setPrintPreferences(pref);
    session->checkpoint(d.recoverySnapshot("2026-11-05T03:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema18 recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document recovered(store);
    recovered.recover(claim->snapshot);
    check(recovered.project()->printPreferences == d.project()->printPreferences &&
              recovered.project()->networkPositions == d.project()->networkPositions,
          "recovery presentation");
    d.addTask("仮");
    const auto id = d.project()->tasks.back().id;
    auto positions = d.project()->networkPositions;
    positions.push_back({id, 100, 100});
    d.setNetworkPositions(positions);
    d.removeTasks({id});
    check(d.project()->networkPositions.size() == 2, "delete orphan position");
    d.undo();
    check(d.project()->networkPositions.size() == 3, "delete undo lost position");
    const auto before = *d.project();
    rejects([&] { d.setNetworkPositions({{"missing", 0, 0}}); });
    check(*d.project() == before, "invalid positions partial commit");
    d.open((out + "/presentation.ppm").toStdString(), true);
    rejects([&] { d.setPrintPreferences({}); });
    rejects([&] { d.setNetworkPositions({}); });
    auto leveled = p;
    leveled.documentId = "presentation-selected";
    leveled.resources = {{"R", "인력", "labor", 200, {}}};
    leveled.assignments = {{"TA", "R", 100}, {"TB", "R", 100}};
    const auto adoptedPath = temp.filePath("leveled.ppm").toStdString();
    store.save(adoptedPath, leveled, {});
    Document adopted(store);
    adopted.open(adoptedPath);
    LevelingSelection policy;
    policy.priority = "task-order";
    policy.horizonWorkDays = 30;
    policy.policy = levelingPolicyFor(*adopted.project());
    policy.adoptedAt = "2026-11-05T03:00:00Z";
    policy.reason = "채택";
    policy.sourceDocumentId = adopted.project()->documentId;
    policy.sourceRevision = adopted.project()->revision;
    adopted.adoptLeveling(policy);
    adopted.applyCalculation(calculate(*adopted.project()));
    const auto selected = adopted.project()->planLeveling;
    const auto decisions = adopted.project()->levelingDecisions;
    adopted.setPrintPreferences(pref);
    adopted.setNetworkPositions({{"TA", 40, 50}});
    check(adopted.project()->planLeveling == selected && adopted.project()->levelingDecisions == decisions &&
              adopted.calculation()->status == CalculationStatus::Valid,
          "presentation invalidated adopted leveling");
    check(nativeSchemaFor(p) == 9, "old writer drift");
    QFile meta(out + "/expected.json");
    check(meta.open(QIODevice::WriteOnly), "metadata");
    meta.write(QJsonDocument(QJsonObject{{"schedule_pages", int(layout.pages.size())},
                                         {"network_tiles", tiled.pageCount()},
                                         {"wbs_rows", 2},
                                         {"relations", 1}})
                   .toJson());
    std::cout
        << "Presentation: schema18/history/dirty/recovery/scoped-WBS/relations/vector PDF/printer/cancel passed\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
