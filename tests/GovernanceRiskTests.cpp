#include "calculation/ScheduleRisk.h"
#include "calculation/WorkCalendar.h"
#include "persistence/Automation.h"
#include "persistence/GovernanceJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include "persistence/WorkspaceFile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read");
    return f.readAll();
}
Project project() {
    Project p;
    p.documentId = "P";
    p.name = "위험 시험";
    p.startDate = "2026-11-02";
    p.tasks = {{"A", "설계", 480, {}}, {"B", "구현", 480, {"A"}}};
    return p;
}
Workspace workspace() {
    Workspace w;
    w.id = "W";
    w.projects = {{"P", "P.ppm", "위험 시험"}, {"Q", "Q.ppm", "대기 프로젝트"}};
    auto &g = w.governance;
    g.organizations = {{"obs", "개발", "", "OBS"}, {"eps", "제품", "", "EPS"}};
    g.members = {{"O", "소유자", "obs"}, {"E", "편집자", "obs"}, {"R", "검토자", "obs"}, {"V", "조회자", "obs"}};
    g.grants = {{"O", "", "owner"}, {"E", "P", "editor"}, {"R", "P", "reviewer"}, {"V", "P", "viewer"}};
    g.portfolio = {{"P", "eps", "O", "제품군", "KRW", 5000, 70}, {"Q", "eps", "O", "제품군", "USD", 200, 30}};
    g.risks = {{"risk", "P", "A", "설계 변동", "O", "open", "", 480, 960, 1440, 10000}};
    return w;
}
QByteArray request(const Project &p, const QJsonArray &operations) {
    return QJsonDocument(QJsonObject{{"format", "PPM-AUTOMATION-1"},
                                     {"documentId", QString::fromStdString(p.documentId)},
                                     {"revision", qint64(p.revision)},
                                     {"requestId", "run-1"},
                                     {"operations", operations}})
        .toJson();
}
void governance() {
    auto w = workspace();
    validateWorkspace(w);
    check(permits(w, "O", "Q", WorkspaceAction::Administer), "owner");
    check(permits(w, "E", "P", WorkspaceAction::Edit) && !permits(w, "E", "Q", WorkspaceAction::Edit),
          "qualified edit scope");
    check(!permits(w, "V", "P", WorkspaceAction::Edit) && permits(w, "V", "P", WorkspaceAction::View), "viewer");
    check(permits(w, "R", "P", WorkspaceAction::Approve) && !permits(w, "E", "P", WorkspaceAction::Approve),
          "reviewer");
    check(!permits(w, "unknown", "P", WorkspaceAction::View), "unknown member default deny");
    auto bad = w;
    bad.governance.organizations[0].parentId = "obs";
    rejects([&] { validateWorkspace(bad); });
    bad = w;
    bad.governance.grants.erase(bad.governance.grants.begin());
    rejects([&] { validateWorkspace(bad); });
    bad = w;
    bad.governance.portfolio[0].epsId = "obs";
    rejects([&] { validateWorkspace(bad); });
    bad = w;
    bad.governance.risks[0].probability = 10001;
    rejects([&] { validateWorkspace(bad); });
    appendCollaboration(w, {"req", "P", "E", "request", "설계 검토", "2026-11-05 10:00", "", 5});
    const auto before = w;
    rejects([&] { appendCollaboration(w, {"no", "P", "E", "approve", "승인", "2026-11-05 10:01", "req", 5}); });
    check(w == before, "failed approval mutated history");
    rejects([&] { appendCollaboration(w, {"stale", "P", "R", "approve", "승인", "2026-11-05 10:01", "req", 6}); });
    appendCollaboration(w, {"ok", "P", "R", "approve", "승인", "2026-11-05 10:01", "req", 5});
    rejects([&] { appendCollaboration(w, {"twice", "P", "R", "reject", "반려", "2026-11-05 10:02", "req", 5}); });
    check(decodeGovernance(encodeGovernance(w.governance)) == w.governance, "governance codec");
    auto json = encodeGovernance(w.governance);
    json["surprise"] = true;
    rejects([&] { decodeGovernance(json); });
    QTemporaryDir tmp;
    JsonProjectStore files;
    JsonWorkspaceStore store(files);
    for (auto &ref : w.projects)
        ref.path = tmp.filePath(QString::fromStdString(ref.documentId + ".ppm")).toStdString();
    const auto path = tmp.filePath("governance.ppmworkspace");
    const auto fp = store.save(path.toStdString(), w, {});
    check(store.load(path.toStdString()).workspace == w, "schema3 roundtrip");
    check(QJsonDocument::fromJson(read(path)).object()["schemaVersion"] == 3, "schema3 version");
    write(path, read(path) + " ");
    rejects([&] { store.save(path.toStdString(), w, fp); });
    auto root = QJsonDocument::fromJson(read(path)).object();
    auto payload = root["payload"].toObject();
    auto gov = payload["governance"].toObject();
    auto entries = gov["risks"].toArray();
    auto entry = entries[0].toObject();
    entry["probability"] = 1.5;
    entries[0] = entry;
    gov["risks"] = entries;
    payload["governance"] = gov;
    root["payload"] = payload;
    root["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
    rejects([&] { decodeWorkspaceFile(QJsonDocument(root).toJson(), tmp.path()); });
    QDir().mkpath("governance-qa");
    w.projects[0].path = QDir("governance-qa").absoluteFilePath("P.ppm").toStdString();
    w.projects[1].path = QDir("governance-qa").absoluteFilePath("Q.ppm").toStdString();
    const auto evidence = QDir("governance-qa").absoluteFilePath("governance.ppmworkspace");
    if (QFile::exists(evidence))
        QFile::remove(evidence);
    store.save(evidence.toStdString(), w, {});
}
void risk() {
    const auto p = project();
    const auto source = p;
    auto risks = workspace().governance.risks;
    RiskOptions o{12345, 2000, "2026-11-04 18:00"};
    auto r = analyzeScheduleRisk(p, risks, o);
    check(r.status == CalculationStatus::Valid && r.iterations == 2000 && p == source, "risk mutated source");
    const auto again = analyzeScheduleRisk(p, risks, o);
    check(r.finishQuantiles == again.finishQuantiles && r.criticalCounts == again.criticalCounts,
          "seed reproducibility");
    WorkCalendar cal(p);
    for (const auto &[percent, finish] : r.finishQuantiles) {
        const double u = percent / 100.;
        const auto analytic = 480 + (u < .5 ? 480 + std::sqrt(u * 960 * 480) : 1440 - std::sqrt((1 - u) * 960 * 480));
        check(std::abs(double(cal.offset(finish)) - analytic) < 50, "triangular analytic quantile");
    }
    check(r.deadlineHits > 800 && r.deadlineHits < 1200, "deadline probability");
    check(r.criticalCounts.at("A") == 2000 && r.criticalCounts.at("B") == 2000, "critical chain frequency");
    check(r.medianDriftMinutes < 70, "half-sample stability");
    risks[0].probability = 0;
    const auto none = analyzeScheduleRisk(p, risks, {1, 20, {}});
    check(none.finishQuantiles.at(90) == *calculatePlan(p).projectFinish, "zero probability baseline");
    risks[0].probability = 10000;
    risks[0].optimistic = risks[0].likely = risks[0].pessimistic = 960;
    const auto fixed = analyzeScheduleRisk(p, risks, {1, 20, {}});
    check(cal.offset(fixed.finishQuantiles.at(10)) == 1440 &&
              fixed.finishQuantiles.at(10) == fixed.finishQuantiles.at(90),
          "degenerate exact CPM");
    auto first = workspace().governance.risks[0];
    first.group = "weather";
    auto second = first;
    second.id = "risk2";
    second.taskId = "B";
    auto correlated = analyzeScheduleRisk(p, {first, second}, {55, 1000, {}});
    first.group.clear();
    second.group.clear();
    auto independent = analyzeScheduleRisk(p, {first, second}, {55, 1000, {}});
    check(cal.offset(correlated.finishQuantiles.at(90)) > cal.offset(independent.finishQuantiles.at(90)) + 70,
          "shared ranks affect tail");
    const auto reversed = analyzeScheduleRisk(p, {second, first}, {55, 1000, {}});
    check(reversed.finishQuantiles == independent.finishQuantiles, "risk list order changed seed");
    auto other = first;
    other.projectId = "Q";
    check(analyzeScheduleRisk(p, {other}, {1, 20, {}}).finishQuantiles == none.finishQuantiles,
          "same task ID in another project affected risk schedule");
    auto constrained = p;
    constrained.calculationProfile = constraintProfile;
    constrained.tasks[0].constraintType = ConstraintType::MFO;
    constrained.tasks[0].constraintAt = "2026-11-02 18:00";
    check(calculatePlan(constrained).status == CalculationStatus::Valid, "constraint source fixture");
    auto conflicting = workspace().governance.risks;
    conflicting[0].optimistic = conflicting[0].likely = conflicting[0].pessimistic = 960;
    rejects([&] { analyzeScheduleRisk(constrained, conflicting, {1, 20, {}}); });
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    const auto cancelled = analyzeScheduleRisk(p, {}, {1, 10, {}}, control);
    check(cancelled.status == CalculationStatus::Cancelled && cancelled.finishQuantiles.empty(),
          "cancel published percentiles");
    rejects([&] { analyzeScheduleRisk(p, {first, first}, {1, 10, {}}); });
    first.taskId = "missing";
    rejects([&] { analyzeScheduleRisk(p, {first}, {1, 10, {}}); });
    rejects([&] { analyzeScheduleRisk(p, {}, {1, 1, {}}); });
    auto work = p;
    work.tasks[0].workDriven = true;
    rejects([&] { analyzeScheduleRisk(work, {}, {1, 10, {}}); });
    auto closed = workspace().governance.risks;
    closed[0].state = "closed";
    check(analyzeScheduleRisk(p, closed, {1, 10, {}}).finishQuantiles == none.finishQuantiles, "closed risk sampled");
    QJsonObject evidence{{"seed", qint64(r.seed)},
                         {"iterations", r.iterations},
                         {"baselineFinish", QString::fromStdString(r.baselineFinish)},
                         {"deadlineHits", r.deadlineHits},
                         {"medianDriftMinutes", qint64(r.medianDriftMinutes)}};
    for (const auto &[percent, finish] : r.finishQuantiles)
        evidence[QString("P%1").arg(percent)] = QString::fromStdString(finish);
    write("governance-qa/risk-result.json", QJsonDocument(evidence).toJson());
    auto w = workspace();
    const auto summary = summarizePortfolio(w, {std::make_shared<const Project>(p)});
    check(summary.size() == 2 && summary[0].currency == "KRW" && summary[0].loaded == 1 &&
              summary[0].budgetMinor == 5000 && summary[1].currency == "USD" && summary[1].loaded == 0 &&
              !summary[1].finish,
          "portfolio currency and missing coverage");
}
void automation() {
    JsonProjectStore files;
    Document d(files);
    d.create("자동화", "2026-11-02", {"A", "B"});
    d.setDuration(0, 480);
    d.setDuration(1, 480);
    const auto before = *d.project();
    const QJsonArray operations{
        QJsonObject{{"taskId", QString::fromStdString(before.tasks[0].id)}, {"name", "변경"}, {"durationMinutes", 960}},
        QJsonObject{{"taskId", QString::fromStdString(before.tasks[1].id)}, {"durationMinutes", 120}}};
    const auto bytes = request(before, operations);
    auto preview = previewAutomation(d, bytes);
    check(*d.project() == before && preview.receipt["state"] == "preview", "preview mutated input");
    auto receipt = applyAutomation(d, preview);
    check(d.project()->revision == before.revision + 1 && d.project()->tasks[0].name == "변경" &&
              *d.project()->tasks[1].durationMinutes == 120,
          "batch not atomic revision");
    rejects([&] { applyAutomation(d, preview); });
    d.undo();
    check(d.project()->tasks == before.tasks, "automation undo");
    d.redo();
    const auto after = *d.project();
    rejects([&] { previewAutomation(d, bytes); });
    check(*d.project() == after, "stale request changed input");
    auto badOps = operations;
    badOps.append(QJsonObject{{"taskId", "missing"}, {"name", "no"}});
    rejects([&] { previewAutomation(d, request(*d.project(), badOps)); });
    check(*d.project() == after, "invalid last operation partially committed");
    auto stale = previewAutomation(d, request(*d.project(), operations));
    d.renameTask(1, "newer");
    const auto newer = *d.project();
    rejects([&] { applyAutomation(d, stale); });
    check(*d.project() == newer, "stale prepared automation applied");
    auto revoked = previewAutomation(d, request(*d.project(), operations));
    d.setWriteAuthorization([] { return false; });
    check(d.readOnly() && !d.canUndo(), "read only policy bypass");
    rejects([&] { d.renameTask(0, "blocked"); });
    rejects([&] { applyAutomation(d, revoked); });
    check(*d.project() == newer, "role revoke changed input");
    d.setWriteAuthorization({});
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    rejects([&] { previewAutomation(d, request(*d.project(), operations), control); });
    control = std::make_shared<CalculationControl>();
    auto prepared = previewAutomation(d, request(*d.project(), operations), control);
    control->cancel();
    rejects([&] { applyAutomation(d, prepared); });
    check(*d.project() == newer, "cancelled preview changed input");
    auto malformed = QJsonDocument::fromJson(request(*d.project(), operations)).object();
    malformed["revision"] = 1.5;
    rejects([&] { previewAutomation(d, QJsonDocument(malformed).toJson()); });
    auto protect = QJsonDocument::fromJson(request(*d.project(), operations)).object();
    protect["progress"] = QJsonArray{};
    rejects([&] { previewAutomation(d, QJsonDocument(protect).toJson()); });
    const auto path = QDir("governance-qa").absoluteFilePath("automation.ppm");
    if (QFile::exists(path))
        QFile::remove(path);
    d.saveAs(path.toStdString());
    write("governance-qa/automation-request.json", request(*d.project(), operations));
    write("governance-qa/receipt.json", QJsonDocument(receipt).toJson());
}
void cli() {
    QTemporaryDir temp;
    JsonProjectStore files;
    auto p = project();
    const auto path = temp.filePath("cli.ppm");
    files.save(path.toStdString(), p, {});
    const auto req = temp.filePath("request.json");
    write(req, request(p, {QJsonObject{{"taskId", "A"}, {"name", "CLI"}}}));
    const auto original = read(path);
    const auto run = [&](const QStringList &extra, bool success) {
        QProcess process;
        process.start(QDir(QCoreApplication::applicationDirPath()).filePath("PPM-Automation"),
                      QStringList{"--project", path} + extra);
        check(process.waitForFinished(30000), "CLI process timeout");
        check((process.exitCode() == 0) == success, process.readAllStandardError().constData());
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    };
    check(run({}, true)["revision"] == 1, "inspect CLI");
    check(run({"--request", req}, true)["state"] == "preview" && read(path) == original, "CLI preview wrote source");
    check(run({"--request", req, "--apply"}, true)["saved"] == true, "CLI atomic save");
    const auto applied = read(path);
    check(files.load(path.toStdString()).project.tasks[0].name == "CLI", "CLI saved batch");
    run({"--request", req, "--apply"}, false);
    check(read(path) == applied, "CLI replay changed file");
    auto policy = workspace();
    policy.projects[0].path = path.toStdString();
    policy.projects[1].path = temp.filePath("q.ppm").toStdString();
    const auto policyPath = temp.filePath("policy.ppmworkspace");
    JsonWorkspaceStore(files).save(policyPath.toStdString(), policy, {});
    write(req, request(files.load(path.toStdString()).project, {QJsonObject{{"taskId", "A"}, {"name", "denied"}}}));
    run({"--request", req, "--apply", "--workspace", policyPath, "--actor", "V"}, false);
    check(read(path) == applied, "CLI viewer wrote file");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        governance();
        risk();
        automation();
        cli();
        std::cout << "governance/risk/automation contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
