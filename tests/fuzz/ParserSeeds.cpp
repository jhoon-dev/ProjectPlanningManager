#include "domain/MaterialActuals.h"
#include "domain/ResourcePlan.h"
#include "../XmlActualFixture.h"
#include "../XmlQuantityFixture.h"
#include "../XmlRemainingFixture.h"
#include "../XmlBaselineFixture.h"
#include "../XmlEarlyLateFixture.h"
#include "../XmlProgressFixture.h"
#include "../XmlPeriodActualFixture.h"
#include "../XmlPeriodFragmentFixture.h"
#include "../XmlActualExportFixture.h"
#include "../XmlWorkProgressFixture.h"
#include "../XmlProportionalFixture.h"
#include "../XmlAdditionalFunctionFixture.h"
#include "../XmlTimeZoneFixture.h"
#include "application/PeriodActualEditing.h"
#include "../XmlCalendarFixture.h"
#include "../XmlFormulaFixture.h"
#include "../XmlRollUpFixture.h"
#include "../XmlRollDownFixture.h"
#include "../XmlCodeFixture.h"
#include "ParserHarness.h"
#include "domain/Classification.h"
#include "domain/NativeSchema.h"
#include "domain/XmlBaselines.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "domain/PeriodActuals.h"
#include "application/Document.h"
#include "domain/XerScheduleAdoption.h"
#include "domain/XerFinancialAllocation.h"
#include "domain/ExternalEvm.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/GovernanceJson.h"
#include "domain/CalculationControl.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "calculation/ResourcePlanning.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::logic_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot read seed fixture");
    return file.readAll();
}
QByteArray envelope(const QJsonObject &body, int schema) {
    const auto digest =
        QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex();
    return QJsonDocument(QJsonObject{{"format", "JOBS"},
                                     {"schemaVersion", schema},
                                     {"payload", body},
                                     {"sha256", QString::fromLatin1(digest)}})
        .toJson(QJsonDocument::Compact);
}
struct Seed {
    QString parser, name;
    QByteArray bytes;
    bool valid;
};
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto fixtureRoot = QString::fromUtf8(JOBS_FIXTURES);
        std::vector<Seed> seeds;
        jobs::Project minimal;
        minimal.documentId = "parser-seed";
        minimal.name = "한글 seed";
        minimal.startDate = "2026-10-26";
        minimal.tasks = {{"T1", "Task", 60, {}}};
        for (int schema = 1; schema <= jobs::maximumNativeSchema; ++schema) {
            auto body = jobs::json::encodeProject(minimal);
            const auto remove = [&](int introduced, std::initializer_list<const char *> fields) {
                if (schema < introduced)
                    for (auto field : fields)
                        body.remove(QString::fromLatin1(field));
            };
            remove(2, {"holidays", "calculationProfile"});
            remove(3, {"wbsNodes"});
            remove(4, {"statusAt", "baselines", "progress"});
            remove(5, {"resources", "assignments", "scenarios"});
            remove(6, {"currency", "costProfile", "taskCosts", "costEntries"});
            auto task = body["tasks"].toArray()[0].toObject();
            if (schema < 2) {
                task.remove("durationMinutes");
                task.remove("predecessors");
            }
            if (schema < 3)
                task.remove("wbsId");
            body["tasks"] = QJsonArray{task};
            const auto bytes = envelope(body, schema);
            (void)jobs::decodeProjectFile(bytes);
            seeds.push_back({"jobs", QString("schema-%1.ppm").arg(schema), bytes, true});
            seeds.push_back({"jobs", QString("payload-%1.json").arg(schema), QJsonDocument(body).toJson(), true});
        }
        auto objects = minimal;
        objects.projectFields = {{"owner", "발주처", jobs::CustomFieldType::Text, {}, {}}};
        objects.resourceFields = {{"capacity", "정격", jobs::CustomFieldType::Number, "kW", {}}};
        objects.projectAttributes = {{"field:owner", "서부"}};
        jobs::Resource objectResource;
        objectResource.id = "resource-seed";
        objectResource.name = "장비";
        objectResource.attributes = {{"field:capacity", "12.5"}};
        objects.resources.push_back(objectResource);
        const auto objectBody = jobs::json::encodeProject(objects);
        check(jobs::decodeProjectFile(envelope(objectBody, 16)) == objects, "object field schema16 roundtrip");
        seeds.push_back({"jobs", "object-fields.ppm", envelope(objectBody, 16), true});
        auto wrongScope = objects;
        wrongScope.projectAttributes[0].key = "field:capacity";
        seeds.push_back(
            {"jobs", "object-field-wrong-scope.ppm", envelope(jobs::json::encodeProject(wrongScope), 16), false});
        seeds.push_back(
            {"jobs", "object-fields-schema15.ppm", envelope(objectBody, 15), true}); // raw schema16 body is valid
        auto adopted = minimal;
        adopted.progressPolicy = jobs::ProgressPolicySelection{
            jobs::overrideProgressPolicy, "PM", "2026-11-03T00:00:00Z", "Seed", adopted.documentId, adopted.revision};
        const auto adoptedBody = jobs::json::encodeProject(adopted);
        check(jobs::decodeProjectFile(envelope(adoptedBody, 13)) == adopted, "progress schema13");
        seeds.push_back({"jobs", "progress-policy.ppm", envelope(adoptedBody, 13), true});
        bool oldSchemaRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(adoptedBody, 12));
        } catch (const std::runtime_error &) {
            oldSchemaRejected = true;
        }
        check(oldSchemaRejected, "schema12 accepted progress policy");
        // The envelope rejects it; the harness also probes the valid schema13 raw body.
        seeds.push_back({"jobs", "progress-policy-schema12.ppm", envelope(adoptedBody, 12), true});
        auto invalidPolicy = adoptedBody;
        auto policyObject = adoptedBody["progressPolicy"].toObject();
        policyObject["policy"] = "future-policy";
        invalidPolicy["progressPolicy"] = policyObject;
        seeds.push_back({"jobs", "progress-policy-unknown.ppm", envelope(invalidPolicy, 13), false});
        policyObject = adoptedBody["progressPolicy"].toObject();
        policyObject["unknown"] = true;
        invalidPolicy["progressPolicy"] = policyObject;
        seeds.push_back({"jobs", "progress-policy-unknown-field.ppm", envelope(invalidPolicy, 13), false});
        auto combined = adopted;
        combined.statusAt = "2026-11-03 09:00";
        combined.remainingLeveling = jobs::LevelingSelection{};
        combined.remainingLeveling->policy = "serial-no-split-v1";
        combined.remainingLeveling->adoptedAt = "2026-11-03T00:00:00Z";
        combined.remainingLeveling->reason = "Seed";
        combined.remainingLeveling->sourceDocumentId = combined.documentId;
        combined.remainingLeveling->sourceRevision = combined.revision;
        const auto combinedBody = jobs::json::encodeProject(combined);
        check(jobs::decodeProjectFile(envelope(combinedBody, 14)) == combined, "combined schema14");
        seeds.push_back({"jobs", "combined-policy-leveling.ppm", envelope(combinedBody, 14), true});
        bool combinedOldRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(combinedBody, 13));
        } catch (const std::runtime_error &) {
            combinedOldRejected = true;
        }
        check(combinedOldRejected, "schema13 accepted combined policy");
        seeds.push_back(
            {"jobs", "combined-schema13-envelope.ppm", envelope(combinedBody, 13), true}); // raw body is valid14
        auto scenarioPolicy = adopted;
        scenarioPolicy.revision = 2;
        jobs::LevelingScenario frozen;
        frozen.id = "S1";
        frozen.name = "Policy snapshot";
        frozen.createdAt = "2026-11-03T00:00:00Z";
        frozen.sourceDocumentId = scenarioPolicy.documentId;
        frozen.inputRevision = 1;
        frozen.input = minimal;
        frozen.statusAt = combined.statusAt;
        frozen.progressPolicy = adopted.progressPolicy;
        scenarioPolicy.progressPolicy.reset();
        scenarioPolicy.scenarios.push_back(frozen);
        const auto scenarioBody = jobs::json::encodeProject(scenarioPolicy);
        check(jobs::decodeProjectFile(envelope(scenarioBody, 14)) == scenarioPolicy, "scenario policy snapshot14");
        seeds.push_back({"jobs", "scenario-progress-policy.ppm", envelope(scenarioBody, 14), true});
        auto leveled = minimal;
        jobs::LevelingSelection selection;
        selection.policy = "serial-no-split-v1";
        selection.adoptedAt = "2026-11-03T00:00:00Z";
        selection.reason = "Seed";
        selection.sourceDocumentId = leveled.documentId;
        selection.sourceRevision = leveled.revision;
        leveled.planLeveling = selection;
        const auto selectedBody = jobs::json::encodeProject(leveled);
        check(jobs::decodeProjectFile(envelope(selectedBody, 12)) == leveled, "selection schema12 roundtrip failed");
        auto journal = leveled;
        ++journal.revision;
        jobs::appendLevelingDecision(journal, minimal, "adopt", selection.reason);
        const auto journalBody = jobs::json::encodeProject(journal);
        check(jobs::decodeProjectFile(envelope(journalBody, 15)) == journal, "decision schema15 roundtrip failed");
        seeds.push_back({"jobs", "leveling-decisions.ppm", envelope(journalBody, 15), true});
        auto malformedJournal = journalBody;
        auto decisions = malformedJournal["levelingDecisions"].toArray();
        auto decision = decisions[0].toObject();
        decision["previousEntryId"] = "missing";
        decisions[0] = decision;
        malformedJournal["levelingDecisions"] = decisions;
        seeds.push_back({"jobs", "leveling-decision-broken-chain.ppm", envelope(malformedJournal, 15), false});
        seeds.push_back({"jobs", "leveling-selection.ppm", envelope(selectedBody, 12), true});
        auto invalidSelection = selectedBody;
        auto selectionObject = invalidSelection["planLeveling"].toObject();
        selectionObject["policy"] = "future-policy";
        invalidSelection["planLeveling"] = selectionObject;
        seeds.push_back({"jobs", "leveling-unknown-policy.ppm", envelope(invalidSelection, 12), false});
        selectionObject = selectedBody["planLeveling"].toObject();
        selectionObject["unknown"] = true;
        invalidSelection["planLeveling"] = selectionObject;
        seeds.push_back({"jobs", "leveling-unknown-field.ppm", envelope(invalidSelection, 12), false});
        auto classified = minimal;
        classified.customFields = {{"text", "구역", jobs::CustomFieldType::Text, {}, {}},
                                   {"number", "수량", jobs::CustomFieldType::Number, "m²", {}},
                                   {"date", "납기", jobs::CustomFieldType::Date, {}, {}},
                                   {"choice", "상태", jobs::CustomFieldType::Choice, {}, {{"option", "계획"}}}};
        classified.codeTypes = {{"discipline", "공종", {{"root", "토목", {}}, {"child", "구조물", "root"}}}};
        classified.tasks[0].attributes = {{"field:text", "동측"},
                                          {"field:number", "-0.125001"},
                                          {"field:date", "2026-11-02"},
                                          {"field:choice", "option"},
                                          {"code:discipline", "child"}};
        jobs::SavedTaskView savedView{"view", "분류 보기", {}};
        savedView.settings.groupBy = {"code:discipline", "field:choice"};
        savedView.settings.groupPath = {"vchild"};
        savedView.settings.attributeFilters = {{"field:number", "present", ""}};
        savedView.settings.columns = {{"field:number", 180, false}, {"name", 200, false}};
        classified.savedTaskViews.push_back(savedView);
        auto classificationBody = jobs::json::encodeProject(classified);
        const auto classificationBytes = envelope(classificationBody, 10);
        check(jobs::decodeProjectFile(classificationBytes) == classified, "classification seed lost schema10 data");
        seeds.push_back({"jobs", "classification-schema10.ppm", classificationBytes, true});
        auto display = minimal;
        display.printPreferences = jobs::PrintPreferences{};
        display.networkPositions = {{"T1", -30, 240}};
        auto displayBody = jobs::json::encodeProject(display);
        check(jobs::decodeProjectFile(envelope(displayBody, 18)) == display, "presentation schema18 roundtrip");
        bool oldDisplayRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(displayBody, 17));
        } catch (const std::runtime_error &) {
            oldDisplayRejected = true;
        }
        check(oldDisplayRejected, "schema17 accepted presentation");
        seeds.push_back({"jobs", "presentation18.ppm", envelope(displayBody, 18), true});
        seeds.push_back({"jobs", "presentation-old17.ppm", envelope(displayBody, 17), true});
        display.networkPositions[0].taskId = "missing";
        seeds.push_back(
            {"jobs", "presentation-missing-task.ppm", envelope(jobs::json::encodeProject(display), 18), false});
        display.networkPositions = {{"T1", 1000001, 0}};
        seeds.push_back(
            {"jobs", "presentation-coordinate.ppm", envelope(jobs::json::encodeProject(display), 18), false});
        auto shaped = minimal;
        shaped.calculationProfile = jobs::effortProfile;
        shaped.resources = {{"R", "Crew", "labor", 200, {}}};
        shaped.assignments = {{"T1", "R", 100, {{0, 30, 150}, {30, 60, 50}}}};
        auto shapeBody = jobs::json::encodeProject(shaped);
        check(jobs::decodeProjectFile(envelope(shapeBody, 17)) == shaped, "contour schema17 round trip");
        seeds.push_back({"jobs", "assignment-periods17.ppm", envelope(shapeBody, 17), true});
        bool oldPeriodsRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(shapeBody, 16));
        } catch (const std::runtime_error &) {
            oldPeriodsRejected = true;
        }
        check(oldPeriodsRejected, "schema16 envelope accepted assignment periods");
        seeds.push_back({"jobs", "assignment-periods-old16.ppm", envelope(shapeBody, 16), true}); // schema17 body path
        shaped.assignments[0].periods[1].fromMinute = 20;
        seeds.push_back(
            {"jobs", "assignment-period-overlap.ppm", envelope(jobs::json::encodeProject(shaped), 17), false});
        shaped.assignments[0].periods = {{0, 60, 100001}};
        seeds.push_back({"jobs", "assignment-period-rate.ppm", envelope(jobs::json::encodeProject(shaped), 17), false});
        shaped.assignments[0].periods = {{0, 60, 0}};
        seeds.push_back({"jobs", "assignment-period-zero.ppm", envelope(jobs::json::encodeProject(shaped), 17), true});
        shaped.tasks[0].taskType = jobs::TaskType::FixedUnits;
        seeds.push_back({"jobs", "assignment-period-old17-units-body.ppm",
                         envelope(jobs::json::encodeProject(shaped), 17),
                         true}); // Valid schema24 raw body; old envelope remains rejected.
        auto actual = minimal;
        actual.revision = 2;
        actual.statusAt = "2026-11-03 18:00";
        actual.resources = {{"R", "Crew", "labor", 100, {}}};
        actual.assignments = {{"T1", "R", 100}};
        jobs::CostEntry cost;
        cost.id = "C1";
        cost.taskId = "T1";
        cost.statusAt = *actual.statusAt;
        cost.recordedAt = "2026-11-03T00:00:00Z";
        cost.reporter = "PM";
        cost.reason = "Seed";
        cost.sourceDocumentId = actual.documentId;
        cost.recordedRevision = 2;
        cost.currency = "KRW";
        cost.actualMinor = 80;
        actual.costEntries = {cost};
        jobs::PeriodActualEntry entry;
        entry.id = "P1";
        entry.taskId = "T1";
        entry.resourceId = "R";
        entry.resourceType = "labor";
        entry.date = "2026-11-02";
        entry.statusAt = *actual.statusAt;
        entry.recordedAt = cost.recordedAt;
        entry.reporter = cost.reporter;
        entry.reason = cost.reason;
        entry.sourceDocumentId = actual.documentId;
        entry.recordedRevision = 2;
        entry.currency = "KRW";
        entry.costEntryId = "C1";
        entry.workUnitMinutes = 6001;
        entry.actualMinor = 30;
        actual.periodActuals = {entry};
        const auto actualBody = jobs::json::encodeProject(actual);
        const auto actualBytes = envelope(actualBody, 11);
        check(jobs::decodeProjectFile(actualBytes) == actual, "period actual schema11 roundtrip failed");
        seeds.push_back({"jobs", "period-actual-schema11.ppm", actualBytes, true});
        auto invalidActual = actualBody;
        auto periodRows = invalidActual["periodActuals"].toArray();
        auto periodRow = periodRows[0].toObject();
        periodRow["workUnitMinutes"] = 1.5;
        periodRows[0] = periodRow;
        invalidActual["periodActuals"] = periodRows;
        seeds.push_back({"jobs", "period-actual-fraction.ppm", envelope(invalidActual, 11), false});
        seeds.push_back({"jobs", "classification-payload10.json", QJsonDocument(classificationBody).toJson(), true});
        auto definitions = classificationBody["customFields"].toArray();
        auto invalidDefinition = definitions[0].toObject();
        invalidDefinition["unknown"] = true;
        definitions[0] = invalidDefinition;
        classificationBody["customFields"] = definitions;
        seeds.push_back({"jobs", "classification-unknown.ppm", envelope(classificationBody, 10), false});
        const auto xml = read(fixtureRoot + "/project-xml-plan.xml");
        const auto xer = read(fixtureRoot + "/primavera-plan.xer");
        seeds.push_back({"xml", "plan.xml", xml, true});
        seeds.push_back({"xer", "plan.xer", xer, true});
        const auto actualXer = read(fixtureRoot + "/primavera-actual-cost.xer");
        const auto financialXer = read(fixtureRoot + "/primavera-financial-periods.xer");
        seeds.push_back({"xer", "financial-periods.xer", financialXer, true});
        const auto financialProject = jobs::importPrimaveraXer(financialXer, "source.xer").project;
        const auto financialBody = jobs::json::encodeProject(financialProject);
        seeds.push_back({"jobs", "financial-snapshot.ppm", envelope(financialBody, 25), true});
        seeds.push_back({"jobs", "financial-old-schema.ppm", envelope(financialBody, 24),
                         true}); // Raw schema25 body path remains valid.
        auto malformedFinancial = financialBody;
        auto archive = malformedFinancial["xerFinancials"].toObject();
        archive["sourceSha256"] = "bad";
        malformedFinancial["xerFinancials"] = archive;
        seeds.push_back({"jobs", "financial-bad-source.ppm", envelope(malformedFinancial, 25), false});
        QTemporaryDir financialTemp;
        jobs::JsonProjectStore financialStore;
        const auto financialPath = (financialTemp.path() + "/financial.ppm").toStdString();
        financialStore.save(financialPath, financialProject, {});
        jobs::Document financialDocument(financialStore);
        financialDocument.open(financialPath);
        jobs::XerFinancialSubmission adjustment{*financialProject.xerFinancials,
                                                jobs::initialXerFinancialAllocations(*financialProject.xerFinancials),
                                                "parser", "explicit allocation", "2026-11-07T06:00:00Z"};
        adjustment.allocations.push_back({0, {{"2026-10-19", 1}, {"2026-10-20", 2}}});
        financialDocument.recordXerFinancials(adjustment, jobs::xerFinancialProjectionHash);
        const auto adjustmentBody = jobs::json::encodeProject(*financialDocument.project());
        financialDocument.renameTask(0, "merged parser task");
        financialDocument.recordXerFinancialMerge({{}, "parser", "merge current ledger", "2026-11-07T08:00:00Z"},
                                                  jobs::xerFinancialProjectionHash);
        const auto mergeBody = jobs::json::encodeProject(*financialDocument.project());
        const auto evmPath = (financialTemp.path() + "/evm.ppm").toStdString();
        financialStore.save(evmPath, financialProject, {});
        jobs::Document evmDocument(financialStore);
        evmDocument.open(evmPath);
        const auto evmBaseline =
            evmDocument.captureBaseline("EV source", "parser", "source adoption", "2026-11-08T00:00:00Z");
        auto adoption = jobs::externalEvmDraft(*evmDocument.project(), evmBaseline);
        for (std::size_t i = 0; i < financialProject.xerFinancials->periods.size(); ++i) {
            const auto &row = financialProject.xerFinancials->periods[i];
            if (row.resourceId.empty() && row.bcwp && row.bcws) {
                adoption.rows.push_back({i, 0, 0});
                adoption.taskIds.push_back(row.taskId);
                break;
            }
        }
        adoption.confirmed = true;
        adoption.reporter = "parser";
        adoption.reason = "explicit EV/PV opening";
        adoption.recordedAt = "2026-11-08T03:00:00Z";
        evmDocument.recordExternalEvm(adoption);
        const auto evmAdoptedBody = jobs::json::encodeProject(*evmDocument.project());
        seeds.push_back({"jobs", "external-evm.ppm", envelope(evmAdoptedBody, 29), true});
        for (int version : {28, jobs::maximumNativeSchema + 1}) {
            const auto bytes = envelope(evmAdoptedBody, version);
            bool refused = false;
            try {
                (void)jobs::decodeProjectFile(bytes);
            } catch (const std::runtime_error &) {
                refused = true;
            }
            check(refused, "external EVM envelope version accepted");
            // The envelope must fail, while the independent raw-body probe still accepts schema29.
            seeds.push_back({"jobs", QString("external-evm-envelope-%1-valid-body.ppm").arg(version), bytes, true});
        }
        for (int i = 0; i < 6; ++i) {
            auto bad = evmAdoptedBody;
            auto history = bad["externalEvmAdoptions"].toArray();
            auto entry = history[0].toObject();
            if (i == 0)
                entry["confirmed"] = false;
            if (i == 1)
                entry["sourceSha256"] = "missing";
            if (i == 2)
                entry["useEv"] = "true";
            if (i == 3)
                entry["policy"] = "future";
            if (i >= 4) {
                auto rows = entry["rows"].toArray();
                auto row = rows[0].toObject();
                row[i == 4 ? "periodIndex" : "openingEv"] = i == 4 ? QJsonValue(100000) : QJsonValue(-1);
                rows[0] = row;
                entry["rows"] = rows;
            }
            history[0] = entry;
            bad["externalEvmAdoptions"] = history;
            seeds.push_back({"jobs", QString("external-evm-bad-%1.ppm").arg(i), envelope(bad, 29), false});
        }
        jobs::Document periodDocument(financialStore);
        periodDocument.create("Custom periods", "2026-10-26", {"Task"});
        periodDocument.setDuration(0, 60);
        const auto periodBaseline =
            periodDocument.captureBaseline("Baseline", "parser", "periods", "2026-11-08T00:00:00Z");
        periodDocument.setStatusAt("2026-10-27 00:00");
        jobs::FinancialPeriodSubmission periodInput;
        periodInput.period = {"",           "Custom",     periodBaseline,
                              "2026-10-26", "2026-10-26", {{periodDocument.project()->tasks[0].id, 0, 0}}};
        periodInput.confirmed = true;
        periodInput.reporter = "parser";
        periodInput.reason = "period close";
        periodInput.recordedAt = "2026-11-08T04:00:00Z";
        periodInput.period.id = periodDocument.recordFinancialPeriod(periodInput);
        seeds.push_back({"jobs", "custom-period-open.ppm",
                         envelope(jobs::json::encodeProject(*periodDocument.project()), 30), true});
        periodInput.action = "close";
        periodDocument.recordFinancialPeriod(periodInput);
        const auto closedPeriodBody = jobs::json::encodeProject(*periodDocument.project());
        seeds.push_back({"jobs", "custom-period-closed.ppm", envelope(closedPeriodBody, 30), true});
        for (int version : {29, jobs::maximumNativeSchema + 1}) {
            bool refused = false;
            try {
                (void)jobs::decodeProjectFile(envelope(closedPeriodBody, version));
            } catch (const std::runtime_error &) {
                refused = true;
            }
            check(refused, "custom period envelope version accepted");
            seeds.push_back({"jobs", QString("custom-period-envelope-%1-valid-body.ppm").arg(version),
                             envelope(closedPeriodBody, version), true});
        }
        periodInput.action = "reopen";
        periodDocument.recordFinancialPeriod(periodInput);
        seeds.push_back({"jobs", "custom-period-reopened.ppm",
                         envelope(jobs::json::encodeProject(*periodDocument.project()), 30), true});
        for (int i = 0; i < 14; ++i) {
            auto bad = closedPeriodBody;
            auto events = bad["financialPeriodEvents"].toArray();
            auto e = events[1].toObject();
            if (i == 0)
                e["confirmed"] = false;
            if (i == 1)
                e["action"] = "reopen";
            if (i == 2)
                e["policy"] = "future";
            if (i == 3)
                e["knowledgeRevision"] = 0;
            if (i == 4)
                e["recordedRevision"] = 1;
            if (i == 5)
                e["tasks"] = QJsonArray{};
            if (i == 6)
                e["currency"] = "USD";
            if (i == 7)
                e["extra"] = true;
            if (i == 8)
                e["statusAt"] = QJsonValue::Null;
            if (i == 9)
                e["formula"] = "future";
            if (i >= 10) {
                auto def = e["period"].toObject();
                if (i == 10)
                    def["baselineId"] = "missing";
                if (i == 11)
                    def["toDate"] = "2026-02-30";
                if (i == 12)
                    def["values"] = "bad";
                if (i == 13)
                    def["id"] = "another";
                e["period"] = def;
            }
            events[1] = e;
            bad["financialPeriodEvents"] = events;
            seeds.push_back({"jobs", QString("custom-period-bad-%1.ppm").arg(i), envelope(bad, 30), false});
        }
        seeds.push_back({"jobs", "financial-merge.ppm", envelope(mergeBody, 27), true});
        const auto relinkPath = (financialTemp.path() + "/relink.ppm").toStdString();
        financialStore.save(relinkPath, financialProject, {});
        jobs::Document relinkDocument(financialStore);
        relinkDocument.open(relinkPath);
        jobs::XerRelinkAudit sourceAudit;
        sourceAudit.incoming = *financialProject.xerFinancials;
        sourceAudit.incoming.sourceName = "new-version.xer";
        sourceAudit.projectName = "New source";
        sourceAudit.sourceStatusAt = sourceAudit.incoming.statusAt;
        sourceAudit.confirmed = true;
        sourceAudit.incomingLinks = jobs::suggestXerLinks(sourceAudit.incoming, financialProject);
        sourceAudit.previousLinks = sourceAudit.incomingLinks;
        relinkDocument.recordXerRelink({sourceAudit, {{}, "parser", "source ID review", "2026-11-08T02:00:00Z"}},
                                       jobs::xerFinancialProjectionHash);
        const auto relinkBody = jobs::json::encodeProject(*relinkDocument.project());
        seeds.push_back({"jobs", "source-relink.ppm", envelope(relinkBody, 28), true});
        const auto schedulePath = (financialTemp.path() + "/schedule.ppm").toStdString();
        financialStore.save(schedulePath, financialProject, {});
        jobs::Document scheduleDocument(financialStore);
        scheduleDocument.open(schedulePath);
        jobs::XerScheduleSubmission scheduleInput;
        scheduleInput.source.input = financialProject;
        for (auto &t : scheduleInput.source.input.tasks) {
            t.constraintType = jobs::ConstraintType::ASAP;
            t.constraintAt.reset();
        }
        scheduleInput.source.input.tasks[0].durationMinutes = 60;
        scheduleInput.source.sourceName = "schedule.xer";
        scheduleInput.source.sourceSha256 = financialProject.xerFinancials->sourceSha256;
        scheduleInput.source.projectId = "100";
        scheduleInput.source.importedAt = "2026-11-08T04:00:00Z";
        scheduleInput.source.reportJson = "{}";
        scheduleInput.links = jobs::suggestXerScheduleLinks(scheduleInput.source.input, financialProject);
        scheduleInput.confirmed = true;
        scheduleInput.reporter = "parser";
        scheduleInput.reason = "schedule review";
        scheduleInput.recordedAt = "2026-11-08T04:00:00Z";
        scheduleDocument.recordXerSchedule(scheduleInput);
        const auto scheduleBody = jobs::json::encodeProject(*scheduleDocument.project());
        seeds.push_back({"jobs", "schedule-adoption.ppm", envelope(scheduleBody, 32), true});
        for (int version : {31, jobs::maximumNativeSchema + 1}) {
            bool refused = false;
            try {
                (void)jobs::decodeProjectFile(envelope(scheduleBody, version));
            } catch (const std::runtime_error &) {
                refused = true;
            }
            check(refused, "schedule adoption envelope accepted");
            seeds.push_back({"jobs", QString("schedule-envelope-%1-valid-body.ppm").arg(version),
                             envelope(scheduleBody, version), true});
        }
        for (int kind = 0; kind < 12; ++kind) {
            auto bad = scheduleBody;
            auto events = bad["xerScheduleAdoptions"].toArray();
            auto e = events[0].toObject();
            if (kind == 0)
                e["policy"] = "unknown";
            if (kind == 1)
                e["confirmed"] = false;
            if (kind == 2)
                e["recordedRevision"] = 1.5;
            if (kind == 3)
                e["unknown"] = 1;
            if (kind == 4)
                e.remove("before");
            if (kind == 5) {
                auto a = e["after"].toObject();
                a["startDate"] = "2026-11-04";
                e["after"] = a;
            }
            if (kind == 6) {
                auto a = e["source"].toObject();
                a["sourceSha256"] = "bad";
                e["source"] = a;
            }
            if (kind == 7) {
                auto links = e["links"].toArray();
                auto l = links[0].toObject();
                l["create"] = "true";
                links[0] = l;
                e["links"] = links;
            }
            if (kind == 8)
                e["recordedAt"] = "yesterday";
            if (kind == 9)
                e["reporter"] = " ";
            if (kind == 10)
                e["links"] = QJsonArray{};
            if (kind == 11) {
                auto a = e["source"].toObject();
                a["statusAt"] = 5;
                e["source"] = a;
            }
            events[0] = e;
            bad["xerScheduleAdoptions"] = events;
            seeds.push_back({"jobs", QString("schedule-adoption-bad-%1.ppm").arg(kind), envelope(bad, 32), false});
        }
        jobs::XerSourceConversion forward{
            financialProject.currency == "USD" ? "EUR" : "USD",          "2026-11-08", "parser source conversion", 2, 1,
            {{"labor", "equipment", 2, 1}, {"equipment", "labor", 2, 1}}};
        sourceAudit.incoming = jobs::convertXerSource(sourceAudit.incoming, forward);
        sourceAudit.incomingConversion =
            jobs::XerSourceConversion{financialProject.currency,
                                      "2026-11-08",
                                      "parser inverse conversion",
                                      1,
                                      2,
                                      {{"labor", "equipment", 1, 2}, {"equipment", "labor", 1, 2}}};
        relinkDocument.recordXerRelink(
            {sourceAudit, {{}, "parser", "source conversion review", "2026-11-08T03:00:00Z"}},
            jobs::xerFinancialProjectionHash);
        const auto conversionBody = jobs::json::encodeProject(*relinkDocument.project());
        seeds.push_back({"jobs", "source-conversion.ppm", envelope(conversionBody, 31), true});
        for (int version : {30, jobs::maximumNativeSchema + 1}) {
            bool refused = false;
            try {
                (void)jobs::decodeProjectFile(envelope(conversionBody, version));
            } catch (const std::runtime_error &) {
                refused = true;
            }
            check(refused, "source conversion envelope version accepted");
            seeds.push_back({"jobs", QString("source-conversion-version-%1.ppm").arg(version),
                             envelope(conversionBody, version), true}); // Raw schema31 body remains valid.
        }
        for (int kind = 0; kind < 12; ++kind) {
            auto bad = conversionBody;
            auto history = bad["xerFinancialRevisions"].toArray();
            auto revision = history.last().toObject();
            auto audit = revision["relink"].toObject();
            auto c = audit["incomingConversion"].toObject();
            if (kind == 0)
                c["numerator"] = 0;
            if (kind == 1)
                c["denominator"] = QJsonValue::Null;
            if (kind == 2)
                c["denominator"] = 0.5;
            if (kind == 3)
                c["numerator"] = 1000000001;
            if (kind == 4)
                c["targetCurrency"] = "JPY";
            if (kind == 5)
                c["effectiveDate"] = "2026-02-30";
            if (kind == 6)
                c["reference"] = " ";
            if (kind == 7)
                c["policy"] = "unknown";
            if (kind == 8)
                c["unexpected"] = true;
            if (kind == 9)
                c.remove("reference");
            if (kind == 10) {
                auto rules = c["resources"].toArray();
                rules.append(rules[0]);
                c["resources"] = rules;
            }
            if (kind == 11) {
                auto rules = c["resources"].toArray();
                auto rule = rules[1].toObject();
                rule["workNumerator"] = 2;
                rules[1] = rule;
                c["resources"] = rules;
            }
            audit["incomingConversion"] = c;
            revision["relink"] = audit;
            history[history.size() - 1] = revision;
            bad["xerFinancialRevisions"] = history;
            seeds.push_back({"jobs", QString("source-conversion-bad-%1.ppm").arg(kind), envelope(bad, 31), false});
        }
        for (int kind = 0; kind < 4; ++kind) {
            auto bad = relinkBody;
            auto history = bad["xerFinancialRevisions"].toArray();
            auto revision = history[0].toObject();
            auto audit = revision["relink"].toObject();
            if (kind == 0)
                audit["confirmed"] = false;
            if (kind == 1)
                audit["policy"] = "unknown";
            if (kind == 2) {
                auto list = audit["incomingLinks"].toArray();
                auto link = list[0].toObject();
                link["targetId"] = "missing";
                list[0] = link;
                audit["incomingLinks"] = list;
            }
            if (kind == 3)
                audit["sourceStatusAt"] = 1;
            revision["relink"] = audit;
            history[0] = revision;
            bad["xerFinancialRevisions"] = history;
            seeds.push_back({"jobs", QString("source-relink-bad-%1.ppm").arg(kind), envelope(bad, 28), false});
        }
        for (int kind = 0; kind < 4; ++kind) {
            auto body = mergeBody;
            auto revisions = body["xerFinancialRevisions"].toArray();
            auto revision = revisions.last().toObject();
            auto merge = revision["merge"].toObject();
            if (kind == 0)
                merge["baseProjectionSha256"] = QString(64, '0');
            if (kind == 1)
                merge["policy"] = "unknown";
            if (kind == 2)
                merge["replacedPeriods"] = QJsonArray{99999};
            if (kind == 3) {
                auto owned = revision["ownedEntryIds"].toArray();
                owned[0] = QString::fromStdString(financialProject.periodActuals[0].id);
                revision["ownedEntryIds"] = owned;
            }
            revision["merge"] = merge;
            revisions[revisions.size() - 1] = revision;
            body["xerFinancialRevisions"] = revisions;
            seeds.push_back({"jobs", QString("financial-merge-bad-%1.ppm").arg(kind), envelope(body, 27), false});
        }
        seeds.push_back({"jobs", "financial-adjustment.ppm", envelope(adjustmentBody, 26), true});
        for (int kind = 0; kind < 4; ++kind) {
            auto body = adjustmentBody;
            auto revisions = body["xerFinancialRevisions"].toArray();
            auto revision = revisions[0].toObject();
            if (kind == 0)
                revision["ownedEntryIds"] = QJsonArray{"missing"};
            else {
                auto allocations = revision["allocations"].toArray();
                auto allocation = allocations[0].toObject();
                if (kind == 1)
                    allocation["periodIndex"] = 999;
                if (kind == 2) {
                    auto days = allocation["days"].toArray();
                    auto day = days[0].toObject();
                    day["weight"] = 0;
                    days[0] = day;
                    allocation["days"] = days;
                }
                if (kind == 3)
                    allocation["unknown"] = true;
                allocations[0] = allocation;
                revision["allocations"] = allocations;
            }
            revisions[0] = revision;
            body["xerFinancialRevisions"] = revisions;
            seeds.push_back({"jobs", QString("financial-adjustment-bad-%1.ppm").arg(kind), envelope(body, 26), false});
        }
        seeds.push_back({"xer", "actual-cost.xer", actualXer, true});
        auto badActual = actualXer;
        badActual.replace("%R\t101\t20\t100\t1\t2\t70.12", "%R\t102\t20\t100\t1\t2\t70.12");
        seeds.push_back({"xer", "actual-cost-owner.xer", badActual, false});
        badActual = actualXer;
        badActual.replace("\t2\t70.12", "\t2\t-70.12");
        seeds.push_back({"xer", "actual-cost-negative.xer", badActual, false});
        seeds.push_back({"xer", "multi-plan.xer", read(fixtureRoot + "/primavera-multi-plan.xer"), true});
        auto westernXer = QString::fromUtf8(xer).toLatin1();
        westernXer.replace("SITE-01", QByteArray("Caf\xe9", 4));
        check(jobs::fuzz::probe("xer", westernXer).accepted == 1, "XER encoding branches were not distinguished");
        seeds.push_back({"xer", "windows-1252.xer", westernXer, true});
        auto codeProject = minimal;
        codeProject.codeTypes = {{"trade", "Trade", {{"root", "Root", ""}, {"child", "Child", "root"}}}};
        codeProject.tasks[0].attributes = {{"code:trade", "child"}};
        const auto codeXer = jobs::exportPrimaveraXer(codeProject).xer;
        seeds.push_back({"xer", "codes.xer", codeXer, true});
        auto cyclicCode = codeXer;
        cyclicCode.replace("%R\t1\t\t1\tRoot\tRoot\t1", "%R\t1\t2\t1\tRoot\tRoot\t1");
        check(cyclicCode != codeXer, "cycle seed mutation");
        seeds.push_back({"xer", "code-cycle.xer", cyclicCode, false});
        auto missingCode = codeXer;
        missingCode.replace("%R\t1\t2\t1\t1\r\n", "%R\t1\t999\t1\t1\r\n");
        check(missingCode != codeXer, "dangling code seed mutation");
        seeds.push_back({"xer", "code-missing-value.xer", missingCode, false});
        auto duplicateCode = codeXer;
        duplicateCode.replace("%R\t1\t2\t1\t1\r\n", "%R\t1\t2\t1\t1\r\n%R\t1\t2\t1\t1\r\n");
        seeds.push_back({"xer", "code-duplicate-assignment.xer", duplicateCode, false});
        auto secondCodeProject = codeProject;
        secondCodeProject.documentId = "other-code-project";
        std::vector<jobs::PrimaveraXerExchange> codeExports{jobs::exportPrimaveraXer(codeProject),
                                                            jobs::exportPrimaveraXer(secondCodeProject)};
        seeds.push_back({"xer", "codes-multi.xer", jobs::combinePrimaveraXer(codeExports), true});
        auto activity = minimal;
        jobs::ActivityDefinition steps;
        steps.id = "steps";
        steps.name = "Steps";
        steps.taskId = "T1";
        steps.steps = {{"s1", "First", 1}};
        activity.activities = {steps};
        seeds.push_back({"jobs", "activities19.ppm", envelope(jobs::json::encodeProject(activity), 19), true});
        activity.activities[0].steps[0].weight = 0;
        seeds.push_back({"jobs", "activity-zero-weight.ppm", envelope(jobs::json::encodeProject(activity), 19), false});
        activity.activities[0].steps[0].weight = 1;
        activity.activities[0].taskId = "missing";
        seeds.push_back(
            {"jobs", "activity-missing-task.ppm", envelope(jobs::json::encodeProject(activity), 19), false});
        activity.activities[0].taskId = "T1";
        jobs::StepReport stepReport;
        stepReport.id = "step-report";
        stepReport.taskId = "T1";
        stepReport.statusAt = "2026-10-26 18:00";
        stepReport.recordedAt = "2026-11-05T00:00:00Z";
        stepReport.reporter = "PM";
        stepReport.reason = "seed";
        stepReport.sourceDocumentId = activity.documentId;
        stepReport.recordedRevision = 1;
        stepReport.steps = {{steps.steps[0], 10000}};
        activity.stepReports = {stepReport};
        seeds.push_back({"jobs", "step-report19.ppm", envelope(jobs::json::encodeProject(activity), 19), true});
        activity.stepReports[0].steps[0].basisPoints = 10001;
        seeds.push_back({"jobs", "step-report-percent.ppm", envelope(jobs::json::encodeProject(activity), 19), false});
        auto resourcePlan = minimal;
        resourcePlan.resourcePlan.materials = {{"material", "Bolt", "piece", 199, 2000}};
        resourcePlan.resourcePlan.uses = {{"material-use", "T1", "material", 1500, "start"}};
        resourcePlan.resourcePlan.expenses = {{"expense", "T1", "Delivery", "logistics", 101, "uniform"}};
        const auto resourceBody = jobs::json::encodeProject(resourcePlan);
        check(jobs::decodeProjectFile(envelope(resourceBody, 20)) == resourcePlan, "resource schema20 parity");
        seeds.push_back({"jobs", "resource-plan20.ppm", envelope(resourceBody, 20), true});
        resourcePlan.resourcePlan.uses[0].materialId = "missing";
        seeds.push_back({"jobs", "missing-material.ppm", envelope(jobs::json::encodeProject(resourcePlan), 20), false});
        const QJsonObject workspaceBody{
            {"id", "workspace-seed"},
            {"projects",
             QJsonArray{QJsonObject{{"documentId", "parser-seed"}, {"name", "Project"}, {"path", "Project.ppm"}}}},
            {"resources", QJsonArray{}},
            {"bindings", QJsonArray{}}};
        auto workspaceSeed = QJsonDocument::fromJson(envelope(workspaceBody, 1)).object();
        workspaceSeed["format"] = "JOBS-WORKSPACE";
        seeds.push_back({"workspace", "valid.ppmworkspace", QJsonDocument(workspaceSeed).toJson(), true});
        auto integratedBody = workspaceBody;
        auto refs = integratedBody["projects"].toArray();
        refs.append(QJsonObject{{"documentId", "other"}, {"name", "Other"}, {"path", "Other.ppm"}});
        integratedBody["projects"] = refs;
        integratedBody["links"] = QJsonArray{QJsonObject{{"id", "cross"},
                                                         {"predecessorProjectId", "parser-seed"},
                                                         {"predecessorTaskId", "T1"},
                                                         {"successorProjectId", "other"},
                                                         {"successorTaskId", "T1"},
                                                         {"type", "FS"},
                                                         {"lagMinutes", -60}}};
        integratedBody["integrated"] = QJsonObject{
            {"forecast", false}, {"levelShared", true}, {"priority", "deadline-first"}, {"horizonWorkDays", 260}};
        auto integratedSeed = QJsonDocument::fromJson(envelope(integratedBody, 2)).object();
        integratedSeed["format"] = "JOBS-WORKSPACE";
        seeds.push_back({"workspace", "integrated2.ppmworkspace", QJsonDocument(integratedSeed).toJson(), true});
        jobs::Governance governance;
        governance.members = {{"owner", "관리자", ""}};
        governance.grants = {{"owner", "", "owner"}};
        governance.risks = {
            {"risk", "parser-seed", "T1", "기간 변동", "owner", "open", "weather", 480, 960, 1440, 10000}};
        for (int variant = 0; variant < 8; ++variant) {
            auto body = integratedBody;
            auto g = jobs::encodeGovernance(governance);
            if (variant == 1)
                g["unknown"] = true;
            if (variant == 2)
                g["grants"] = QJsonArray{};
            if (variant == 3)
                g["members"] = QJsonArray{};
            if (variant >= 4) {
                auto risks = g["risks"].toArray();
                auto r = risks[0].toObject();
                if (variant == 4)
                    r["probability"] = 10001;
                if (variant == 5)
                    r["optimistic"] = 1000;
                if (variant == 6)
                    r["likely"] = 1.5;
                if (variant == 7)
                    r["projectId"] = "missing";
                risks[0] = r;
                g["risks"] = risks;
            }
            body["governance"] = g;
            auto root = QJsonDocument::fromJson(envelope(body, 3)).object();
            root["format"] = "JOBS-WORKSPACE";
            seeds.push_back({"workspace", QString("governance3-%1.ppmworkspace").arg(variant),
                             QJsonDocument(root).toJson(), variant == 0});
        }
        workspaceSeed["sha256"] = "tampered";
        for (int variant = 0; variant < 10; ++variant) {
            auto body = integratedBody;
            body["governance"] = jobs::encodeGovernance(jobs::Governance{});
            QJsonObject period{{"from", QJsonValue(QJsonValue::Null)}, {"to", "2026-11-16 09:29"}, {"units", 50}};
            QJsonArray periods{period, QJsonObject{{"from", "2026-11-16 09:30"},
                                                   {"to", QJsonValue(QJsonValue::Null)},
                                                   {"units", QJsonValue(QJsonValue::Null)}}};
            QJsonObject resource{
                {"id", "pool"},
                {"name", "Open pool"},
                {"type", "labor"},
                {"maxUnits", 100},
                {"exceptions", QJsonArray{}},
                {"calendar", QJsonObject{{"weekdays", QJsonArray{1, 2, 3, 4, 5}},
                                         {"periods", QJsonArray{QJsonObject{{"begin", 540}, {"end", 720}},
                                                                QJsonObject{{"begin", 780}, {"end", 1080}}}},
                                         {"holidays", QJsonArray{}}}},
                {"availableFrom", "2026-11-16 09:00"},
                {"availableTo", QJsonValue(QJsonValue::Null)}};
            if (variant == 1)
                period["from"] = "2026-11-16 09:30";
            if (variant == 2)
                period["units"] = 10000001;
            if (variant == 3)
                period["units"] = 1.5;
            if (variant == 4)
                period.remove("units");
            if (variant == 5)
                period["unknown"] = 1;
            if (variant == 6)
                resource.remove("availableTo");
            if (variant == 7)
                resource["availableFrom"] = "2026-11-16 09:00+09:00";
            periods[0] = period;
            resource["availability"] = periods;
            body["resources"] = QJsonArray{resource};
            auto root = QJsonDocument::fromJson(envelope(body, variant == 8 ? 3 : variant == 9 ? 5 : 4)).object();
            root["format"] = "JOBS-WORKSPACE";
            seeds.push_back({"workspace", QString("availability4-%1.ppmworkspace").arg(variant),
                             QJsonDocument(root).toJson(), variant == 0});
        }
        seeds.push_back({"workspace", "checksum.ppmworkspace", QJsonDocument(workspaceSeed).toJson(), false});
        // Rich PDM, constraints and effort payloads reach profile-specific decoders.
        for (const auto *profile : {jobs::pdmProfile, jobs::constraintProfile, jobs::effortProfile}) {
            auto p = jobs::importProjectXml(xml, "seed.xml").project;
            p.calculationProfile = profile;
            const auto bytes = envelope(jobs::json::encodeProject(p), 9);
            (void)jobs::decodeProjectFile(bytes);
            seeds.push_back({"jobs", QString::fromLatin1(profile) + ".ppm", bytes, true});
        }
        auto extended = minimal;
        extended.calculationProfile = jobs::effortProfile;
        extended.statusAt = "2026-10-26 18:00";
        extended.projectFields = {{"factor", "배수", jobs::CustomFieldType::Number, "", {}}};
        extended.projectAttributes = {{"field:factor", "2"}};
        extended.customFields = {
            {"hours", "환산 시간", jobs::CustomFieldType::Number, "h", {}, "[durationHours]*[project:factor]"}};
        jobs::Resource rr;
        rr.id = "R";
        rr.name = "인력";
        rr.attributes = {{"field:score", "2"}};
        extended.resources = {rr};
        extended.assignments = {{"T1", "R", 100}};
        extended.resourceFields = {
            {"score", "점수", jobs::CustomFieldType::Number, "", {}},
            {"weighted", "가중 점수", jobs::CustomFieldType::Number, "", {}, "[field:score]*[project:factor]"}};
        jobs::ResourceViewSettings rv;
        rv.sortKey = "field:weighted";
        rv.groupBy = {"type"};
        rv.filters = {{"field:weighted", "ge", "0"}};
        extended.savedResourceViews = {{"rv", "자원 보기", rv}};
        extended.remainingWork = {{"T1", "R", *extended.statusAt, {{"2026-10-27", 6001}}}};
        const auto extendedBody = jobs::json::encodeProject(extended);
        check(jobs::decodeProjectFile(envelope(extendedBody, 23)) == extended, "schema23 three-feature roundtrip");
        seeds.push_back({"jobs", "resource-dated-formula.ppm", envelope(extendedBody, 23), true});
        for (int variant = 0; variant < 10; ++variant) {
            auto body = extendedBody;
            if (variant < 6) {
                auto fields = body["customFields"].toArray();
                auto f = fields[0].toObject();
                if (variant == 0)
                    f["expression"] = "[field:hours]";
                if (variant == 1)
                    f["expression"] = "[durationHours]+1";
                if (variant == 2)
                    f["expression"] = false;
                if (variant == 3)
                    f["expression"] = QString(2049, '1');
                if (variant == 4)
                    f["expression"] = "[field:missing]";
                if (variant == 5)
                    f["expression"] = "eval(1)";
                fields[0] = f;
                body["customFields"] = fields;
            } else if (variant < 9) {
                auto plans = body["remainingWork"].toArray();
                auto plan = plans[0].toObject();
                auto days = plan["days"].toArray();
                auto day = days[0].toObject();
                if (variant == 6)
                    day["workUnitMinutes"] = .5;
                if (variant == 7)
                    days.append(day);
                if (variant == 8)
                    day["date"] = "2026-02-30";
                days[0] = day;
                plan["days"] = days;
                plans[0] = plan;
                body["remainingWork"] = plans;
            } else {
                auto tasks = body["tasks"].toArray();
                auto task = tasks[0].toObject();
                task["attributes"] = QJsonArray{QJsonObject{{"key", "field:hours"}, {"value", "1"}}};
                tasks[0] = task;
                body["tasks"] = tasks;
            }
            seeds.push_back({"jobs", QString("extended23-invalid-%1.ppm").arg(variant), envelope(body, 23), false});
        }
        auto enterprise = minimal;
        enterprise.projectFields = {{"project", "Project contract", jobs::CustomFieldType::Text, {}, {}}};
        enterprise.projectAttributes = {{"field:project", "P-1"}};
        jobs::Resource udfResource;
        udfResource.id = "udf-resource";
        udfResource.name = "UDF resource";
        enterprise.resources = {udfResource};
        enterprise.assignments = {{enterprise.tasks[0].id, udfResource.id, 100}};
        enterprise.assignmentFields = {{"assignment", "Assignment rating", jobs::CustomFieldType::Number, {}, {}}};
        enterprise.assignments[0].attributes = {{"field:assignment", "0"}};
        const auto enterpriseXml = jobs::exportProjectXml(enterprise).xml;
        const auto enterpriseXer = jobs::exportPrimaveraXer(enterprise).xer;
        seeds.push_back({"xml", "enterprise.xml", enterpriseXml, true});
        seeds.push_back({"xer", "assignment-udf.xer", enterpriseXer, true});
        const auto enterpriseBody = jobs::json::encodeProject(enterprise);
        seeds.push_back({"jobs", "enterprise.ppm", envelope(enterpriseBody, 37), true});
        bool oldEnterpriseRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(enterpriseBody, 36));
        } catch (const std::runtime_error &) {
            oldEnterpriseRejected = true;
        }
        check(oldEnterpriseRejected, "enterprise fields accepted in schema36 envelope");
        // The fuzz harness also tries the raw payload independently at schema37.
        seeds.push_back({"jobs", "enterprise-old.ppm", envelope(enterpriseBody, 36), true});
        auto invalidEnterprise = enterpriseXml;
        invalidEnterprise.replace("<ElemType>23</ElemType>", "<ElemType>21</ElemType>");
        seeds.push_back({"xml", "enterprise-scope.xml", invalidEnterprise, false});
        invalidEnterprise = enterpriseXml;
        invalidEnterprise.replace("<f408000>0</f408000>", "<f408000>0</f408000><f408000>1</f408000>");
        seeds.push_back({"xml", "enterprise-duplicate.xml", invalidEnterprise, false});
        auto dateProject = enterprise;
        dateProject.customFields = {{"date", "Date", jobs::CustomFieldType::Date, {}, {}},
                                    {"start", "Start", jobs::CustomFieldType::Start, {}, {}},
                                    {"finish", "Finish", jobs::CustomFieldType::Finish, {}, {}}};
        dateProject.tasks[0].attributes = {{"field:date", "2026-11-10T12:34:56.123456"},
                                           {"field:start", "2026-11-10T09:30:06"},
                                           {"field:finish", "2026-11-11T00:00:00"}};
        jobs::CustomField dateLookup{
            "date-list", "Finish lookup", jobs::CustomFieldType::Choice, {}, {{"a", "first"}, {"b", "second"}}};
        jobs::XmlLookup dateTable;
        dateTable.valueType = jobs::CustomFieldType::Finish;
        dateTable.tableGuid = "44444444-3333-4333-8444-555555555555";
        dateTable.values = {{"a", "2001", "44444444-3333-4333-8444-555555555551", "2026-11-10T18:00:00", {}, {}, true},
                            {"b", "2002", "44444444-3333-4333-8444-555555555552", "2026-11-11T00:00:06", {}, {}, true}};
        dateLookup.xmlLookup = dateTable;
        dateProject.customFields.push_back(dateLookup);
        dateProject.tasks[0].attributes.push_back({"field:date-list", "b"});
        const auto dateXml = jobs::exportProjectXml(dateProject).xml;
        seeds.push_back({"xml", "date-time.xml", dateXml, true});
        const auto dateBody = jobs::json::encodeProject(dateProject);
        seeds.push_back({"jobs", "date-time.ppm", envelope(dateBody, 40), true});
        bool oldDateRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(dateBody, 39));
        } catch (const std::runtime_error &) {
            oldDateRejected = true;
        }
        check(oldDateRejected, "date/time accepted in schema39 envelope");
        auto badDate = dateXml;
        badDate.replace("2026-11-10T09:30:06", "2026-11-10T24:30:06");
        seeds.push_back({"xml", "date-time-hour.xml", badDate, false});
        badDate = dateXml;
        badDate.replace("2026-11-10T12:34:56.123456", "2026-11-10T12:34:56Z");
        seeds.push_back({"xml", "date-time-zone.xml", badDate, false});
        auto badDateProject = dateProject;
        badDateProject.tasks[0].attributes[0].value = "2026-02-29T12:00:00";
        seeds.push_back({"jobs", "date-time-leap.ppm", envelope(jobs::json::encodeProject(badDateProject), 40), false});
        badDateProject = dateProject;
        badDateProject.customFields.back().xmlLookup->values[0].value = "2026-11-10T18:00:01";
        seeds.push_back(
            {"jobs", "date-time-quantum.ppm", envelope(jobs::json::encodeProject(badDateProject), 40), false});
        auto externalFormula = jobs::test::xmlFormulaFixture();
        const auto formulaBody = jobs::json::encodeProject(externalFormula);
        seeds.push_back({"jobs", "external-formula.ppm", envelope(formulaBody, 44), true});
        auto scheduleFormula = externalFormula;
        auto &scheduledField = scheduleFormula.customFields[5];
        scheduledField.xmlFormula =
            jobs::bindXmlFormula(scheduleFormula, jobs::FieldScope::Task, "DateDiff(\"n\",[Start],[Finish])");
        scheduledField.xmlFormula->enabled = true;
        const auto xmlScheduleBody = jobs::json::encodeProject(scheduleFormula);
        check(jobs::decodeProjectFile(envelope(xmlScheduleBody, 45)) == scheduleFormula,
              "schema45 schedule reference roundtrip");
        seeds.push_back({"jobs", "schedule-formula.ppm", envelope(xmlScheduleBody, 45), true});
        seeds.push_back({"jobs", "schedule-formula-old44.ppm", envelope(xmlScheduleBody, 44), true});
        seeds.push_back({"xml", "schedule-formula.xml", jobs::exportProjectXml(scheduleFormula).xml, true});
        auto calendarFormula = jobs::test::xmlCalendarFixture();
        const auto calendarBody = jobs::json::encodeProject(calendarFormula);
        check(jobs::decodeProjectFile(envelope(calendarBody, 46)) == calendarFormula, "schema46 calendar roundtrip");
        seeds.push_back({"jobs", "calendar-formula.ppm", envelope(calendarBody, 46), true});
        seeds.push_back({"jobs", "calendar-formula-old45.ppm", envelope(calendarBody, 45), true});
        seeds.push_back({"xml", "calendar-formula.xml", jobs::exportProjectXml(calendarFormula).xml, true});
        calendarFormula.xmlFormulaCalendarId = "AM";
        seeds.push_back(
            {"jobs", "calendar-project-default.ppm", envelope(jobs::json::encodeProject(calendarFormula), 46), true});
        calendarFormula.xmlFormulaCalendarId = "MISSING";
        seeds.push_back(
            {"jobs", "calendar-missing-default.ppm", envelope(jobs::json::encodeProject(calendarFormula), 46), false});
        {
            auto actualFormula = jobs::test::xmlActualFixture();
            const auto xmlActualBody = jobs::json::encodeProject(actualFormula);
            check(jobs::decodeProjectFile(envelope(xmlActualBody, 47)) == actualFormula, "schema47 actual roundtrip");
            seeds.push_back({"jobs", "actual-formula.ppm", envelope(xmlActualBody, 47), true});
            seeds.push_back({"jobs", "actual-formula-old46.ppm", envelope(xmlActualBody, 46), true});
            seeds.push_back({"xml", "actual-formula.xml", jobs::exportProjectXml(actualFormula).xml, true});
            auto invalidActualFormula = actualFormula;
            invalidActualFormula.customFields.front().xmlFormula->references.front().scope = jobs::FieldScope::Resource;
            seeds.push_back({"jobs", "actual-invalid-scope.ppm",
                             envelope(jobs::json::encodeProject(invalidActualFormula), 47), false});
        }
        {
            auto quantities = jobs::test::xmlQuantityFixture();
            auto body = jobs::json::encodeProject(quantities);
            check(jobs::decodeProjectFile(envelope(body, 48)) == quantities, "schema48 quantity roundtrip");
            seeds.push_back({"jobs", "actual-quantities.ppm", envelope(body, 48), true});
            seeds.push_back({"jobs", "actual-quantities-old47.ppm", envelope(body, 47), true});
            seeds.push_back({"xml", "actual-quantities.xml", jobs::exportProjectXml(quantities).xml, true});
            auto &f = quantities.customFields[quantities.customFields.size() - 4];
            f.xmlFormula->references.front().scope = jobs::FieldScope::Resource;
            seeds.push_back({"jobs", "actual-duration-invalid-scope.ppm",
                             envelope(jobs::json::encodeProject(quantities), 48), false});
        }
        {
            auto remaining = jobs::test::xmlRemainingFixture();
            auto body = jobs::json::encodeProject(remaining);
            check(jobs::decodeProjectFile(envelope(body, 49)) == remaining, "schema49 remaining roundtrip");
            seeds.push_back({"jobs", "remaining-formula.ppm", envelope(body, 49), true});
            seeds.push_back({"jobs", "remaining-formula-old48.ppm", envelope(body, 48), true});
            seeds.push_back({"xml", "remaining-formula.xml", jobs::exportProjectXml(remaining).xml, true});
            remaining.customFields[0].xmlFormula->references.front().scope = jobs::FieldScope::Resource;
            seeds.push_back({"jobs", "remaining-duration-invalid-scope.ppm",
                             envelope(jobs::json::encodeProject(remaining), 49), false});
        }
        {
            auto baseline = jobs::test::xmlBaselineFixture();
            const auto body = jobs::json::encodeProject(baseline);
            check(jobs::decodeProjectFile(envelope(body, 50)) == baseline, "schema50 baseline roundtrip");
            seeds.push_back({"jobs", "baseline-formula.ppm", envelope(body, 50), true});
            seeds.push_back({"jobs", "baseline-formula-old49.ppm", envelope(body, 49), true});
            seeds.push_back({"xml", "baseline-formula.xml", jobs::exportProjectXml(baseline).xml, true});
            baseline.customFields[2].xmlFormula->references.front().scope = jobs::FieldScope::Resource;
            seeds.push_back({"jobs", "baseline-duration-invalid-scope.ppm",
                             envelope(jobs::json::encodeProject(baseline), 50), false});
            baseline = jobs::test::xmlBaselineFixture();
            baseline.baselines.front().schedule.pop_back();
            seeds.push_back(
                {"jobs", "baseline-missing-row.ppm", envelope(jobs::json::encodeProject(baseline), 50), false});
        }
        {
            auto timing = jobs::test::xmlEarlyLateFixture();
            const auto body = jobs::json::encodeProject(timing);
            check(jobs::decodeProjectFile(envelope(body, 51)) == timing, "schema51 Early/Late roundtrip");
            seeds.push_back({"jobs", "early-late-formula.ppm", envelope(body, 51), true});
            seeds.push_back({"jobs", "early-late-formula-old50.ppm", envelope(body, 50), true});
            seeds.push_back({"xml", "early-late-formula.xml", jobs::exportProjectXml(timing).xml, true});
            timing.customFields.front().xmlFormula->references.front().scope = jobs::FieldScope::Resource;
            seeds.push_back(
                {"jobs", "early-late-invalid-scope.ppm", envelope(jobs::json::encodeProject(timing), 51), false});
        }
        {
            auto extra = jobs::test::xmlAdditionalFunctionFixture();
            auto body = jobs::json::encodeProject(extra);
            check(jobs::decodeProjectFile(envelope(body, 52)) == extra, "schema52 additional functions roundtrip");
            bool oldRejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 51));
            } catch (const std::runtime_error &) {
                oldRejected = true;
            }
            check(oldRejected, "schema51 additional functions rejected");
            seeds.push_back({"jobs", "additional-functions.ppm", envelope(body, 52), true});
            seeds.push_back({"jobs", "additional-functions-old51.ppm", envelope(body, 51), true});
            seeds.push_back({"xml", "additional-functions.xml", jobs::exportProjectXml(extra).xml, true});
            extra.customFields[5].xmlFormula->source = "Sqr(1,2)";
            seeds.push_back({"jobs", "additional-functions-invalid-arity.ppm",
                             envelope(jobs::json::encodeProject(extra), 52), false});
        }
        {
            auto zoned = jobs::test::xmlTimeZoneFixture();
            const auto body = jobs::json::encodeProject(zoned);
            check(jobs::decodeProjectFile(envelope(body, 53)) == zoned, "schema53 timezone roundtrip");
            bool rejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 52));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "active timezone rejected under52");
            seeds.push_back({"jobs", "timezone.ppm", envelope(body, 53), true});
            seeds.push_back({"jobs", "timezone-old52.ppm", envelope(body, 52), true});
            seeds.push_back({"xml", "timezone.xml", jobs::exportProjectXml(zoned).xml, true});
            zoned.customFields[4].xmlFormula->source = "PPMTimeZone(1)";
            seeds.push_back(
                {"jobs", "timezone-invalid-arity.ppm", envelope(jobs::json::encodeProject(zoned), 53), false});
        }
        {
            auto bytes = jobs::test::xmlProgressFixture();
            seeds.push_back({"xml", "task-progress-source.xml", bytes, true});
            const auto plan = jobs::importProjectXml(bytes, "progress.xml");
            const auto adopted = jobs::adoptXmlProgress(plan, jobs::test::xmlProgressOptions(plan));
            seeds.push_back(
                {"jobs", "task-progress-adopted.ppm", envelope(jobs::json::encodeProject(adopted.project), 48), true});
            auto precise = bytes;
            precise.replace("2026-11-12T12:00:00</StatusDate>", "2026-11-12T12:00:15</StatusDate>");
            seeds.push_back({"xml", "task-progress-imprecise-status.xml", precise, true});
            auto invalid = adopted.project;
            invalid.progress[0].remainingMinutes = 1;
            seeds.push_back({"jobs", "task-progress-invalid-completion.ppm",
                             envelope(jobs::json::encodeProject(invalid), 48), false});
        }
        {
            const auto xml = jobs::test::xmlPeriodFixture();
            seeds.push_back({"xml", "assignment-daily-actuals.xml", xml, true});
            const auto source = jobs::importProjectXml(xml, "period.xml");
            auto native = jobs::test::xmlPeriodTarget();
            auto batches = jobs::prepareXmlPeriodMerge(native, source, jobs::test::xmlPeriodOptions(),
                                                       jobs::test::xmlPeriodMappings(source));
            int next = 0;
            for (auto &s : batches)
                jobs::appendPeriodActuals(native, std::move(s),
                                          [&] { return "xml-period-seed-" + std::to_string(++next); });
            ++native.revision;
            seeds.push_back(
                {"jobs", "assignment-daily-adopted.ppm", envelope(jobs::json::encodeProject(native), 11), true});
            native.costEntries.back().actualMinor = 0;
            seeds.push_back(
                {"jobs", "assignment-daily-unbalanced.ppm", envelope(jobs::json::encodeProject(native), 11), false});
        }
        {
            const auto xml = jobs::test::xmlPeriodFragmentFixture();
            seeds.push_back({"xml", "assignment-fragments.xml", xml, true});
            const auto source = jobs::importProjectXml(xml, "fragments.xml");
            auto native = jobs::test::xmlPeriodTarget();
            auto batches = jobs::prepareXmlPeriodMerge(native, source, jobs::test::xmlPeriodOptions(),
                                                       jobs::test::xmlPeriodMappings(source));
            int next = 0;
            for (auto &s : batches)
                jobs::appendPeriodActuals(native, std::move(s),
                                          [&] { return "xml-fragment-seed-" + std::to_string(++next); });
            ++native.revision;
            seeds.push_back(
                {"jobs", "assignment-fragments-adopted.ppm", envelope(jobs::json::encodeProject(native), 11), true});
            native.periodActuals.back().previousEntryId = "missing-source-entry";
            seeds.push_back({"jobs", "assignment-fragments-invalid-chain.ppm",
                             envelope(jobs::json::encodeProject(native), 11), false});
        }
        {
            const auto native = jobs::test::xmlActualExportFixture();
            const auto actuals = jobs::exportProjectXmlActuals(native).xml;
            seeds.push_back({"xml", "standard-actuals.xml", actuals, true});
            seeds.push_back(
                {"jobs", "standard-actuals-source.ppm", envelope(jobs::json::encodeProject(native), 11), true});
            auto broken = actuals;
            broken.replace("</Project>", "</Projects>");
            seeds.push_back({"xml", "standard-actuals-broken.xml", broken, false});
        }
        {
            const auto xml = jobs::test::xmlWorkProgressFixture();
            seeds.push_back({"xml", "work-progress.xml", xml, true});
            auto native = jobs::test::xmlWorkProgressTarget();
            const auto source = jobs::importProjectXml(xml, "work-progress.xml");
            auto rows = jobs::prepareXmlProgressMerge(native, source, jobs::test::xmlWorkProgressOptions(),
                                                      jobs::test::xmlWorkProgressMappings());
            int next = 0;
            for (auto &e : rows) {
                e.id = "work-progress-seed-" + std::to_string(++next);
                if (e.taskId == "NZ")
                    e.planDurationMinutes = 0;
                native.progress.push_back(std::move(e));
            }
            ++native.revision;
            seeds.push_back(
                {"jobs", "work-progress-merged.ppm", envelope(jobs::json::encodeProject(native), 24), true});
            native.progress.back().remainingWorkUnitMinutes = -1;
            seeds.push_back(
                {"jobs", "work-progress-invalid.ppm", envelope(jobs::json::encodeProject(native), 24), false});
        }
        {
            const auto xml = jobs::test::xmlProportionalFixture();
            seeds.push_back({"xml", "proportional-ranges.xml", xml, true});
            auto native = jobs::test::xmlProportionalTarget();
            const auto source = jobs::importProjectXml(xml, "multi.xml");
            const auto allocation = jobs::prepareXmlActualAllocation(
                native, source, jobs::test::xmlProportionalOptions(), jobs::test::xmlProportionalMappings(),
                {jobs::XmlAllocationBasis::WorkingIntersectionMinutes, true});
            int next = 0;
            for (const auto &s : allocation.batches)
                jobs::appendPeriodActuals(native, s, [&] { return "prorata-seed-" + std::to_string(++next); });
            ++native.revision;
            seeds.push_back(
                {"jobs", "proportional-adopted.ppm", envelope(jobs::json::encodeProject(native), 11), true});
            native.periodActuals.back().workUnitMinutes = -1;
            seeds.push_back(
                {"jobs", "proportional-invalid.ppm", envelope(jobs::json::encodeProject(native), 11), false});
        }
        const auto formulaXml = jobs::exportProjectXml(externalFormula).xml;
        seeds.push_back({"xml", "external-formula.xml", formulaXml, true});
        bool formulaOldRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(formulaBody, 43));
        } catch (const std::runtime_error &) {
            formulaOldRejected = true;
        }
        check(formulaOldRejected, "schema43 envelope accepted formula data");
        auto invalidFormula = externalFormula;
        invalidFormula.customFields[5].xmlFormula->enabled = true;
        invalidFormula.customFields[5].xmlFormula->source = "Run(1)";
        seeds.push_back(
            {"jobs", "formula-function.ppm", envelope(jobs::json::encodeProject(invalidFormula), 44), false});
        invalidFormula = externalFormula;
        invalidFormula.customFields[5].xmlFormula->references.clear();
        seeds.push_back(
            {"jobs", "formula-missing-binding.ppm", envelope(jobs::json::encodeProject(invalidFormula), 44), false});
        invalidFormula = externalFormula;
        invalidFormula.customFields[5].xmlFormula->references[0].scope = jobs::FieldScope::Assignment;
        seeds.push_back({"jobs", "formula-scope.ppm", envelope(jobs::json::encodeProject(invalidFormula), 44), false});
        invalidFormula = externalFormula;
        invalidFormula.customFields[5].expression = "1";
        seeds.push_back(
            {"jobs", "formula-two-engines.ppm", envelope(jobs::json::encodeProject(invalidFormula), 44), false});
        auto rollUp = jobs::test::xmlRollUpFixture();
        seeds.push_back({"jobs", "summary-roll-up.ppm", envelope(jobs::json::encodeProject(rollUp), 43), true});
        bool oldRollUpRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(jobs::json::encodeProject(rollUp), 42));
        } catch (const std::runtime_error &) {
            oldRollUpRejected = true;
        }
        check(oldRollUpRejected, "schema42 envelope accepted schema43 roll-up data");
        const auto rollUpXml = jobs::exportProjectXml(rollUp).xml;
        seeds.push_back({"xml", "summary-roll-up.xml", rollUpXml, true});
        auto badRollUpXml = rollUpXml;
        badRollUpXml.replace("<RollupType>7</RollupType>", "<RollupType>8</RollupType>");
        seeds.push_back({"xml", "roll-up-method.xml", badRollUpXml, false});
        badRollUpXml = rollUpXml;
        badRollUpXml.replace("<CalculationType>1</CalculationType>", "<CalculationType>3</CalculationType>");
        seeds.push_back({"xml", "roll-up-calculation.xml", badRollUpXml, false});
        auto badRollUp = rollUp;
        badRollUp.customFields[0].xmlRollUp->rollupType = 8;
        seeds.push_back({"jobs", "roll-up-method.ppm", envelope(jobs::json::encodeProject(badRollUp), 43), false});
        badRollUp = rollUp;
        badRollUp.customFields.back().xmlRollUp = jobs::XmlRollUp{1, 3};
        seeds.push_back({"jobs", "roll-up-text.ppm", envelope(jobs::json::encodeProject(badRollUp), 43), false});
        badRollUp = rollUp;
        badRollUp.taskSummaryAttributes.push_back(badRollUp.taskSummaryAttributes[0]);
        seeds.push_back({"jobs", "roll-up-duplicate.ppm", envelope(jobs::json::encodeProject(badRollUp), 43), false});
        badRollUp = rollUp;
        badRollUp.wbsNodes[0].attributes.push_back({"field:unknown", "1"});
        seeds.push_back({"jobs", "roll-up-unknown.ppm", envelope(jobs::json::encodeProject(badRollUp), 43), false});
        auto rollDownProject = jobs::test::xmlRollDownFixture();
        const auto rollDownXml = jobs::exportProjectXml(rollDownProject).xml;
        const auto rollDownBody = jobs::json::encodeProject(rollDownProject);
        seeds.push_back({"xml", "assignment-roll-down.xml", rollDownXml, true});
        seeds.push_back({"jobs", "assignment-roll-down.ppm", envelope(rollDownBody, 42), true});
        bool oldRollDownRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(rollDownBody, 41));
        } catch (const std::runtime_error &) {
            oldRollDownRejected = true;
        }
        check(oldRollDownRejected, "roll-down accepted in schema41 envelope");
        auto rollDownBadXml = rollDownXml;
        rollDownBadXml.replace("<AutoRollDown>1</AutoRollDown>", "<AutoRollDown>2</AutoRollDown>");
        seeds.push_back({"xml", "roll-down-boolean.xml", rollDownBadXml, false});
        rollDownBadXml = rollDownXml;
        rollDownBadXml.replace("<SecondaryPID>255885312</SecondaryPID>", "<SecondaryPID>188743731</SecondaryPID>");
        seeds.push_back({"xml", "roll-down-pid.xml", rollDownBadXml, false});
        rollDownBadXml = rollDownXml;
        rollDownBadXml.replace("<AutoRollDown>1</AutoRollDown>", "<AutoRollDown>0</AutoRollDown>");
        seeds.push_back({"xml", "roll-down-disabled.xml", rollDownBadXml, true});
        auto rollDownBad = rollDownProject;
        rollDownBad.customFields[0].xmlRollDown->assignmentFieldId = "unknown";
        seeds.push_back({"jobs", "roll-down-target.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.customFields[0].xmlRollDown->assignmentFieldId = "target1";
        seeds.push_back({"jobs", "roll-down-type.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.assignmentFields[0].xmlRollDown = jobs::XmlRollDown{"target0", true};
        seeds.push_back({"jobs", "roll-down-scope.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.customFields[0].xmlRollDown->assignmentFieldId.clear();
        seeds.push_back(
            {"jobs", "roll-down-no-target.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.assignments[0].rollDownMissing.push_back("field:target0");
        seeds.push_back(
            {"jobs", "roll-down-missing-conflict.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.assignments[0].rollDownMissing.push_back("field:target2");
        seeds.push_back(
            {"jobs", "roll-down-missing-duplicate.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        rollDownBad = rollDownProject;
        rollDownBad.customFields[1].xmlRollDown = rollDownBad.customFields[0].xmlRollDown;
        seeds.push_back(
            {"jobs", "roll-down-owner-conflict.ppm", envelope(jobs::json::encodeProject(rollDownBad), 42), false});
        auto maskedXmlProject = jobs::test::xmlCodeFixture();
        const auto codeXml = jobs::exportProjectXml(maskedXmlProject).xml;
        const auto codeBody = jobs::json::encodeProject(maskedXmlProject);
        seeds.push_back({"xml", "code-masks.xml", codeXml, true});
        seeds.push_back({"jobs", "code-masks.ppm", envelope(codeBody, 41), true});
        bool oldCodeRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(codeBody, 40));
        } catch (const std::runtime_error &) {
            oldCodeRejected = true;
        }
        check(oldCodeRejected, "code masks accepted in schema40 envelope");
        auto invalidCodeXml = codeXml;
        invalidCodeXml.replace("<Length>2</Length>", "<Length>256</Length>");
        seeds.push_back({"xml", "code-mask-length.xml", invalidCodeXml, false});
        invalidCodeXml = codeXml;
        invalidCodeXml.replace("<Level>1</Level>", "<Level>2</Level>");
        seeds.push_back({"xml", "code-mask-level.xml", invalidCodeXml, false});
        auto invalidCodes = maskedXmlProject;
        invalidCodes.xmlWbs->values[1].code = "PR-AA";
        seeds.push_back(
            {"jobs", "wbs-code-duplicate.ppm", envelope(jobs::json::encodeProject(invalidCodes), 41), false});
        invalidCodes = maskedXmlProject;
        invalidCodes.customFields[0].xmlLookup->masks[0].type = 4;
        seeds.push_back({"jobs", "code-mask-type.ppm", envelope(jobs::json::encodeProject(invalidCodes), 41), false});
        invalidCodes = maskedXmlProject;
        invalidCodes.xmlWbs->values[0].id = "unknown";
        seeds.push_back({"jobs", "wbs-code-owner.ppm", envelope(jobs::json::encodeProject(invalidCodes), 41), false});
        invalidCodeXml = codeXml;
        invalidCodeXml.replace("<Separator>.</Separator>", "<Separator>..</Separator>");
        seeds.push_back({"xml", "code-mask-separator.xml", invalidCodeXml, false});
        auto typesProject = enterprise;
        typesProject.currency = "USD";
        typesProject.customFields = {{"flag", "Flag", jobs::CustomFieldType::Flag, {}, {}},
                                     {"cost", "Cost", jobs::CustomFieldType::Cost, {}, {}},
                                     {"duration", "Duration", jobs::CustomFieldType::Duration, {}, {}}};
        typesProject.tasks[0].attributes = {
            {"field:flag", "0"}, {"field:cost", "-12.34"}, {"field:duration", "90.5 em?"}};
        const auto typesXml = jobs::exportProjectXml(typesProject).xml;
        const auto typesBody = jobs::json::encodeProject(typesProject);
        seeds.push_back({"xml", "additional-types.xml", typesXml, true});
        seeds.push_back({"jobs", "additional-types.ppm", envelope(typesBody, 39), true});
        bool oldTypesRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(typesBody, 38));
        } catch (const std::runtime_error &) {
            oldTypesRejected = true;
        }
        check(oldTypesRejected, "additional types accepted in schema38 envelope");
        auto badTypeXml = typesXml;
        badTypeXml.replace("<Value>-1234</Value>", "<Value>-1234.1</Value>");
        seeds.push_back({"xml", "cost-precision.xml", badTypeXml, false});
        badTypeXml = typesXml;
        badTypeXml.replace("<DurationFormat>36</DurationFormat>", "<DurationFormat>19</DurationFormat>");
        seeds.push_back({"xml", "duration-percent.xml", badTypeXml, false});
        badTypeXml = typesXml;
        badTypeXml.replace("<Value>PT1H30M30S</Value>", "<Value>P1M</Value>");
        seeds.push_back({"xml", "duration-month.xml", badTypeXml, false});
        auto badTypes = typesProject;
        badTypes.tasks[0].attributes[0].value = "2";
        seeds.push_back({"jobs", "flag-invalid.ppm", envelope(jobs::json::encodeProject(badTypes), 39), false});
        auto lookupProject = enterprise;
        jobs::CustomField lookupField{
            "lookup", "Lookup", jobs::CustomFieldType::Choice, {}, {{"a", "Alpha"}, {"b", "Beta"}}};
        jobs::XmlLookup lookup;
        lookup.tableGuid = "11111111-2222-4333-8444-555555555555";
        lookup.maxValues = 2;
        lookup.values = {{"a", "101", "11111111-2222-4333-8444-555555555551", "Alpha", {}, {}, true},
                         {"b", "102", "11111111-2222-4333-8444-555555555552", "Beta", {}, {}, true}};
        lookupField.xmlLookup = lookup;
        lookupProject.assignmentFields = {lookupField};
        lookupProject.assignments[0].attributes = {{"field:lookup", jobs::choiceValue(lookupField, {"a", "b"})}};
        const auto lookupXml = jobs::exportProjectXml(lookupProject).xml;
        seeds.push_back({"xml", "lookup.xml", lookupXml, true});
        seeds.push_back({"jobs", "lookup.ppm", envelope(jobs::json::encodeProject(lookupProject), 38), true});
        auto badLookup = lookupXml;
        badLookup.replace("<ValueID>102</ValueID>", "<ValueID>101</ValueID>");
        seeds.push_back({"xml", "lookup-duplicate-id.xml", badLookup, false});
        badLookup = lookupXml;
        badLookup.replace("<FieldValueGUID>11111111-2222-4333-8444-555555555552</FieldValueGUID>",
                          "<FieldValueGUID>11111111-2222-4333-8444-555555555559</FieldValueGUID>");
        seeds.push_back({"xml", "lookup-foreign-guid.xml", badLookup, false});
        badLookup = lookupXml;
        badLookup.replace("<MaxMultiValues>2</MaxMultiValues>", "<MaxMultiValues>1</MaxMultiValues>");
        seeds.push_back({"xml", "lookup-single-overflow.xml", badLookup, false});
        auto badLookupProject = lookupProject;
        badLookupProject.assignmentFields[0].xmlLookup->values[0].parentId = "b";
        badLookupProject.assignmentFields[0].xmlLookup->values[1].parentId = "a";
        seeds.push_back({"jobs", "lookup-cycle.ppm", envelope(jobs::json::encodeProject(badLookupProject), 38), false});
        auto udfXml = jobs::exportProjectXml(extended).xml;
        auto udfXer = jobs::exportPrimaveraXer(extended).xer;
        seeds.push_back({"xml", "udf-values.xml", udfXml, true});
        seeds.push_back({"xer", "udf-values.xer", udfXer, true});
        udfXml.replace("<CFType>5</CFType>", "<CFType>7</CFType>");
        seeds.push_back({"xml", "udf-type-mismatch.xml", udfXml, false});
        const auto udfTable = udfXer.indexOf("%T\tUDFVALUE");
        const auto udfRow = udfXer.indexOf("%R\t", udfTable);
        const auto udfEnd = udfXer.indexOf('\n', udfRow);
        auto udfCells = udfXer.mid(udfRow, udfEnd - udfRow).split('\t');
        udfCells[2] = "999999";
        udfXer.replace(udfRow, udfEnd - udfRow, udfCells.join('\t'));
        seeds.push_back({"xer", "udf-owner-mismatch.xer", udfXer, false});
        auto workCurve = minimal;
        workCurve.calculationProfile = jobs::effortProfile;
        workCurve.resources = extended.resources;
        for (auto &resource : workCurve.resources)
            resource.attributes.clear();
        workCurve.tasks[0].taskType = jobs::TaskType::FixedWork;
        workCurve.tasks[0].workDriven = true;
        workCurve.tasks[0].workUnitMinutes = 6000;
        workCurve.assignments = {{workCurve.tasks[0].id, workCurve.resources[0].id, 100, {{0, 30, 50}, {30, 60, 150}}}};
        auto workBody = jobs::json::encodeProject(workCurve);
        check(jobs::decodeProjectFile(envelope(workBody, 24)) == workCurve, "work curve schema24");
        bool workOldRejected = false;
        try {
            (void)jobs::decodeProjectFile(envelope(workBody, 23));
        } catch (const std::runtime_error &) {
            workOldRejected = true;
        }
        check(workOldRejected, "schema23 accepted work contours");
        seeds.push_back({"jobs", "fixed-work-contour.ppm", envelope(workBody, 24), true});
        workCurve.tasks[0].taskType = jobs::TaskType::FixedUnits;
        seeds.push_back(
            {"jobs", "fixed-units-work-contour.ppm", envelope(jobs::json::encodeProject(workCurve), 24), true});
        workCurve.tasks[0].workDriven = false;
        workCurve.tasks[0].workUnitMinutes.reset();
        seeds.push_back(
            {"jobs", "fixed-units-duration-contour.ppm", envelope(jobs::json::encodeProject(workCurve), 24), true});
        workCurve.tasks[0].taskType = jobs::TaskType::FixedWork;
        seeds.push_back(
            {"jobs", "fixed-work-missing-work.ppm", envelope(jobs::json::encodeProject(workCurve), 24), false});
        workCurve.tasks[0].workDriven = true;
        workCurve.tasks[0].workUnitMinutes = 6000;
        workCurve.assignments[0].periods[0].toMinute = 31;
        seeds.push_back(
            {"jobs", "work-contour-overlap.ppm", envelope(jobs::json::encodeProject(workCurve), 24), false});
        auto identity = minimal;
        jobs::ProgressEntry observed;
        observed.id = "identity-observation";
        observed.taskId = "T1";
        observed.statusAt = "2026-10-26 09:00";
        observed.recordedAt = "2026-11-08T10:00:00Z";
        observed.reporter = "PM";
        observed.reason = "Identity seed";
        observed.sourceDocumentId = identity.documentId;
        observed.recordedRevision = ++identity.revision;
        observed.remainingMinutes = 60;
        identity.statusAt = observed.statusAt;
        identity.progress.push_back(observed);
        auto identityAfter = jobs::migrateLedgerIdentities(identity, {{"task", "T1", "new-task"}});
        jobs::LedgerIdMigration migration;
        migration.links = {{"task", "T1", "new-task"}};
        migration.id = "identity-migration";
        migration.sourceDocumentId = identity.documentId;
        migration.recordedRevision = identityAfter.revision;
        migration.reporter = "PM";
        migration.reason = "Identity seed";
        migration.recordedAt = "2026-11-08T11:00:00Z";
        migration.confirmed = true;
        migration.before = std::make_shared<const jobs::Project>(identity);
        migration.after = std::make_shared<const jobs::Project>(identityAfter);
        identityAfter.ledgerIdMigrations.push_back(migration);
        const auto identityBody = jobs::json::encodeProject(identityAfter);
        check(jobs::decodeProjectFile(envelope(identityBody, 33)) == identityAfter, "identity schema33");
        seeds.push_back({"jobs", "ledger-identity.ppm", envelope(identityBody, 33), true});
        for (int variant = 0; variant < 7; ++variant) {
            auto body = identityBody;
            auto events = body["ledgerIdMigrations"].toArray();
            auto event = events[0].toObject();
            if (variant == 0)
                event["policy"] = "unknown";
            if (variant == 1)
                event["confirmed"] = false;
            if (variant == 2)
                event["links"] = QJsonArray{};
            if (variant == 3)
                event["recordedRevision"] = 2.5;
            if (variant == 4) {
                auto before = event["before"].toObject();
                before["ledgerIdMigrations"] = QJsonArray{};
                event["before"] = before;
            }
            if (variant == 5) {
                auto after = event["after"].toObject();
                after["revision"] = 1;
                event["after"] = after;
            }
            if (variant == 6)
                event["unexpected"] = true;
            events[0] = event;
            body["ledgerIdMigrations"] = events;
            seeds.push_back(
                {"jobs", QString("ledger-identity-invalid-%1.ppm").arg(variant), envelope(body, 33), false});
        }
        auto moneySeed = identity;
        moneySeed.taskCosts = {{"T1", 1, "uniform", "physical"}};
        jobs::CurrencyRate rate{"KRW", "USD", "2026-11-08", "Parser currency seed", "half-away-from-zero-v1", 1, 200};
        auto converted = jobs::convertProjectCurrency(moneySeed, rate);
        jobs::CurrencyConversion event;
        event.rate = rate;
        event.id = "currency-conversion";
        event.sourceDocumentId = moneySeed.documentId;
        event.recordedRevision = converted.revision;
        event.reporter = "PM";
        event.reason = "Currency seed";
        event.recordedAt = "2026-11-08T12:00:00Z";
        event.confirmed = true;
        event.before = std::make_shared<const jobs::Project>(moneySeed);
        event.after = std::make_shared<const jobs::Project>(converted);
        converted.currencyConversions.push_back(event);
        const auto currencyBody = jobs::json::encodeProject(converted);
        check(jobs::decodeProjectFile(envelope(currencyBody, 34)) == converted, "currency schema34 replay");
        seeds.push_back({"jobs", "project-currency.ppm", envelope(currencyBody, 34), true});
        for (int variant = 0; variant < 10; ++variant) {
            auto body = currencyBody;
            auto events = body["currencyConversions"].toArray();
            auto e = events[0].toObject();
            auto rate = e["rate"].toObject();
            if (variant == 0)
                rate["numerator"] = 0;
            if (variant == 1)
                rate["denominator"] = 1.5;
            if (variant == 2)
                rate["fromCurrency"] = "EUR";
            if (variant == 3)
                rate["rounding"] = "future";
            if (variant == 4)
                e["confirmed"] = false;
            if (variant == 5)
                e["reporter"] = " ";
            if (variant == 6)
                e["unexpected"] = true;
            if (variant == 7) {
                auto a = e["before"].toObject();
                a["currencyConversions"] = QJsonArray{};
                e["before"] = a;
            }
            if (variant == 8) {
                auto a = e["after"].toObject();
                a["ledgerIdMigrations"] = QJsonArray{};
                e["after"] = a;
            }
            if (variant == 9)
                body["currency"] = "JPY";
            e["rate"] = rate;
            events[0] = e;
            body["currencyConversions"] = events;
            seeds.push_back(
                {"jobs", QString("project-currency-invalid-%1.ppm").arg(variant), envelope(body, 34), false});
        }
        auto dated = minimal;
        dated.calculationProfile = jobs::effortProfile;
        dated.tasks[0].taskType = jobs::TaskType::FixedWork;
        dated.tasks[0].workDriven = true;
        dated.tasks[0].workUnitMinutes = 48000;
        jobs::Resource worker;
        worker.id = "R";
        worker.name = "Worker";
        dated.resources = {worker};
        dated.assignments = {{"T1", "R", 100}};
        dated.statusAt = "2026-10-26 09:00";
        dated.remainingWork = {{"T1", "R", *dated.statusAt, {{"2026-10-27", 24000}}}};
        for (auto type : {jobs::TaskType::FixedWork, jobs::TaskType::FixedUnits}) {
            dated.tasks[0].taskType = type;
            const auto body = jobs::json::encodeProject(dated);
            check(jobs::decodeProjectFile(envelope(body, 35)) == dated, "dated effort schema35");
            seeds.push_back({"jobs", QString("dated-effort-%1.ppm").arg(int(type)), envelope(body, 35), true});
            // Old readers must not silently apply fixed-duration dated semantics.
            bool oldRejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 34));
            } catch (const jobs::AppError &) {
                oldRejected = true;
            }
            check(oldRejected, "dated effort accepted schema34");
            for (int variant = 0; variant < 5; ++variant) {
                auto invalid = body;
                auto plans = invalid["remainingWork"].toArray();
                auto plan = plans[0].toObject();
                auto days = plan["days"].toArray();
                auto day = days[0].toObject();
                if (variant == 0)
                    day["workUnitMinutes"] = -1;
                if (variant == 1)
                    day["workUnitMinutes"] = 0.5;
                if (variant == 2)
                    day["date"] = "2026-10-25";
                if (variant == 3)
                    plan["resourceId"] = "missing";
                if (variant == 4)
                    day["unexpected"] = true;
                days[0] = day;
                plan["days"] = days;
                plans[0] = plan;
                invalid["remainingWork"] = plans;
                seeds.push_back({"jobs", QString("dated-effort-%1-invalid-%2.ppm").arg(int(type)).arg(variant),
                                 envelope(invalid, 35), false});
            }
        }
        auto productive = minimal;
        productive.calculationProfile = jobs::effortProfile;
        jobs::Resource productiveResource;
        productiveResource.id = "R";
        productiveResource.name = "Measured resource";
        productiveResource.productivity = jobs::ProductivityRate{"m3", "2026-11-09", "Measured basis", "PM", 1000};
        productive.resources = {productiveResource};
        productive.assignments = {{"T1", "R", 100}};
        productive = jobs::applyNativeCurve(productive, "T1", "R", jobs::mspWorkContour(6));
        productive.assignments[0].productivityEstimate = jobs::ProductivityEstimate{*productiveResource.productivity,
                                                                                    1000,
                                                                                    6000,
                                                                                    "Source estimate",
                                                                                    "2026-11-09T00:00:00Z",
                                                                                    true,
                                                                                    "duration-rates-v1",
                                                                                    "PM"};
        const auto productiveBody = jobs::json::encodeProject(productive);
        check(jobs::decodeProjectFile(envelope(productiveBody, 36)) == productive, "resource planning schema36");
        bool oldResourcePlanning = false;
        try {
            jobs::decodeProjectFile(envelope(productiveBody, 35));
        } catch (const jobs::AppError &) {
            oldResourcePlanning = true;
        }
        check(oldResourcePlanning, "schema35 admitted resource planning metadata");
        seeds.push_back({"jobs", "resource-planning.ppm", envelope(productiveBody, 36), true});
        for (int variant = 0; variant < 12; ++variant) {
            auto body = productiveBody;
            auto resources = body["resources"].toArray();
            auto r = resources[0].toObject();
            auto rate = r["productivity"].toObject();
            auto assignments = body["assignments"].toArray();
            auto a = assignments[0].toObject();
            auto curve = a["nativeCurve"].toObject();
            auto estimate = a["productivityEstimate"].toObject();
            if (variant == 0)
                rate["quantityMilliPerHour"] = 0;
            if (variant == 1)
                rate["unit"] = " ";
            if (variant == 2)
                rate["unexpected"] = true;
            if (variant == 3)
                estimate["workUnitMinutes"] = 6001;
            if (variant == 4)
                estimate["confirmed"] = false;
            if (variant == 5)
                estimate["application"] = "future";
            if (variant == 6)
                curve["durationMinutes"] = 0;
            if (variant == 7)
                curve["mspCode"] = 9;
            if (variant == 8) {
                auto weights = curve["weights"].toArray();
                weights[0] = 1;
                curve["weights"] = weights;
            }
            if (variant == 9) {
                auto periods = curve["projectedPeriods"].toArray();
                auto p = periods[0].toObject();
                p["toMinute"] = 0;
                periods[0] = p;
                curve["projectedPeriods"] = periods;
            }
            if (variant == 10)
                estimate["reporter"] = "";
            if (variant == 11)
                curve["weights"] = QJsonArray{0, 10000};
            r["productivity"] = rate;
            resources[0] = r;
            body["resources"] = resources;
            a["nativeCurve"] = curve;
            a["productivityEstimate"] = estimate;
            assignments[0] = a;
            body["assignments"] = assignments;
            seeds.push_back(
                {"jobs", QString("resource-planning-invalid-%1.ppm").arg(variant), envelope(body, 36), false});
        }
        seeds.push_back({"xml", "native-curves.xml", jobs::exportProjectXml(productive).xml, true});
        seeds.push_back({"xer", "native-curves.xer", jobs::exportPrimaveraXer(productive).xer, true});
        auto externalBaseline = minimal;
        jobs::XmlBaselineSet external;
        external.sourceName = "external-baseline.xml";
        external.sourceSha256 = std::string(64, 'a');
        external.reporter = "PM";
        external.reason = "schema75 seed";
        external.recordedAt = "2026-11-15T09:00:00Z";
        external.weekdays = {1, 2, 3, 4, 5};
        external.periods = {{540, 720}, {780, 1080}};
        jobs::XmlBaselineRow externalRow;
        externalRow.kind = "task";
        externalRow.id = minimal.tasks.front().id;
        externalRow.xmlUid = "1";
        externalRow.number = 0;
        externalRow.duration = 1000000;
        externalRow.work = 0;
        externalRow.cost = 0;
        external.rows.push_back(externalRow);
        externalBaseline.xmlBaselines = external;
        const auto externalBody = jobs::json::encodeProject(externalBaseline);
        check(jobs::decodeProjectFile(envelope(externalBody, 75)) == externalBaseline, "schema75 data roundtrip");
        seeds.push_back({"jobs", "xml-baseline-data75.ppm", envelope(externalBody, 75), true});
        for (int variant = 0; variant < 4; ++variant) {
            auto body = externalBody;
            auto data = body["xmlBaselines"].toObject();
            auto rows = data["rows"].toArray();
            if (variant == 0)
                rows.append(rows.first());
            if (variant == 1) {
                auto row = rows.first().toObject();
                row["cost"] = -1;
                rows[0] = row;
            }
            if (variant == 2)
                data["sourceSha256"] = "bad";
            if (variant == 3)
                data["unexpected"] = true;
            data["rows"] = rows;
            body["xmlBaselines"] = data;
            seeds.push_back(
                {"jobs", QString("xml-baseline-data-invalid-%1.ppm").arg(variant), envelope(body, 75), false});
        }
        {
            jobs::XmlBaselineMergeSubmission merge;
            merge.incoming = external;
            merge.currency = minimal.currency;
            merge.links = {{"task", externalRow.id, externalRow.id, false}};
            merge.confirmed = true;
            jobs::XmlBaselineMerge event;
            static_cast<jobs::XmlBaselineMergeSubmission &>(event) = merge;
            event.id = "seed-xml-baseline-merge";
            event.sourceDocumentId = minimal.documentId;
            event.recordedRevision = minimal.revision + 1;
            event.after = jobs::mergeXmlBaselineSets({}, merge);
            auto merged = minimal;
            merged.revision = event.recordedRevision;
            merged.xmlBaselines = event.after;
            merged.xmlBaselineMerges.push_back(event);
            auto mergeBody = jobs::json::encodeProject(merged);
            check(jobs::decodeProjectFile(envelope(mergeBody, 76)) == merged, "schema76 merge roundtrip");
            bool downgradeRejected = false;
            try {
                jobs::decodeProjectFile(envelope(mergeBody, 75));
            } catch (const std::runtime_error &) {
                downgradeRejected = true;
            }
            check(downgradeRejected, "schema75 envelope rejects baseline merge");
            seeds.push_back({"jobs", "xml-baseline-merge76.ppm", envelope(mergeBody, 76), true});
            for (int variant = 0; variant < 5; ++variant) {
                auto body = mergeBody;
                auto events = body["xmlBaselineMerges"].toArray();
                auto e = events[0].toObject();
                if (variant == 0)
                    e["recordedRevision"] = 0;
                if (variant == 1) {
                    auto after = e["after"].toObject();
                    auto rows = after["rows"].toArray();
                    auto row = rows[0].toObject();
                    row["work"] = 42;
                    rows[0] = row;
                    after["rows"] = rows;
                    e["after"] = after;
                }
                if (variant == 2) {
                    auto data = body["xmlBaselines"].toObject();
                    auto origins = data["origins"].toArray();
                    origins.append(origins[0]);
                    data["origins"] = origins;
                    body["xmlBaselines"] = data;
                }
                if (variant == 3)
                    e["confirmed"] = false;
                if (variant == 4)
                    e["unexpected"] = true;
                events[0] = e;
                body["xmlBaselineMerges"] = events;
                seeds.push_back(
                    {"jobs", QString("xml-baseline-merge-invalid-%1.ppm").arg(variant), envelope(body, 76), false});
            }
        }
        {
            auto imported = externalBaseline;
            auto &row = imported.xmlBaselines->rows[0];
            row.work = 1000000;
            row.timephasedOutside = true;
            row.timephased = {{9, "1", "2026-11-16T09:00:00", "2026-11-16T09:01:00", "PT1M", 0}};
            const auto body = jobs::json::encodeProject(imported);
            check(jobs::decodeProjectFile(envelope(body, 77)) == imported, "schema77 timephased placement roundtrip");
            bool rejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 76));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "schema76 envelope rejects parent-level baseline timephased data");
            seeds.push_back({"jobs", "xml-baseline-outside77.ppm", envelope(body, 77), true});
            for (int variant = 0; variant < 3; ++variant) {
                auto bad = body;
                auto data = bad["xmlBaselines"].toObject();
                auto rows = data["rows"].toArray();
                auto r = rows[0].toObject();
                if (variant == 0)
                    r["timephasedOutside"] = "yes";
                auto times = r["timephased"].toArray();
                auto t = times[0].toObject();
                if (variant == 1)
                    t["type"] = 7;
                if (variant == 2)
                    t["value"] = "PT-1M";
                times[0] = t;
                r["timephased"] = times;
                rows[0] = r;
                data["rows"] = rows;
                bad["xmlBaselines"] = data;
                seeds.push_back(
                    {"jobs", QString("xml-baseline-outside-invalid-%1.ppm").arg(variant), envelope(bad, 77), false});
            }
        }
        {
            auto units = minimal;
            units.assignmentFields.push_back({"units", "units", jobs::CustomFieldType::Number, "", {}});
            units.assignmentFields.back().xmlFormula =
                jobs::bindXmlFormula(units, jobs::FieldScope::Assignment, "[Units]+[Max Units]");
            units.assignmentFields.back().xmlFormula->enabled = true;
            units.resourceFields.push_back({"day", "day", jobs::CustomFieldType::Number, "", {}});
            units.resourceFields.back().xmlFormula =
                jobs::bindXmlFormula(units, jobs::FieldScope::Resource, "PPMUnitAvailability(#2026-11-17#)");
            units.resourceFields.back().xmlFormula->enabled = true;
            const auto body = jobs::json::encodeProject(units);
            check(jobs::decodeProjectFile(envelope(body, 78)) == units, "schema78 unit references roundtrip");
            bool rejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 77));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "schema77 envelope rejects unit references");
            seeds.push_back({"jobs", "xml-units78.ppm", envelope(body, 78), true});
            for (int variant = 0; variant < 3; ++variant) {
                auto bad = body;
                auto fields = bad["assignmentFields"].toArray();
                auto field = fields[0].toObject();
                auto formula = field["xmlFormula"].toObject();
                auto refs = formula["references"].toArray();
                auto ref = refs[1].toObject();
                if (variant == 0)
                    ref["scope"] = 1; // Max Units is a resource reference.
                if (variant == 1)
                    ref["key"] = "builtin:maxunit";
                if (variant == 2)
                    ref["scope"] = 9;
                refs[1] = ref;
                formula["references"] = refs;
                field["xmlFormula"] = formula;
                fields[0] = field;
                bad["assignmentFields"] = fields;
                seeds.push_back({"jobs", QString("xml-units-invalid-%1.ppm").arg(variant), envelope(bad, 78), false});
            }
        }
        {
            auto capacity = minimal;
            capacity.resources.push_back({"capacity", "Capacity", "labor", 100, {}});
            capacity.resources.back().availableFrom = "2026-11-16 09:00";
            capacity.resources.back().availability = {{{}, "2026-11-16 11:59", 50}, {"2026-11-16 12:00", {}, {}}};
            const auto body = jobs::json::encodeProject(capacity);
            check(jobs::decodeProjectFile(envelope(body, 79)) == capacity, "schema79 availability roundtrip");
            bool rejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 78));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "old envelope rejects availability periods");
            seeds.push_back({"jobs", "availability79.ppm", envelope(body, 79), true});
            for (int variant = 0; variant < 4; ++variant) {
                auto bad = body;
                auto rows = bad["resources"].toArray();
                auto resource = rows[0].toObject();
                auto periods = resource["availability"].toArray();
                auto period = periods[1].toObject();
                if (variant == 0)
                    period["from"] = "2026-11-16 11:59";
                if (variant == 1)
                    period["units"] = -1;
                if (variant == 2)
                    period["to"] = "2026-11-16T12:00:00";
                if (variant == 3)
                    period.remove("units");
                periods[1] = period;
                resource["availability"] = periods;
                rows[0] = resource;
                bad["resources"] = rows;
                seeds.push_back(
                    {"jobs", QString("availability-invalid-%1.ppm").arg(variant), envelope(bad, 79), false});
            }
        }
        {
            auto load = minimal;
            load.resourceFields.push_back({"remaining", "remaining", jobs::CustomFieldType::Number, "", {}});
            load.resourceFields.back().xmlFormula = jobs::bindXmlFormula(
                load, jobs::FieldScope::Resource,
                "PPMRemainingAvailability(#2026-11-16T09:30:00#)+PPMResourceLoad(#2026-11-16T09:30:00#)");
            load.resourceFields.back().xmlFormula->enabled = true;
            const auto body = jobs::json::encodeProject(load);
            check(jobs::decodeProjectFile(envelope(body, 80)) == load, "schema80 load formula roundtrip");
            bool rejected = false;
            try {
                jobs::decodeProjectFile(envelope(body, 79));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "old schema rejects active load functions");
            seeds.push_back({"jobs", "resource-load80.ppm", envelope(body, 80), true});
            auto bad = body;
            auto fields = bad["resourceFields"].toArray();
            auto field = fields[0].toObject();
            auto formula = field["xmlFormula"].toObject();
            formula["source"] = "PPMResourceLoad()";
            field["xmlFormula"] = formula;
            fields[0] = field;
            bad["resourceFields"] = fields;
            seeds.push_back({"jobs", "resource-load-arity.ppm", envelope(bad, 80), false});
        }
        {
            auto inventory = minimal;
            inventory.resourcePlan.materials = {{"M", "Material", "kg", 0, 1000}};
            inventory.resourcePlan.materials[0].stockAt = inventory.startDate + " 09:00";
            inventory.resourcePlan.receipts = {{"receipt", "M", inventory.startDate + " 10:00", 1000}};
            const auto body = jobs::json::encodeProject(inventory);
            check(jobs::decodeProjectFile(envelope(body, 81)) == inventory, "inventory schema81 parity");
            seeds.push_back({"jobs", "inventory81.ppm", envelope(body, 81), true});
            // The fuzz harness also probes the raw body under every schema, including81.
            // Enforce the envelope's declared version separately.
            bool rejected = false;
            try {
                (void)jobs::decodeProjectFile(envelope(body, 80));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "old schema rejects material inventory");
            for (int variant = 0; variant < 7; ++variant) {
                auto bad = body;
                auto plan = bad["resourcePlan"].toObject();
                auto rows = plan["receipts"].toArray();
                auto row = rows[0].toObject();
                if (variant == 0)
                    row["quantityMilli"] = 1.5;
                if (variant == 1)
                    row["quantityMilli"] = 0;
                if (variant == 2)
                    row["materialId"] = "missing";
                if (variant == 3)
                    row["at"] = "bad";
                if (variant == 4)
                    row["unknown"] = true;
                if (variant == 5)
                    row.remove("at");
                if (variant == 6)
                    row["at"] = QString::fromStdString(inventory.startDate + " 08:59");
                rows[0] = row;
                plan["receipts"] = rows;
                bad["resourcePlan"] = plan;
                seeds.push_back({"jobs", QString("inventory-invalid-%1.ppm").arg(variant), envelope(bad, 81), false});
            }
        }
        {
            auto automatic = minimal;
            automatic.resourcePlan.autoDelayForReceipts = true;
            automatic.resourcePlan.materials = {{"M", "Material", "kg", 0, 0}};
            automatic.resourcePlan.receipts = {{"receipt", "M", automatic.startDate + " 10:00", 1000}};
            automatic.resourcePlan.uses = {{"use", automatic.tasks.front().id, "M", 1000, "start"}};
            const auto body = jobs::json::encodeProject(automatic);
            check(jobs::decodeProjectFile(envelope(body, 82)) == automatic, "automatic material schema82 parity");
            bool rejected = false;
            try {
                (void)jobs::decodeProjectFile(envelope(body, 81));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "schema81 rejects automatic material policy");
            seeds.push_back({"jobs", "material-delay82.ppm", envelope(body, 82), true});
            {
                auto approved = automatic;
                jobs::LevelingSelection selection;
                selection.policy = jobs::levelingPolicyFor(approved);
                selection.sourceDocumentId = approved.documentId;
                selection.sourceRevision = approved.revision;
                selection.reason = "Material float";
                selection.adoptedAt = "2026-11-17T03:00:00Z";
                approved.planLeveling = selection;
                QTemporaryDir temp;
                jobs::JsonProjectStore store;
                const auto path = temp.filePath("approved.ppm").toStdString();
                store.save(path, approved, {});
                jobs::Document doc(store);
                doc.open(path);
                doc.captureBaseline("Material float", "Parser", "Certificate", "2026-11-17T03:00:00Z");
                const auto certified = jobs::json::encodeProject(*doc.project());
                check(jobs::nativeSchemaFor(*doc.project()) == 84 &&
                          jobs::decodeProjectFile(envelope(certified, 84)) == *doc.project(),
                      "material float baseline84 parity");
                seeds.push_back({"jobs", "material-float84.ppm", envelope(certified, 84), true});
                bool rejected = false;
                try {
                    (void)jobs::decodeProjectFile(envelope(certified, 83));
                } catch (const std::runtime_error &) {
                    rejected = true;
                }
                check(rejected, "schema83 rejects material float engine");
                // The harness additionally probes the valid current-schema raw body.
                seeds.push_back({"jobs", "material-float-wrong-envelope83.ppm", envelope(certified, 83), true});
                auto bad = certified;
                auto baselines = bad["baselines"].toArray();
                auto baseline = baselines[0].toObject();
                baseline["engineVersion"] = "jobs-adopted-material-leveling-9.9";
                baselines[0] = baseline;
                bad["baselines"] = baselines;
                seeds.push_back({"jobs", "material-float-unknown-engine84.ppm", envelope(bad, 84), false});
            }
            for (int variant = 0; variant < 4; ++variant) {
                auto bad = body;
                auto plan = bad["resourcePlan"].toObject();
                if (variant == 0)
                    plan["autoDelayForReceipts"] = 1;
                if (variant == 1)
                    plan["autoDelayForReceipts"] = "true";
                if (variant == 2)
                    plan["autoDelayForReceipts"] = QJsonValue(QJsonValue::Null);
                if (variant == 3)
                    plan["autoDelayUnknown"] = true;
                bad["resourcePlan"] = plan;
                seeds.push_back(
                    {"jobs", QString("material-delay-invalid-%1.ppm").arg(variant), envelope(bad, 82), false});
            }
        }
        {
            auto actual = minimal;
            actual.statusAt = actual.startDate + " 10:00";
            actual.resourcePlan.autoDelayRemainingForReceipts = true;
            actual.resourcePlan.materials = {{"M", "Material", "kg", 0, 1000}};
            jobs::MaterialActualEntry e;
            e.materialId = "M";
            e.kind = "receipt";
            e.at = actual.startDate + " 09:30";
            e.quantityMilli = 1000;
            jobs::appendMaterialActuals(actual, {{e}, "Reporter", "Actual receipt", "2026-11-17T03:00:00Z"},
                                        [] { return "material-observation"; });
            ++actual.revision;
            jobs::validate(actual);
            const auto body = jobs::json::encodeProject(actual);
            check(jobs::decodeProjectFile(envelope(body, 83)) == actual, "actual material schema83 parity");
            bool rejected = false;
            try {
                (void)jobs::decodeProjectFile(envelope(body, 82));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected, "82 rejects actual materials");
            seeds.push_back({"jobs", "material-actual83.ppm", envelope(body, 83), true});
            for (int variant = 0; variant < 9; ++variant) {
                auto bad = body;
                auto entries = bad["materialActuals"].toArray();
                auto row = entries[0].toObject();
                if (variant == 0)
                    row["quantityMilli"] = -1;
                if (variant == 1)
                    row["kind"] = "invented";
                if (variant == 2)
                    row.remove("unit");
                if (variant == 3)
                    row["recordedRevision"] = 1.5;
                if (variant == 4)
                    row["quantityMilli"] = "1";
                if (variant == 5)
                    row["previousEntryId"] = "missing";
                if (variant == 6)
                    row["unit"] = "ton";
                if (variant == 7) {
                    auto plan = bad["resourcePlan"].toObject();
                    plan["autoDelayRemainingForReceipts"] = "true";
                    bad["resourcePlan"] = plan;
                }
                if (variant == 8)
                    row["unknown"] = true;
                entries[0] = row;
                bad["materialActuals"] = entries;
                seeds.push_back(
                    {"jobs", QString("material-actual-invalid-%1.ppm").arg(variant), envelope(bad, 83), false});
            }
        }
        const auto seedCount = seeds.size();
        for (std::size_t i = 0; i < seedCount; ++i) {
            auto bytes = seeds[i].bytes.left(seeds[i].bytes.size() / 2);
            // Truncating a raw payload is deliberately invalid JSON too.
            seeds.push_back({seeds[i].parser, seeds[i].name + ".truncated", bytes, false});
        }
        for (const auto *parser : {"jobs", "xml", "xer", "workspace"}) {
            seeds.push_back({parser, "empty", {}, false});
            seeds.push_back({parser, "binary", QByteArray("\0\xff\xfe\x01", 4), false});
        }
        auto corrupt = envelope(jobs::json::encodeProject(minimal), 9);
        corrupt.replace("Task", "Tampered");
        bool rejected = false;
        try {
            (void)jobs::decodeProjectFile(corrupt);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected, "envelope checksum bypassed");
        seeds.push_back({"jobs", "checksum.ppm", corrupt, true}); // body path still accepts it
        auto unknown = jobs::json::encodeProject(minimal);
        unknown["unknown"] = 1;
        seeds.push_back({"jobs", "unknown.ppm", envelope(unknown, 9), false});
        rejected = false;
        try {
            (void)jobs::decodeProjectFile(QByteArray(jobs::projectFileLimitBytes + 1, ' '));
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected, "native byte limit bypassed");
        bool logicEscaped = false;
        try {
            (void)jobs::fuzz::probe("unknown", {});
        } catch (const std::logic_error &) {
            logicEscaped = true;
        }
        check(logicEscaped, "harness swallowed a logic error");
        auto cancelled = std::make_shared<jobs::CalculationControl>();
        cancelled->cancel();
        bool cancellationEscaped = false;
        {
            jobs::EvaluationScope evaluation(cancelled);
            try {
                (void)jobs::fuzz::probe("jobs", seeds.front().bytes);
            } catch (const jobs::CalculationCancelled &) {
                cancellationEscaped = true;
            }
        }
        check(cancellationEscaped, "harness swallowed cancellation");
        for (const auto &seed : seeds) {
            const auto result = jobs::fuzz::probe(seed.parser.toStdString(), seed.bytes);
            if (seed.valid)
                check(result.accepted > 0, ("valid seed never reached parser: " + seed.name.toStdString()).c_str());
            else
                check(result.accepted == 0 && result.rejected > 0,
                      ("invalid seed was accepted: " + seed.name.toStdString()).c_str());
            if (app.arguments().size() == 3 && app.arguments()[1] == "--write-corpus") {
                const auto directory = app.arguments()[2] + '/' + seed.parser;
                check(QDir().mkpath(directory), "cannot create corpus directory");
                QFile file(directory + '/' + seed.name);
                check(file.open(QIODevice::WriteOnly) && file.write(seed.bytes) == seed.bytes.size(),
                      "cannot write corpus seed");
            }
        }
        // The memory parser and real store must agree on successful input.
        QTemporaryDir directory;
        jobs::JsonProjectStore store;
        const auto path = directory.filePath("Seed.ppm").toStdString();
        store.save(path, minimal, {});
        check(store.load(path).project == jobs::decodeProjectFile(read(QString::fromStdString(path))),
              "store decoder diverged");
        std::cout << "PASS: " << seeds.size() << " parser seeds, schema 1.." << jobs::maximumNativeSchema
                  << ", leveling/classification/views/dated work/formulas/financials, "
                     "checksum/body "
                     "separation, byte limit and "
                     "store parity\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
