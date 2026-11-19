#include "persistence/ProjectMpp.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "application/Document.h"
#include "calculation/Effort.h"
#include "domain/Classification.h"
#include <QCoreApplication>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
void write(const QString &name, const QByteArray &bytes) {
    QFile f("mpp-curves-qa/" + name);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
void run() {
    QDir().mkpath("mpp-curves-qa");
    QTemporaryDir temp;
    JsonProjectStore store;
    for (const auto &name : QStringList{"timephased-multi-rate.mpp"}) {
        std::cout << "READ " << name.toStdString() << std::endl;
        const auto source = readProjectMpp(QString(PPM_MPP_FIXTURES) + "/" + name);
        auto e = importProjectMpp(source, name);
        check(e.project.tasks.size() == 12 && e.project.assignments.size() == 12, "real MPP shape");
        check(e.project.progress.empty() && e.project.periodActuals.empty() && e.project.costEntries.empty() &&
                  e.project.baselines.empty() && !e.project.statusAt,
              "inferred observations");
        check(e.report["reader"].toObject()["protocol"] == 2, "projection protocol");
        check(e.report["planningCurves"].toObject()["rows"].toInt() >= 13, "raw curves not retained");
        check(e.readerXml.contains("<TimephasedData>") && e.readerXml.contains("<Type>1</Type>") &&
                  !e.readerXml.contains("<Type>2</Type>"),
              "source regular work only");
        auto plan = calculatePlan(e.project);
        check(plan.status == CalculationStatus::Valid, "curved native plan");
        QJsonArray spans;
        for (const auto &a : e.project.assignments) {
            auto task = std::find_if(e.project.tasks.begin(), e.project.tasks.end(),
                                     [&](const auto &t) { return t.id == a.taskId; });
            auto row =
                std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &r) { return r.taskId == a.taskId; });
            std::int64_t total = 0;
            QJsonArray segments;
            for (const auto &s : TaskExecution(e.project, *task).spans(*row))
                if (s.resourceId == a.resourceId) {
                    total += (s.finish - s.start) * s.units;
                    segments.append(
                        QJsonObject{{"start", qint64(s.start)}, {"finish", qint64(s.finish)}, {"units", s.units}});
                }
            if ((a.taskId == "xml-task-14" || a.taskId == "xml-task-16")) {
                check(a.nativeCurve && a.nativeCurve->mspCode == 8 && total == 96000, "manual split16h curve");
                check(a.periods.size() >= 3, "manual pattern flattened");
            }
            spans.append(QJsonObject{{"task", QString::fromStdString(a.taskId)},
                                     {"resource", QString::fromStdString(a.resourceId)},
                                     {"work", qint64(total)},
                                     {"start", QString::fromStdString(row->start)},
                                     {"finish", QString::fromStdString(row->finish)},
                                     {"spans", segments}});
        }
        check(e.project.xmlWbs && !e.project.xmlWbs->values.empty() && e.readerXml.contains("<WBS>") &&
                  nativeSchemaFor(e.project) == 41,
              "MPP curve and source WBS codes require schema41");
        auto curvesOnly = e.project;
        curvesOnly.xmlWbs.reset();
        check(nativeSchemaFor(curvesOnly) == 36, "native curve metadata keeps minimum schema36 without WBS codes");
        const auto folder = publishProjectMpp(e, temp.path() + "/" + name);
        check(store.load((folder + "/project.ppm").toStdString()).project == e.project, "native save/reopen");
        QFile original(folder + "/original.mpp");
        check(original.open(QIODevice::ReadOnly) && original.readAll() == source, "original changed");
        Document d(store);
        d.open((folder + "/project.ppm").toStdString());
        auto before = *d.project();
        d.renameTask(0, "검토 후 이름");
        d.undo();
        check(d.project()->assignments == before.assignments, "undo lost source curves");
        JsonRecoveryStore recovery(temp.path() + "/recovery-" + name);
        auto session = recovery.newSession();
        d.renameTask(0, "복구");
        session->checkpoint(d.recoverySnapshot("2026-11-09T04:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "curve recovery");
        write(name + ".json", QJsonDocument(json::encodeProject(e.project)).toJson());
        write(name + ".report.json", QJsonDocument(e.report).toJson());
        write(name + ".reader.xml", e.readerXml);
        write(name + ".spans.json", QJsonDocument(spans).toJson());
        if (name == "timephased-multi-rate.mpp") {
            write("export.xml", exportProjectXml(e.project).xml);
            write("export.xer", exportPrimaveraXer(e.project).xer);
        }
    }
    for (const auto &name : QStringList{"timephased-actual-work-resource.mpp", "timephased-actual-overtime-work.mpp"}) {
        auto e = importProjectMpp(readProjectMpp(QString(PPM_MPP_FIXTURES) + "/" + name), name);
        check(e.project.progress.empty() && e.project.periodActuals.empty() && e.project.costEntries.empty() &&
                  e.project.baselines.empty() && !e.project.statusAt,
              "observed/OT data inferred");
        check(e.report["planningCurves"].toObject()["excludedActual"].toInt() > 0, "actual exclusion disclosure");
        check(!e.readerXml.contains("<Type>2</Type>") && !e.readerXml.contains("<Type>3</Type>"),
              "actual/OT timephases emitted");
        write(name + ".json", QJsonDocument(json::encodeProject(e.project)).toJson());
        write(name + ".report.json", QJsonDocument(e.report).toJson());
    }
    for (const auto &name :
         QStringList{"mpp9timephasedsegments.mpp", "mpp12timephasedsegments.mpp", "mpp14timephasedsegments.mpp"}) {
        bool rejected = false;
        try {
            importProjectMpp(readProjectMpp(QString(PPM_MPP_FIXTURES) + "/" + name), name);
        } catch (const AppError &e) {
            rejected = e.code() == "FILE_PROJECTXML_021";
        }
        check(rejected, "one-off working calendar exception silently lost");
    }
    std::cout << "PASS real MPP manual source curves, work conservation, no inferred actuals, "
                 "original/schema36/save/undo/recovery and XML/XER export\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        run();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &s : e.diagnostic().arguments)
            std::cerr << " " << s;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
