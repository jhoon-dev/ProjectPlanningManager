#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/XerScheduleAdoption.h"
#include "formatting/DiagnosticText.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/XerScheduleImport.h"
#include "persistence/XerFinancialJson.h"
#include "ui/XerScheduleDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <algorithm>
using namespace ppm;
namespace {
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    check(bad, "expected schedule adoption rejection");
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write evidence");
}
QByteArray envelope(const Project &p, int schema) {
    const auto b = json::encodeProject(p);
    return QJsonDocument(QJsonObject{{"format", "PPM"},
                                     {"schemaVersion", schema},
                                     {"payload", b},
                                     {"sha256", QString::fromLatin1(QCryptographicHash::hash(
                                                                        QJsonDocument(b).toJson(QJsonDocument::Compact),
                                                                        QCryptographicHash::Sha256)
                                                                        .toHex())}})
        .toJson();
}
void preserved(const Project &before, const Project &after) {
    check(before.resources == after.resources && before.assignments == after.assignments &&
              before.taskCosts == after.taskCosts && before.currency == after.currency &&
              before.costProfile == after.costProfile && before.statusAt == after.statusAt &&
              before.progress == after.progress && before.costEntries == after.costEntries &&
              before.periodActuals == after.periodActuals && before.baselines == after.baselines &&
              before.scenarios == after.scenarios && before.xerFinancials == after.xerFinancials &&
              before.xerFinancialRevisions == after.xerFinancialRevisions &&
              before.financialPeriodEvents == after.financialPeriodEvents &&
              before.externalEvmAdoptions == after.externalEvmAdoptions && before.activities == after.activities &&
              before.resourcePlan == after.resourcePlan,
          "schedule adoption changed financial/history/resource facts");
}
void wait(const std::function<bool()> &f) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(f(), "schedule UI timeout");
}
XerScheduleSubmission submit(const XerScheduleSource &source, const Project &p) {
    XerScheduleSubmission s;
    s.source = source;
    s.confirmed = true;
    s.reporter = "계획 검토자";
    s.reason = "후속 일정·구조·휴일 채택";
    s.recordedAt = "2026-11-08T09:00:00Z";
    for (std::size_t i = 0; i < source.input.tasks.size(); ++i)
        s.links.push_back({source.input.tasks[i].id, i < 6 ? p.tasks[i].id : "", i >= 6});
    return s;
}
auto prepare(Document &d, const XerScheduleSubmission &s,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(XerScheduleEdit{s}), c);
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
        QDir().mkpath("xer-schedule-qa");
        QTemporaryDir temp;
        JsonProjectStore store;
        auto p =
            importPrimaveraXer(readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE)), "original.xer").project;
        const auto initial = (temp.path() + "/initial.ppm").toStdString();
        store.save(initial, p, {});
        {
            Document setup(store);
            setup.open(initial);
            setup.addTask("로컬 작업 보존");
            setup.setDuration(setup.project()->tasks.size() - 1, 60);
            const auto baseline =
                setup.captureBaseline("채택 이전 기준선", "PM", "동결 예산·일정", "2026-11-08T08:00:00Z");
            FinancialPeriodSubmission period;
            period.period.name = "채택 이전 마감";
            period.period.baselineId = baseline;
            period.period.fromDate = period.period.toDate = "2026-10-26";
            period.reporter = "PM";
            period.reason = "마감 보존 검증";
            period.recordedAt = "2026-11-08T08:10:00Z";
            period.confirmed = true;
            period.period.id = setup.recordFinancialPeriod(period);
            period.action = "close";
            period.recordedAt = "2026-11-08T08:11:00Z";
            setup.recordFinancialPeriod(period);
            p = *setup.project();
        }
        auto next = p;
        next.tasks.pop_back();
        next.progress.clear();
        next.costEntries.clear();
        next.periodActuals.clear();
        next.baselines.clear();
        next.financialPeriodEvents.clear();
        next.xerFinancials.reset();
        next.xerFinancialRevisions.clear();
        next.statusAt.reset();
        next.startDate = "2026-11-02";
        next.tasks[0].name = "후속 설계 변경";
        next.tasks[0].durationMinutes = 960;
        next.tasks[1].constraintType = ConstraintType::ASAP;
        next.tasks[1].constraintAt.reset();
        for (auto &w : next.wbsNodes)
            w.name = "후속 " + w.name;
        if (next.wbsNodes.size() > 1) {
            next.wbsNodes.back().parentId.clear();
            next.wbsNodes.front().parentId = next.wbsNodes.back().id;
        }
        for (auto &c : next.calendars) {
            c.periods = {{540, 720}, {780, 1020}};
            c.holidays = {"2026-11-03"};
        }
        Task fresh;
        fresh.id = "source-new";
        fresh.name = "후속 신규 작업";
        fresh.durationMinutes = 60;
        fresh.predecessors = {next.tasks[0].id};
        fresh.calendarId = next.tasks[0].calendarId;
        fresh.wbsId = next.tasks.back().wbsId;
        next.tasks.push_back(fresh);
        const auto bytes = exportPrimaveraXer(next).xer;
        const auto info = listPrimaveraXerProjects(bytes);
        check(info.size() == 1, "source selection fixture");
        const auto source = importXerScheduleSource(bytes, "next.xer", XerEncoding::Utf8, info[0].id);
        rejects([&] { importXerScheduleSource(bytes, "next.xer", XerEncoding::Utf8, "missing"); });
        check(!source.statusAt && source.input.tasks.size() == 7, "source planning import");
        auto s = submit(source, p);
        const auto candidate = adoptXerSchedule(p, s, "oracle");
        auto currency = s;
        currency.source.input.currency = "EUR";
        check(adoptXerSchedule(p, currency, "currency").currency == p.currency, "schedule adopted source currency");
        auto ambiguous = p;
        auto renamedSource = source.input;
        renamedSource.tasks[0].id = "unknown-source-id";
        ambiguous.tasks[0].name = ambiguous.tasks[1].name = renamedSource.tasks[0].name;
        check(suggestXerScheduleLinks(renamedSource, ambiguous)[0].targetId.empty(), "duplicate name guessed");
        check(candidate.tasks.size() == 8 && candidate.tasks.back().name == fresh.name, "new/local tasks");
        check(candidate.tasks[0].durationMinutes == 960 && candidate.tasks[0].id == p.tasks[0].id &&
                  candidate.tasks[1].predecessors[0] == p.tasks[0].id,
              "schedule identity/dependency mapping");
        check(candidate.calendars.size() == p.calendars.size() + source.input.calendars.size() &&
                  candidate.wbsNodes.size() == p.wbsNodes.size() + source.input.wbsNodes.size(),
              "namespaced union");
        for (const auto &c : p.calendars)
            check(std::find(candidate.calendars.begin(), candidate.calendars.end(), c) != candidate.calendars.end(),
                  "old resource calendar overwritten");
        for (int flags = 1; flags < 8; ++flags) {
            auto scoped = s;
            scoped.schedule = flags & 1;
            scoped.wbs = flags & 2;
            scoped.calendars = flags & 4;
            if (!scoped.schedule)
                scoped.links.back().create = false;
            const auto out = adoptXerSchedule(p, scoped, "scope");
            if (!scoped.schedule) {
                check(out.startDate == p.startDate && out.tasks[0].durationMinutes == p.tasks[0].durationMinutes,
                      "WBS/calendar changed timing inputs");
            }
            if (!scoped.wbs)
                check(out.wbsNodes == p.wbsNodes && out.tasks[0].wbsId == p.tasks[0].wbsId, "unselected WBS");
            if (!scoped.calendars)
                check(out.calendars == p.calendars && out.holidays == p.holidays &&
                          out.tasks[0].calendarId == p.tasks[0].calendarId,
                      "unselected calendar");
        }
        for (int i = 0; i < 10; ++i) {
            auto bad = s;
            if (i == 0)
                bad.confirmed = false;
            if (i == 1)
                bad.links.pop_back();
            if (i == 2)
                bad.links[1].targetId = bad.links[0].targetId;
            if (i == 3)
                bad.links[0].targetId = "missing";
            if (i == 4)
                bad.links[0].targetId.clear();
            if (i == 5)
                bad.source.sourceSha256 = "bad";
            if (i == 6)
                bad.reporter = " ";
            if (i == 7)
                bad.schedule = bad.wbs = bad.calendars = false;
            if (i == 8)
                bad.links[0].sourceId = "foreign";
            if (i == 9) {
                bad.schedule = false;
            }
            rejects([&] { adoptXerSchedule(p, bad, "bad"); });
        }
        const auto path = (temp.path() + "/document.ppm").toStdString();
        store.save(path, p, {});
        Document d(store);
        d.open(path);
        auto edit = prepare(d, s);
        if (edit->failure())
            std::cerr << diagnosticMessage(*edit->failure()) << '\n';
        check(edit->state() == EditState::Ready && *d.project() == p, "worker preparation/source preservation");
        const auto plan = calculatePlan(*edit->previewProject());
        check(plan.status == CalculationStatus::Valid, "prepared plan dates");
        check(d.applyPreparedEdit(*edit) == EditState::Applied, "schedule commit");
        preserved(p, *d.project());
        check(!d.canUndo() && nativeSchemaFor(*d.project()) == 32, "immutable schema32 adoption");
        const auto after = *d.project();
        check(!after.financialPeriodEvents.empty(), "closed financial period fixture absent");
        auto privateCalculation = after;
        privateCalculation.tasks[0].durationMinutes = 1020;
        check(calculatePlan(privateCalculation).status == CalculationStatus::Valid,
              "private calculation rejected adoption archive");
        d.setDuration(0, 1020);
        d.undo();
        check(d.project()->xerScheduleAdoptions == after.xerScheduleAdoptions &&
                  d.project()->tasks[0].durationMinutes == after.tasks[0].durationMinutes,
              "ordinary duration edit/undo lost adoption");
        auto plain = p;
        plain.statusAt.reset();
        plain.progress.clear();
        plain.costEntries.clear();
        plain.periodActuals.clear();
        plain.financialPeriodEvents.clear();
        plain.xerFinancials.reset();
        const auto levelingPath = (temp.path() + "/leveling.ppm").toStdString();
        store.save(levelingPath, plain, {});
        Document leveling(store);
        leveling.open(levelingPath);
        LevelingSelection selection;
        selection.policy = levelingPolicyFor(plain);
        selection.priority = "task-order";
        selection.reason = "채택 전 평준화";
        selection.adoptedAt = "2026-11-08T08:20:00Z";
        selection.sourceDocumentId = leveling.project()->documentId;
        selection.sourceRevision = leveling.project()->revision;
        leveling.adoptLeveling(selection);
        check(leveling.project()->planLeveling.has_value(), "leveling fixture absent");
        leveling.recordXerSchedule(s);
        check(!leveling.project()->planLeveling && leveling.project()->levelingDecisions.back().action == "invalidate",
              "schedule adoption did not invalidate leveling with audit");
        auto contour = p;
        contour.assignments[0].periods = {{0, 480, 50}};
        contour.calculationProfile = effortProfile;
        validatePlan(contour);
        auto shortPlan = s;
        shortPlan.source.input.tasks[0].durationMinutes = 60;
        rejects([&] { adoptXerSchedule(contour, shortPlan, "contour"); });
        check(after.xerScheduleAdoptions.back().source == source, "source archive changed");
        d.save();
        check(store.load(d.path()).project == *d.project(), "schema32 roundtrip/replay");
        rejects([&] { decodeProjectFile(envelope(after, 31)); });
        check(decodeProjectFile(envelope(after, 52)) == after, "supported schema52");
        rejects([&] { decodeProjectFile(envelope(after, maximumNativeSchema + 1)); });
        auto duplicate = after;
        duplicate.xerScheduleAdoptions.push_back(duplicate.xerScheduleAdoptions[0]);
        rejects([&] { validate(duplicate); });
        for (int i = 0; i < 12; ++i) {
            auto body = json::encodeProject(after);
            auto events = body["xerScheduleAdoptions"].toArray();
            auto e = events[0].toObject();
            if (i == 0)
                e["policy"] = "unknown";
            if (i == 1)
                e["confirmed"] = false;
            if (i == 2)
                e["recordedRevision"] = 1.5;
            if (i == 3)
                e["unknown"] = 1;
            if (i == 4)
                e.remove("before");
            if (i == 5) {
                auto a = e["after"].toObject();
                a["startDate"] = "2026-11-04";
                e["after"] = a;
            }
            if (i == 6) {
                auto a = e["source"].toObject();
                a["sourceSha256"] = "bad";
                e["source"] = a;
            }
            if (i == 7) {
                auto l = e["links"].toArray();
                auto a = l[0].toObject();
                a["create"] = "true";
                l[0] = a;
                e["links"] = l;
            }
            if (i == 8)
                e["recordedAt"] = "yesterday";
            if (i == 9)
                e["reporter"] = " ";
            if (i == 10)
                e["links"] = QJsonArray{};
            if (i == 11) {
                auto a = e["source"].toObject();
                a["statusAt"] = 5;
                e["source"] = a;
            }
            events[0] = e;
            body["xerScheduleAdoptions"] = events;
            rejects([&] { json::decodeProject(body, 32); });
            write(QString("xer-schedule-qa/malformed-%1.json").arg(i), QJsonDocument(body).toJson());
        }
        auto cyclic = s;
        cyclic.source.input.tasks[0].predecessors = {cyclic.source.input.tasks[1].id};
        auto failed = prepare(d, cyclic);
        check(failed->state() == EditState::Failed && d.project()->xerScheduleAdoptions == after.xerScheduleAdoptions,
              "cyclic adoption applied");
        auto cancel = std::make_shared<CalculationControl>();
        cancel->cancel();
        check(prepare(d, s, cancel)->state() == EditState::Cancelled, "cancel ignored");
        auto stale = prepare(d, s);
        check(stale->state() == EditState::Ready, "repeat adoption preparation");
        d.renameTask(0, "ordinary edit");
        check(d.applyPreparedEdit(*stale) == EditState::Stale, "stale applied");
        d.undo();
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        d.renameTask(0, "복구 검증");
        auto session = recovery.newSession();
        session->checkpoint(d.recoverySnapshot("2026-11-08T10:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema32 recovery");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        check(restored.project()->xerScheduleAdoptions == d.project()->xerScheduleAdoptions,
              "recovery lost schedule adoption");
        d.undo();
        d.saveAs((temp.path() + "/copy.ppm").toStdString());
        check(store.load(d.path()).project == *d.project(), "copy adoption");
        Document ro(store);
        ro.open(d.path(), true);
        rejects([&] { ro.recordXerSchedule(s); });
        const auto exchange = exportPrimaveraXer(*d.project());
        const auto back = importPrimaveraXer(exchange.xer, "adopted-export.xer").project;
        check(back.tasks.size() == d.project()->tasks.size(), "schedule export task count");
        check(exchange.report["actualCosts"].toObject()["scheduleAdoptionCount"].toInt() == 1, "exchange receipt");
        write("xer-schedule-qa/source.xer", bytes);
        write("xer-schedule-qa/source-plan.json", QJsonDocument(json::encodePlanInput(source.input)).toJson());
        write("xer-schedule-qa/before.json", QJsonDocument(json::encodeProject(p)).toJson());
        write("xer-schedule-qa/adopted.ppm", envelope(after, 32));
        write("xer-schedule-qa/export.xer", exchange.xer);
        QJsonArray dates;
        for (const auto &row : plan.tasks)
            dates.append(QJsonObject{{"id", QString::fromStdString(row.taskId)},
                                     {"start", QString::fromStdString(row.start)},
                                     {"finish", QString::fromStdString(row.finish)}});
        write("xer-schedule-qa/plan-dates.json", QJsonDocument(dates).toJson());
        const auto sourcePath = temp.path() + "/next.xer";
        write(sourcePath, bytes);
        auto runUi = [&](bool small, bool publish) {
            const auto uiPath = (temp.path() + (publish ? "/ui-save.ppm" : "/ui-cancel.ppm")).toStdString();
            store.save(uiPath, p, {});
            Document ui(store);
            ui.open(uiPath);
            XerScheduleDialog dialog(ui);
            dialog.show();
            QApplication::processEvents();
            if (small)
                dialog.resize(800, 600);
            dialog.loadSource(sourcePath);
            wait([&] { return dialog.findChild<QPushButton *>("xerScheduleOpen")->isEnabled(); });
            auto *table = dialog.findChild<QTableView *>("xerScheduleLinks");
            check(table->model()->rowCount() == 7, "schedule load UI");
            for (int i = 0; i < 7; ++i)
                check(table->model()->setData(table->model()->index(i, 2),
                                              i < 6 ? "id:" + QString::fromStdString(p.tasks[i].id) : QString("new"),
                                              Qt::EditRole),
                      "UI mapping");
            dialog.findChild<QLineEdit *>("xerScheduleReporter")->setText("UI 검토자");
            auto *reason = dialog.findChild<QLineEdit *>("xerScheduleReason");
            reason->setText("새 일정·계층·휴일 검토");
            auto *analyze = dialog.findChild<QPushButton *>("xerScheduleAnalyze");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            auto *preview = dialog.findChild<QTableView *>("xerSchedulePreview");
            auto *confirm = dialog.findChild<QCheckBox *>("xerScheduleConfirm");
            auto *put = dialog.findChild<QPushButton *>("xerScheduleSubmit");
            check(preview->model() && preview->model()->rowCount() == 8 && confirm->isEnabled(),
                  "schedule preview absent");
            check(*ui.project() == p, "preview mutated");
            confirm->setChecked(true);
            reason->setText("변경 사유 재확인");
            check(!put->isEnabled() && !confirm->isChecked(), "draft edit retained preview");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            confirm->setChecked(true);
            dialog.grab().save(small ? "xer-schedule-qa/preview-small.png" : "xer-schedule-qa/preview.png");
            auto *structure = dialog.findChild<QTableView *>("xerScheduleStructure");
            check(structure->model() && structure->model()->rowCount() > 5, "calendar/WBS definition preview absent");
            dialog.findChild<QTabWidget *>("xerScheduleTabs")->setCurrentWidget(structure);
            dialog.grab().save(small ? "xer-schedule-qa/structure-small.png" : "xer-schedule-qa/structure.png");
            dialog.findChild<QTabWidget *>("xerScheduleTabs")->setCurrentWidget(table);
            dialog.grab().save(small ? "xer-schedule-qa/editor-small.png" : "xer-schedule-qa/editor.png");
            if (publish) {
                check(put->isEnabled(), "commit disabled");
                put->click();
                preserved(p, *ui.project());
                check(ui.project()->xerScheduleAdoptions.size() == 1, "UI submit");
                XerScheduleDialog history(ui);
                history.show();
                auto *h = history.findChild<QTableWidget *>("xerScheduleHistory");
                check(h->rowCount() == 1, "history absent");
                QMetaObject::invokeMethod(h, "cellDoubleClicked", Qt::DirectConnection, Q_ARG(int, 0), Q_ARG(int, 0));
                wait([&] { return history.findChild<QPushButton *>("xerScheduleOpen")->isEnabled(); });
                check(history.findChild<QTableView *>("xerSchedulePreview")->model()->rowCount() == 8,
                      "history preview");
                history.grab().save("xer-schedule-qa/history.png");
                history.reject();
            } else {
                dialog.reject();
                check(*ui.project() == p, "close adopted plan");
            }
        };
        runUi(true, false);
        runUi(false, true);
        XerScheduleDialog readonly(ro);
        check(!readonly.findChild<QPushButton *>("xerScheduleOpen")->isEnabled(), "readonly UI active");
        auto cancelPath = (temp.path() + "/cancel.ppm").toStdString();
        store.save(cancelPath, p, {});
        Document cancelDoc(store);
        cancelDoc.open(cancelPath);
        XerScheduleDialog readCancel(cancelDoc);
        readCancel.loadSource(sourcePath);
        readCancel.findChild<QPushButton *>("xerScheduleCancel")->click();
        wait([&] { return readCancel.findChild<QPushButton *>("xerScheduleOpen")->isEnabled(); });
        check(readCancel.findChild<QTableView *>("xerScheduleLinks")->model()->rowCount() == 0 &&
                  *cancelDoc.project() == p,
              "cancelled read retained source");
        MainWindow window(cancelDoc);
        auto *action = window.findChild<QAction *>("adoptXerSchedule");
        check(action && action->isEnabled(), "schedule menu");
        bool seen = false;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *v = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (v && v->objectName() == "xerScheduleDialog") {
                seen = true;
                v->reject();
            }
        });
        timer.start();
        action->trigger();
        timer.stop();
        check(seen, "schedule menu not connected");
        std::cout << "PASS: schedule/WBS/calendar scoped adoption, explicit task creation/ID mapping, local tasks and "
                     "native histories preserved, schema32/replay/recovery/copy, worker stale/cancel/read-only, XER "
                     "export and Windows UI\n";
    } catch (const std::exception &e) {
        std::cerr << errorText(e) << '\n';
        return 1;
    }
    return 0;
}
