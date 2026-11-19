#include "calculation/SharedAvailabilityQuery.h"
#include "calculation/SharedResources.h"
#include "domain/Classification.h"
#include "domain/NativeSchema.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/SharedAvailabilityExport.h"
#include "reporting/ReportExport.h"
#include "ui/DesktopWorkspace.h"
#include "ui/WorkspaceDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <tuple>
#include <cstdlib>
namespace {
using namespace ppm;
void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char *why) {
    bool bad = false;
    try {
        f();
    } catch (const AppError &) {
        bad = true;
    }
    check(bad, why);
}
void save(const QString &path, const QByteArray &data) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size(), "save artifact");
}
Project project(std::string id) {
    Project p;
    p.documentId = id;
    p.name = "프로젝트 " + id;
    p.revision = id == "P" ? 3 : 7;
    p.startDate = "2026-11-16";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "같은 작업 ID", id == "P" ? 120 : 60, {}}};
    if (id == "Q") {
        p.tasks[0].constraintType = ConstraintType::SNET;
        p.tasks[0].constraintAt = "2026-11-16 09:30";
    }
    p.resources = {{"R", "같은 자원 이름", "labor", id == "P" ? 75000 : 10, {}}};
    p.assignments = {{"T", "R", id == "P" ? 75 : 50}};
    if (id == "P") {
        p.resources.push_back({"R2", "같은 자원 이름", "labor", 0, {}});
        p.assignments.push_back({"T", "R2", 25});
        p.resources.push_back({"Z", "미연결", "labor", 100, {}});
    }
    validate(p);
    return p;
}
Workspace config() {
    Workspace w;
    w.id = "shared-remaining";
    w.projects = {{"P", "", "프로젝트 P"}, {"Q", "", "프로젝트 Q"}};
    w.resources = {{"main", "공유 시공조", "labor", 100},
                   {"spare", "미배정 공유 풀", "labor", 200},
                   {"unknown", "미설정 공유 풀", "labor", {}}};
    w.bindings = {{"P", "R", "main"}, {"P", "R2", "main"}, {"Q", "R", "main"}};
    return w;
}
std::vector<std::shared_ptr<const Project>> sources(const Project &p, const Project &q) {
    return {std::make_shared<const Project>(p), std::make_shared<const Project>(q)};
}
void report(Project &p, const std::string &at, bool complete) {
    p.statusAt = at;
    ProgressEntry e;
    e.id = "entry-" + p.documentId;
    e.taskId = "T";
    e.statusAt = at;
    e.recordedAt = "2026-11-16T02:00:00Z";
    e.reporter = "PM";
    e.reason = "수요 기준";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = p.revision;
    e.planDurationMinutes = p.tasks[0].durationMinutes;
    e.actualStart = p.documentId == "P" ? "2026-11-16 09:00" : "2026-11-16 09:30";
    e.actualDurationMinutes = 60;
    e.remainingMinutes = complete ? 0 : 60;
    if (complete)
        e.actualFinish = at;
    p.progress = {e};
    validate(p);
}
void contract() {
    auto p = project("P"), q = project("Q");
    auto w = config();
    const auto beforeP = p, beforeQ = q;
    const auto beforeW = w;
    SharedAvailabilityQuery query(w, sources(p, q), false);
    for (const auto &[at, used, left] : std::vector<std::tuple<std::string, int, int>>{{"2026-11-16 09:00", 100, 0},
                                                                                       {"2026-11-16 09:30", 150, -50},
                                                                                       {"2026-11-16 10:30", 100, 0},
                                                                                       {"2026-11-16 11:00", 0, 100},
                                                                                       {"2026-11-16 12:00", 0, 0}}) {
        const auto point = query.at("main", at);
        check(point.value.demand == used && point.knownDemand == used && point.value.remaining == left,
              "pooled simultaneous demand and half-open endpoints");
        check(point.value.freeUnits == std::max(0, left) && point.value.shortage == std::max(0, -left),
              "signed remaining/free/shortage");
    }
    auto full = query.allAt("2026-11-16 09:45");
    check(full.unmappedResources == 1 && full.sources.size() == 2, "unmapped resources and provenance");
    check(full.points[0].contributions.size() == 2 && full.points[0].contributions[0].demand == 100 &&
              full.points[0].contributions[1].demand == 50,
          "multiple local resources combine under qualified project IDs");
    check(query.at("spare", "2026-11-16 09:45").value.freeUnits == 200, "unbound pool has zero mapped demand");
    check(query.at("unknown", "2026-11-16 09:45").value.demand == 0 &&
              !query.at("unknown", "2026-11-16 09:45").value.remaining,
          "unknown pool capacity is not zero");
    SharedAvailabilityQuery partial(w, {std::make_shared<const Project>(p)}, false);
    const auto missing = partial.at("main", "2026-11-16 09:45");
    check(missing.knownDemand == 100 && !missing.value.demand && !missing.value.remaining && !missing.value.freeUnits,
          "closed bound project blocks total and free capacity");
    check(partial.at("spare", "2026-11-16 09:45").value.freeUnits == 200,
          "unaffected unbound pool retains scoped values");
    auto invalid = q;
    invalid.tasks[0].durationMinutes.reset();
    SharedAvailabilityQuery incomplete(w, sources(p, invalid), false);
    check(!incomplete.at("main", "2026-11-16 09:45").value.remaining,
          "incomplete schedule is not treated as zero demand");
    auto wrong = p;
    wrong.resources[1].type = "equipment";
    SharedAvailabilityQuery wrongType(w, sources(wrong, q), false);
    check(wrongType.at("main", "2026-11-16 09:45").knownDemand == 125 &&
              !wrongType.at("main", "2026-11-16 09:45").value.demand,
          "invalid binding preserves known lower bound");
    auto large = w;
    large.resources[0].maxUnits = maxSharedResourceUnits;
    SharedAvailabilityQuery largePool(large, sources(p, q), false);
    check(largePool.at("main", "2026-11-16 09:45").value.remaining == maxSharedResourceUnits - 150,
          "shared ceiling exceeds local capacity ceiling");
    auto holiday = w;
    holiday.resources[0].calendar.holidays = {"2026-11-16"};
    SharedAvailabilityQuery closed(holiday, sources(p, q), false);
    check(closed.at("main", "2026-11-16 09:45").value.remaining == -150,
          "shared holiday restricts supply without discarding project demand");
    auto exception = w;
    exception.resources[0].exceptions = {{"2026-11-16", 50}};
    SharedAvailabilityQuery reduced(exception, sources(p, q), false);
    check(reduced.at("main", "2026-11-16 09:45").value.remaining == -100, "shared day exception applied once");
    auto future = q;
    future.startDate = "2026-11-17";
    future.tasks[0].constraintAt = "2026-11-17 09:30";
    SharedAvailabilityQuery differentStarts(w, sources(p, future), false);
    check(differentStarts.at("main", "2026-11-16 09:45").value.demand == 100,
          "different project anchors align by civil timestamp");
    auto fp = p, fq = q;
    report(fp, "2026-11-16 10:00", false);
    report(fq, "2026-11-16 10:30", true);
    SharedAvailabilityQuery forecast(w, sources(fp, fq), true);
    const auto between = forecast.at("main", "2026-11-16 10:15");
    check(between.knownDemand == 100 && !between.value.demand && !between.value.freeUnits,
          "different status cutoffs preserve unknown contribution");
    check(forecast.at("main", "2026-11-16 10:30").value.demand == 100 &&
              forecast.at("main", "2026-11-16 11:00").value.demand == 0,
          "forecast excludes completed work");
    SharedAvailabilityQuery mixed(w, sources(fp, q), true);
    check(mixed.at("main", "2026-11-16 09:45").knownDemand == 50 && !mixed.at("main", "2026-11-16 09:45").value.demand,
          "plan and forecast contributors keep their own cutoff");
    SharedAvailabilityQuery explicitPlan(w, sources(fp, fq), false);
    check(explicitPlan.at("main", "2026-11-16 09:45").value.demand == 150,
          "explicit plan is not historical actual demand");
    std::map<std::string, ScheduleResult> supplied{{"P", calculatePlan(p)}, {"Q", calculatePlan(q)}};
    auto stale = supplied;
    ++stale["Q"].inputRevision;
    rejects([&] { SharedAvailabilityQuery bad(w, sources(p, q), false, {}, &stale); },
            "stale supplied schedule rejected");
    auto partialRows = supplied;
    partialRows["Q"].tasks.clear();
    rejects([&] { SharedAvailabilityQuery bad(w, sources(p, q), false, {}, &partialRows); },
            "partial supplied schedule rejected");
    auto wrongMode = supplied;
    wrongMode["Q"] = calculate(fq);
    rejects([&] { SharedAvailabilityQuery bad(w, sources(p, fq), false, {}, &wrongMode); },
            "forecast result cannot masquerade as plan");
    auto duplicate = sources(p, q);
    duplicate.push_back(duplicate[0]);
    rejects([&] { SharedAvailabilityQuery bad(w, duplicate, false); }, "duplicate snapshots rejected");
    auto changed = w;
    changed.resources[0].maxUnits = 200;
    rejects([&] { sharedAvailabilityJson(changed, full, 5); }, "changed catalog cannot relabel old output");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        SharedAvailabilityQuery bad(w, sources(p, q), false, control);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "shared query cancellation");
    auto later = std::make_shared<CalculationControl>();
    SharedAvailabilityQuery stopped(w, sources(p, q), false, later);
    later->cancel();
    cancelled = false;
    try {
        stopped.allAt("2026-11-16 09:45");
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation after indexing");
    check(p == beforeP && q == beforeQ && w == beforeW && nativeSchemaFor(p) == nativeSchemaFor(beforeP),
          "query does not mutate input or upgrade storage");
    const auto dir = QDir::currentPath() + "/shared-availability-qa";
    QDir().mkpath(dir);
    save(dir + "/full.json", sharedAvailabilityJson(w, full, 5));
    save(dir + "/partial.json", sharedAvailabilityJson(w, partial.allAt("2026-11-16 09:45"), 5));
    save(dir + "/forecast.json", sharedAvailabilityJson(w, forecast.allAt("2026-11-16 10:15"), 5));
    save(dir + "/p.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(dir + "/q.json", QJsonDocument(json::encodeProject(q)).toJson());
    QByteArray vectors = "at\tcapacity\tdemand\tremaining\tfree\tshortage\n";
    for (int minute = 0; minute < 1440; ++minute) {
        char stamp[24];
        std::snprintf(stamp, sizeof(stamp), "2026-11-16 %02d:%02d", minute / 60, minute % 60);
        const auto v = query.at("main", stamp).value;
        const auto n = [](auto value) { return value ? QByteArray::number(*value) : QByteArray("?"); };
        vectors += QByteArray(stamp) + '\t' + n(v.capacity) + '\t' + n(v.demand) + '\t' + n(v.remaining) + '\t' +
                   n(v.freeUnits) + '\t' + n(v.shortage) + '\n';
    }
    save(dir + "/points.tsv", vectors);
    auto reportFull = sharedAvailabilityReport(w, full, 5, "2026-11-16T03:00:00Z");
    QFile::remove(dir + "/full.xlsx");
    QFile::remove(dir + "/full.pdf");
    exportXlsx(reportFull, dir + "/full.xlsx");
    check(exportPdf(reportFull, dir + "/full.pdf") > 0, "report exports");
    auto reportPartial = sharedAvailabilityReport(w, partial.allAt("2026-11-16 09:45"), 5, "2026-11-16T03:00:00Z");
    QFile::remove(dir + "/partial.xlsx");
    QFile::remove(dir + "/partial.pdf");
    exportXlsx(reportPartial, dir + "/partial.xlsx");
    check(exportPdf(reportPartial, dir + "/partial.pdf") > 0, "incomplete diagnostic exports");
    std::cout << "PASS shared remaining capacity/simultaneity/qualified bindings/missing files/lower bounds/mixed "
                 "cutoffs/stale schedules/calendar/exports\n";
}
void ui() {
    QTemporaryDir temp;
    auto files = std::make_shared<JsonProjectStore>();
    auto p = project("P"), q = project("Q");
    auto pp = temp.filePath("p.ppm").toStdString(), qp = temp.filePath("q.ppm").toStdString();
    files->save(pp, p, {});
    files->save(qp, q, {});
    DesktopWorkspace workspace(files, {});
    workspace.openProject(pp);
    workspace.openProject(qp);
    auto cfg = config();
    cfg.projects[0].path = pp;
    cfg.projects[1].path = qp;
    workspace.applyConfiguration(cfg);
    WorkspaceDialog parent(workspace);
    parent.show();
    QTest::qWait(80);
    int stage = 0;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "sharedAvailabilityDialog" || d->property("sharedAvailabilityBusy").toBool())
            return;
        auto *table = d->findChild<QTableWidget *>("sharedAvailabilityPoints");
        auto *date = d->findChild<QLineEdit *>("sharedAvailabilityAt");
        auto *run = d->findChild<QPushButton *>("calculateSharedAvailability");
        if (stage == 0 && table->rowCount() == 3) {
            check(table->item(0, 5)->text() == "0%", "initial pool point");
            stage = 1;
            date->setText("2026-11-16 10:15");
            run->click();
        } else if (stage == 1 && table->rowCount() == 3) {
            check(table->item(0, 3)->text() == "150%" && table->item(0, 5)->text() == "-50%",
                  "overlapping pooled point displayed");
            auto dir = QDir::currentPath() + "/shared-availability-qa";
            QDir().mkpath(dir);
            check(d->grab().save(dir + "/review.png"), "screenshot");
            stage = 2;
            workspace.document("Q")->setDuration(0, 30);
            run->click();
            check(table->rowCount() == 0, "fresh run clears prior snapshot");
        } else if (stage == 2 && table->rowCount() == 3) {
            check(table->item(0, 3)->text() == "100%" && table->item(0, 5)->text() == "0%",
                  "fresh generation survives pending timer notification");
            date->setText("bad");
            run->click();
            check(table->rowCount() == 0 && !d->findChild<QPushButton *>("exportSharedAvailabilityJSON")->isEnabled(),
                  "invalid query cannot export stale result");
            stage = 3;
            d->reject();
        }
    });
    timer.start();
    parent.findChild<QPushButton *>("workspaceRemainingAvailability")->click();
    timer.stop();
    parent.reject();
    check(stage == 3, "workspace query completed");
    std::cout << "PASS workspace shared remaining UI/pooled demand/revision invalidation/export guard\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "shared availability timeout\n";
        std::_Exit(2);
    });
    watchdog.start(60000);
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &a : error->diagnostic().arguments)
                std::cerr << ' ' << a;
        return 1;
    }
}
