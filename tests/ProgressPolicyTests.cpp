#include "ProgressPolicyFixture.h"
#include "formatting/QtDiagnosticText.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/Tracking.h"
#include "persistence/ProgressPolicyJson.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <chrono>
#include <algorithm>
#include <iostream>
using namespace jobs;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly), "write");
    check(f.write(b) == b.size(), "short write");
}
const ProgressPolicyTask &task(const ProgressPolicyComparison &r, const std::string &id) {
    for (const auto &t : r.tasks)
        if (t.id == id)
            return t;
    throw std::runtime_error("missing row");
}
void contracts() {
    for (const std::string profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        auto p = progress_fixture::small(profile);
        p.baselines.push_back(prepareBaseline(p, "승인", "tester", "original", "2026-11-03T00:00:00Z"));
        p.baselines.back().id = "baseline-1";
        p.baselines.back().version = 1;
        ++p.revision;
        const auto before = p;
        const auto r = compareProgressPolicies(p);
        if (!r.computed || r.retained.status != CalculationStatus::Valid ||
            r.overridden.status != CalculationStatus::Valid) {
            std::cerr << profile << " " << r.blockingReason << "\n";
            for (const auto *lane : {&r.originalPlan, &r.retained, &r.overridden})
                for (const auto &d : lane->diagnostics)
                    std::cerr << qDiagnosticText(d).toStdString() << "\n";
        }
        check(r.computed && r.retained.status == CalculationStatus::Valid &&
                  r.overridden.status == CalculationStatus::Valid,
              "comparison invalid");
        check(r.ignored.size() == 1 && r.ignored[0].predecessorId == "A" && r.ignored[0].taskId == "B",
              "active relation mask wrong");
        check(task(r, "B").retained->remainingStart == "2026-11-04 09:00" &&
                  task(r, "B").retained->finish == "2026-11-04 14:00" &&
                  task(r, "B").overridden->remainingStart == "2026-11-03 09:00" &&
                  task(r, "B").overridden->finish == "2026-11-03 14:00",
              "hand B dates wrong");
        write("progress-policy-qa/" + QString::fromStdString(profile) + ".json", progressPolicyJson(r));
        check(r.retained.projectFinish == "2026-11-05 14:00" && r.overridden.projectFinish == "2026-11-04 14:00" &&
                  task(r, "C").finishDeltaMinutes == -480,
              "hand downstream dates wrong");
        check(task(r, "A").overridden->totalFloatMinutes == 240 && task(r, "B").overridden->totalFloatMinutes == 0 &&
                  task(r, "D").overridden->totalFloatMinutes == 240,
              "independent override float wrong");
        check(task(r, "B").overridden->start == "2026-11-02 14:00" && task(r, "E").overridden->completedActual &&
                  task(r, "E").overridden->finish == "2026-11-02 18:00",
              "actual facts changed");
        check(task(r, "B").overridden->drivingPredecessors.empty() &&
                  task(r, "C").overridden->drivingPredecessors == std::vector<std::string>{"B"},
              "inactive edge remained driving");
        check(r.overridden.plannedTasks == r.originalPlan.tasks && p == before, "source plan/baseline mutated");
        check(std::any_of(r.overridden.diagnostics.begin(), r.overridden.diagnostics.end(),
                          [](const auto &d) { return d.code == "OUT_OF_SEQUENCE"; }),
              "source out of sequence evidence lost");
        auto json = QJsonDocument::fromJson(progressPolicyJson(r)).object();
        check(json["tasks"].toArray()[4].toObject()["overridden"].toObject()["totalFloatMinutes"].isNull(),
              "completed actual float encoded as zero");
    }
    for (auto type : {DependencyType::FS, DependencyType::SS, DependencyType::FF, DependencyType::SF})
        for (int lag : {-60, 60}) {
            auto p = progress_fixture::small(pdmProfile);
            p.tasks[1].dependencyRules = {{"A", type, lag}};
            const auto r = compareProgressPolicies(p);
            check(r.computed && r.overridden.status == CalculationStatus::Valid && r.ignored[0].type == type &&
                      r.ignored[0].lagMinutes == lag && task(r, "B").overridden->finish == "2026-11-03 14:00",
                  "PDM mask/lag semantics wrong");
        }
    auto p = progress_fixture::small(effortProfile);
    Resource resource;
    resource.id = "R";
    resource.name = "작업자";
    resource.type = "labor";
    resource.maxUnits = 100;
    p.resources.push_back(resource);
    p.tasks[1].taskType = TaskType::FixedWork;
    p.tasks[1].workDriven = true;
    p.tasks[1].workUnitMinutes = 48000;
    p.assignments = {{"B", "R", 100}};
    p.progress[1].remainingWorkUnitMinutes = 24000;
    auto r = compareProgressPolicies(p);
    check(r.computed && r.overridden.status == CalculationStatus::Valid &&
              task(r, "B").overridden->finish == "2026-11-03 14:00",
          "remaining effort not reused");
    p.tasks[1].breaks = {{"2026-11-03 10:00", "2026-11-03 11:00"}};
    r = compareProgressPolicies(p);
    check(task(r, "B").overridden->finish == "2026-11-03 15:00", "manual break lost");
    p.calendars = {{"holiday", "휴일", {1, 2, 3, 4, 5}, {{540, 720}, {780, 1080}}, {"2026-11-03"}}};
    p.tasks[1].calendarId = "holiday";
    r = compareProgressPolicies(p);
    check(task(r, "B").overridden->finish == "2026-11-04 14:00", "task calendar lost");
    p = progress_fixture::small(constraintProfile);
    p.tasks[0].durationMinutes = 480;
    p.progress[0].planDurationMinutes = 480;
    p.progress[0].actualDurationMinutes = 120;
    p.progress[0].remainingMinutes = 960;
    p.tasks[1].constraintType = ConstraintType::FNLT;
    p.tasks[1].constraintAt = "2026-11-04 18:00";
    r = compareProgressPolicies(p);
    check(r.computed && r.retained.status == CalculationStatus::Conflict &&
              r.overridden.status == CalculationStatus::Valid && !task(r, "B").retained &&
              !task(r, "B").finishDeltaMinutes,
          "conflict silently became normal comparison");
    p = progress_fixture::small();
    p.statusAt.reset();
    check(!compareProgressPolicies(p).computed, "no status accepted");
    p = progress_fixture::small();
    p.remainingLeveling = LevelingSelection{};
    check(!compareProgressPolicies(p).computed, "adopted leveling silently ignored");
    p = progress_fixture::small();
    p.tasks[0].predecessors = {"C"};
    check(!compareProgressPolicies(p).computed, "ignored edges hid original cycle");
    p = progress_fixture::small();
    p.progress.erase(p.progress.begin() + 1);
    r = compareProgressPolicies(p);
    check(r.ignored.empty() && task(r, "B").retained->finish == task(r, "B").overridden->finish,
          "unreported task relation suppressed");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        compareProgressPolicies(p, control);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation ignored");
    auto output = compareProgressPolicies(progress_fixture::small());
    write("progress-policy-qa/comparison.json", progressPolicyJson(output, "2026-11-03T00:00:00Z"));
}
void kit() {
    auto p = progress_fixture::small();
    const auto comparison = compareProgressPolicies(p);
    JsonProjectStore store;
    QTemporaryDir tmp;
    store.save((tmp.path() + "/progress.ppm").toStdString(), p, {});
    QFile native(tmp.path() + "/progress.ppm");
    check(native.open(QIODevice::ReadOnly), "native fixture");
    QDir().mkpath("progress-policy-qa/p6-kit");
    write("progress-policy-qa/p6-kit/progress-native.ppm", native.readAll());
    p.progress.clear();
    p.statusAt.reset();
    auto e = exportPrimaveraXer(p);
    write("progress-policy-qa/p6-kit/plan-input.xer", e.xer);
    QJsonArray expected;
    const auto plan = calculatePlan(p);
    const auto map = e.report["mapping"].toArray();
    for (const auto &t : plan.tasks) {
        QString code;
        for (const auto &m : map) {
            const auto o = m.toObject();
            if (o["kind"] == "task" && o["jobsId"] == QString::fromStdString(t.taskId))
                code = o["activityCode"].toString();
        }
        check(!code.isEmpty(), "export activity mapping missing");
        expected.append(QJsonObject{{"taskId", QString::fromStdString(t.taskId)},
                                    {"activityCode", code},
                                    {"start", QString::fromStdString(t.start)},
                                    {"finish", QString::fromStdString(t.finish)},
                                    {"totalFloatMinutes", qint64(t.totalFloatMinutes)},
                                    {"freeFloatMinutes", qint64(t.freeFloatMinutes)}});
    }
    write("progress-policy-qa/p6-kit/expected-plan.json",
          QJsonDocument(
              QJsonObject{{"contract", "ppm-p6-plan-roundtrip-kit-v1"},
                          {"inputSha256",
                           QString::fromLatin1(QCryptographicHash::hash(e.xer, QCryptographicHash::Sha256).toHex())},
                          {"expected", expected},
                          {"executionStatus", "not-run"},
                          {"reason", "P6 not installed; user confirmed"}})
              .toJson());
    write("progress-policy-qa/p6-kit/expected-native-comparison.json",
          progressPolicyJson(comparison, "2026-11-03T00:00:00Z"));
}
void benchmark() {
    QJsonArray metrics;
    for (int n : {1000, 10000}) {
        const auto p = progress_fixture::large(n);
        const auto before = p;
        const auto start = std::chrono::steady_clock::now();
        const auto r = compareProgressPolicies(p);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        check(r.computed && r.tasks.size() == std::size_t(n) && r.overridden.status == CalculationStatus::Valid &&
                  r.ignored.size() == 1 && p == before,
              "large comparison wrong");
        metrics.append(
            QJsonObject{{"tasks", n}, {"comparison_ms", ms}, {"ignored", 1}, {"changed", qint64(r.changedTasks)}});
    }
    write("progress-policy-qa/core-metrics.json", QJsonDocument(metrics).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QDir().mkpath("progress-policy-qa");
        contracts();
        kit();
        benchmark();
        std::cout << "progress policy native hand "
                     "dates/float/profiles/effort/constraints/mask/actuals/readonly/kit/scale passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
