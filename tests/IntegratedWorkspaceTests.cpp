#include "persistence/XerBatch.h"
#include <algorithm>
#include <tuple>
#include "calculation/EnginePolicy.h"
#include "calculation/IntegratedWorkspace.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include "reporting/IntegratedExport.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <functional>
using namespace jobs;
namespace {
void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
void rejects(const std::function<void()> &f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
Project project(const std::string &id) {
    Project p;
    p.documentId = id;
    p.name = "Project " + id;
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"A", "동일 작업 ID", 480, {}}};
    p.resources = {{"R", "Crew", "labor", 100, {}}};
    p.assignments = {{"A", "R", 100}};
    return p;
}
Workspace workspace() {
    Workspace w;
    w.id = "integrated-fixture";
    w.projects = {{"P", "", "Project P"}, {"Q", "", "Project Q"}};
    w.resources = {{"crew", "공유 시공조", "labor", 100}};
    w.bindings = {{"P", "R", "crew"}, {"Q", "R", "crew"}};
    w.integrated.forecast = false;
    return w;
}
std::vector<std::shared_ptr<const Project>> snapshots(const Project &p, const Project &q) {
    return {std::make_shared<const Project>(p), std::make_shared<const Project>(q)};
}
const IntegratedTask &row(const IntegratedResult &r, const std::string &pid) {
    for (const auto &t : r.tasks)
        if (t.ref.projectId == pid)
            return t;
    throw std::runtime_error("missing integrated row");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
void contracts() {
    auto p = project("P"), q = project("Q");
    auto w = workspace();
    const auto sourceP = p, sourceQ = q;
    auto empty = project("empty");
    empty.tasks.clear();
    empty.assignments.clear();
    empty.resources.clear();
    empty.startDate = "2027-01-04";
    auto withEmpty = w;
    withEmpty.projects.push_back({"empty", "", "Empty"});
    auto inputs = snapshots(p, q);
    inputs.push_back(std::make_shared<const Project>(empty));
    auto emptyResult = calculateIntegratedWorkspace(withEmpty, inputs);
    check(emptyResult.status == CalculationStatus::Valid && emptyResult.finish == "2026-11-02 18:00",
          "empty project release extended integrated finish");
    auto r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid && r.tasks.size() == 2, "logical integrated calculation");
    check(r.shared.status == CalculationStatus::Conflict && r.shared.days[0].peakUnits == 200,
          "independent overlap missing");
    w.integrated.levelShared = true;
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    if (r.status != CalculationStatus::Valid) {
        for (const auto &d : r.diagnostics)
            std::cerr << d.code << ' ' << (d.arguments.empty() ? "" : d.arguments[0]) << '\n';
    }
    check(r.status == CalculationStatus::Valid && r.shared.status == CalculationStatus::Valid,
          "integrated shared leveling");
    check(row(r, "P").schedule.start == "2026-11-02 09:00" && row(r, "Q").schedule.start == "2026-11-03 09:00",
          "shared serial golden");
    check(p == sourceP && q == sourceQ, "integrated changed source projects");
    for (const auto &d : r.shared.days)
        check(d.peakUnits <= 100 && d.overloadMinutes == 0, "leveling left pooled overload");
    w.integrated.levelShared = false;
    for (const auto &[type, lag, expected] :
         std::vector<std::tuple<DependencyType, int, std::string>>{{DependencyType::FS, 0, "2026-11-03 09:00"},
                                                                   {DependencyType::SS, 60, "2026-11-02 10:00"},
                                                                   {DependencyType::FF, 60, "2026-11-02 10:00"},
                                                                   {DependencyType::SF, 960, "2026-11-03 09:00"},
                                                                   {DependencyType::FS, -60, "2026-11-02 17:00"}}) {
        w.links = {{"L", "P", "A", "Q", "A", type, lag}};
        r = calculateIntegratedWorkspace(w, snapshots(p, q));
        check(r.status == CalculationStatus::Valid, "cross relation rejected");
        if (row(r, "Q").schedule.start != expected)
            std::cerr << "actual " << row(r, "Q").schedule.start << " expected " << expected << '\n';
        check(row(r, "Q").schedule.start == expected, "cross relation golden");
        check(row(r, "Q").ref.taskId == "A" && row(r, "P").ref.taskId == "A", "qualified IDs lost");
    }
    w.links = {{"L", "P", "A", "Q", "A", DependencyType::FS, 0}, {"back", "Q", "A", "P", "A", DependencyType::FS, 0}};
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Conflict && r.tasks.empty(), "global cycle accepted");
    w.links.pop_back();
    w.links[0].successorTaskId = "deleted";
    check(calculateIntegratedWorkspace(w, snapshots(p, q)).status == CalculationStatus::Conflict,
          "dangling task accepted");
    w.links[0].successorTaskId = "A";
    auto duplicated = w;
    duplicated.links.push_back(w.links[0]);
    duplicated.links.back().id = "dup";
    rejects([&] { validateWorkspace(duplicated); });
    auto same = w;
    same.links[0].successorProjectId = "P";
    rejects([&] { validateWorkspace(same); });
    check(calculateIntegratedWorkspace(w, {std::make_shared<const Project>(p)}).status == CalculationStatus::Conflict,
          "missing project accepted");
    w.links.clear();
    q.startDate = "2026-11-04";
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(row(r, "Q").schedule.start == "2026-11-04 09:00", "project release start lost");
    q = sourceQ;
    w.integrated.forecast = true;
    p.statusAt = "2026-11-03 09:00";
    check(calculateIntegratedWorkspace(w, snapshots(p, q)).status == CalculationStatus::Conflict,
          "mixed status dates accepted");
    q.statusAt = p.statusAt;
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid && r.forecast, "common status forecast");
    check(row(r, "P").schedule.start == "2026-11-03 09:00", "not started forecast release");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        calculateIntegratedWorkspace(w, snapshots(p, q), control);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancel ignored");
    p = sourceP;
    q = sourceQ;
    w = workspace();
    w.integrated.levelShared = true;
    TaskCalendar special;
    special.id = "special";
    special.name = "different";
    special.periods = {{600, 720}};
    p.calendars.push_back(special);
    p.resources[0].calendarId = "special";
    check(calculateIntegratedWorkspace(w, snapshots(p, q)).status == CalculationStatus::Conflict,
          "incompatible pool calendar accepted");
}
void advanced() {
    auto p = project("P"), q = project("Q");
    auto w = workspace();
    w.integrated.levelShared = true;
    p.resources.push_back({"R2", "Second crew", "labor", 100, {}});
    p.assignments[0].units = 50;
    p.assignments.push_back({"A", "R2", 50});
    w.bindings.push_back({"P", "R2", "crew"});
    auto r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid && r.shared.status == CalculationStatus::Valid,
          "same task pooled assignment merge");
    p.calculationProfile = "native-fs0-workminutes-v1";
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid, "mixed native profiles");
    p = project("P");
    w = workspace();
    w.integrated.forecast = true;
    for (auto *v : {&p, &q}) {
        v->statusAt = "2026-11-03 09:00";
        ProgressEntry e;
        e.id = "report-" + v->documentId;
        e.taskId = "A";
        e.statusAt = *v->statusAt;
        e.recordedAt = "2026-11-05T00:00:00Z";
        e.reporter = "PM";
        e.reason = "done";
        e.sourceDocumentId = v->documentId;
        e.recordedRevision = 1;
        e.planDurationMinutes = 480;
        e.actualStart = "2026-11-02 09:00";
        e.actualFinish = "2026-11-02 18:00";
        e.actualDurationMinutes = 480;
        e.remainingMinutes = 0;
        v->progress = {e};
        v->progressPolicy = ProgressPolicySelection{actualDatesProgressPolicy, "PM",          "2026-11-05T00:00:00Z",
                                                    "actual endpoint",         v->documentId, v->revision};
    }
    w.links = {{"L", "P", "A", "Q", "A", DependencyType::FS, 0}};
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid && row(r, "Q").schedule.start == "2026-11-02 09:00" &&
              row(r, "Q").schedule.completedActual,
          "integrated actual endpoint policy");
    check(r.finish == "2026-11-02 18:00", "completed integrated finish extended by private release node");
    w.links.push_back({"back", "Q", "A", "P", "A", DependencyType::FS, 0});
    r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Conflict, "actual masking hid global cycle");
}
void persistence() {
    QTemporaryDir tmp;
    JsonProjectStore files;
    auto p = project("P"), q = project("Q");
    auto w = workspace();
    w.links = {{"L", "P", "A", "Q", "A", DependencyType::FS, 0}};
    w.integrated.priority = "deadline-first";
    rejects([&] { exportXerBatch({p}, w); });
    auto batch = exportXerBatch({p, q}, w);
    check(batch.workspace.links == w.links && batch.workspace.integrated == w.integrated,
          "batch omitted integrated configuration");
    const auto published = publishXerBatch(batch, tmp.filePath("exchange"));
    check(JsonWorkspaceStore(files).load((published + "/workspace.ppmworkspace").toStdString()).workspace.links ==
              w.links,
          "published native links missing");

    for (auto &ref : w.projects) {
        ref.path = tmp.filePath(QString::fromStdString(ref.documentId) + ".ppm").toStdString();
        files.save(ref.path, ref.documentId == "P" ? p : q, {});
    }
    JsonWorkspaceStore store(files);
    const auto path = tmp.filePath("workspace.ppmworkspace").toStdString();
    store.save(path, w, {});
    check(store.load(path).workspace == w, "workspace2 roundtrip");
    QFile f(QString::fromStdString(path));
    check(f.open(QIODevice::ReadOnly), "open workspace");
    const auto bytes = f.readAll();
    check(QJsonDocument::fromJson(bytes).object()["schemaVersion"] == 2, "workspace version not2");
    f.close();
    QDir dir(tmp.path());
    check(dir.mkdir("moved"), "mkdir");
    for (const auto &ref : w.projects)
        check(QFile::copy(QString::fromStdString(ref.path),
                          tmp.filePath("moved/" + QString::fromStdString(ref.documentId) + ".ppm")),
              "copy source");
    check(QFile::copy(QString::fromStdString(path), tmp.filePath("moved/workspace.ppmworkspace")), "copy workspace");
    auto moved = store.load(tmp.filePath("moved/workspace.ppmworkspace").toStdString()).workspace;
    check(moved.links == w.links && moved.integrated == w.integrated &&
              moved.projects[0].path.find("moved") != std::string::npos,
          "workspace2 relocation");
    auto r = calculateIntegratedWorkspace(w, snapshots(p, q));
    check(r.status == CalculationStatus::Valid, "export fixture calculation");
    for (const auto &ref : w.projects)
        check(files.load(ref.path).project == (ref.documentId == "P" ? p : q), "source file changed");
    write("integrated-workspace.json", integratedWorkspaceJson(w, r, 42));
    auto report = integratedWorkspaceReport(w, r, 42, "2026-11-05T00:00:00Z");
    exportXlsx(report, "integrated-workspace.xlsx");
    exportPdf(report, "integrated-workspace.pdf");
    auto out = w;
    for (auto &ref : out.projects) {
        ref.path = QDir::current().filePath(QString::fromStdString(ref.documentId) + "-integrated.ppm").toStdString();
        files.save(ref.path, ref.documentId == "P" ? p : q, {});
    }
    store.save(QDir::current().filePath("integrated.ppmworkspace").toStdString(), out, {});
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    try {
        QDir().mkpath("integrated-workspace-qa");
        check(QDir::setCurrent("integrated-workspace-qa"), "output directory");
        QFile::remove("integrated.ppmworkspace");
        QFile::remove("integrated-workspace.xlsx");
        QFile::remove("integrated-workspace.pdf");

        QFile::remove("P-integrated.ppm");
        QFile::remove("Q-integrated.ppm");
        contracts();
        advanced();
        persistence();
        std::cout << "Integrated workspace: qualified links, FS/SS/FF/SF signed lag, cycle, release/status/calendar "
                     "safety, shared leveling, workspace2 relocation, immutable source and exports passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
