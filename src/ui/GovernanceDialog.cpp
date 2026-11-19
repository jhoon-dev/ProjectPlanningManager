#include "ui/GovernanceDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/EditorUi.h"
#include "ui/SandboxAccess.h"
#include "calculation/ScheduleRisk.h"
#include "persistence/Automation.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace ppm {
namespace {
QString q(const std::string &v) {
    return QString::fromStdString(v);
}
std::string s(const QString &v) {
    return v.toUtf8().toStdString();
}
QString value(QTableWidget *t, int r, int c) {
    return t->item(r, c) ? t->item(r, c)->text().trimmed() : QString{};
}
std::int64_t number(QTableWidget *t, int r, int c) {
    bool ok = false;
    const auto n = value(t, r, c).toLongLong(&ok);
    if (!ok)
        throw AppError(ErrorCode::WORKSPACE_INVALID, {"숫자 입력을 확인하세요."});
    return n;
}
int bounded(QTableWidget *t, int r, int c, int maximum) {
    const auto n = number(t, r, c);
    if (n < 0 || n > maximum)
        throw AppError(ErrorCode::WORKSPACE_INVALID, {"숫자 범위를 확인하세요."});
    return int(n);
}
void fill(QTableWidget *t, const QStringList &values) {
    const auto r = t->rowCount();
    t->insertRow(r);
    for (int c = 0; c < values.size(); ++c)
        t->setItem(r, c, new QTableWidgetItem(values[c]));
}
QPushButton *button(QHBoxLayout *bar, const QString &label, const char *name) {
    auto *b = new QPushButton(label);
    b->setObjectName(name);
    bar->addWidget(b);
    return b;
}
struct Analysis {
    QString text;
    bool cancelled = false;
};
struct State {
    Workspace source;
    std::uint64_t catalog = 0;
    std::shared_ptr<CalculationControl> control;
    std::optional<AutomationPreview> automation;
    std::string automationProject;
    QString exportText;
    std::uint64_t resultGeneration = 0;
};
} // namespace
GovernanceDialog::GovernanceDialog(DesktopWorkspace &workspace, QWidget *parent) : QDialog(parent) {
    setObjectName("governanceDialog");
    setWindowTitle("조직·포트폴리오·협업·위험·자동화");
    resize(1180, 800);
    auto state = std::make_shared<State>();
    state->source = workspace.configuration();
    state->catalog = workspace.catalogGeneration();
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("로컬 운영 프로필입니다. 구성원 전환은 역할 검토용이며 로그인 인증이 아닙니다. 파일 접근 "
                            "권한은 운영체제에서 관리하세요.\n"
                            "표의 변경은 ‘설정 적용’ 후 작업공간 저장으로 보존됩니다. 협업 요청/승인은 관측한 프로젝트 "
                            "revision에만 적용됩니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *identity = new QHBoxLayout;
    layout->addLayout(identity);
    identity->addWidget(new QLabel("현재 구성원"));
    auto *actor = new QComboBox;
    actor->setObjectName("governanceActor");
    actor->addItem("구성원 선택", "");
    for (const auto &m : state->source.governance.members)
        actor->addItem(q(m.name + " (" + m.id + ")"), q(m.id));
    actor->setCurrentIndex(std::max(0, actor->findData(q(workspace.actorId()))));
    identity->addWidget(actor);
    auto *apply = button(identity, "설정 적용", "governanceApply");
    auto *save = button(identity, "작업공간 저장…", "governanceSave");
    connect(save, &QPushButton::clicked, this, [&workspace, this] { workspace.saveWorkspace(this); });
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    auto *status = new QLabel;
    status->setObjectName("governanceStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    const auto guard = [status](const std::function<void()> &f) {
        try {
            f();
        } catch (const std::exception &e) {
            status->setText(qErrorText(e));
        }
    };
    const auto makeTable = [&](const QString &title, const char *name, const QStringList &headers,
                               const QStringList &defaults) {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        auto *t = new QTableWidget(0, headers.size());
        t->setObjectName(name);
        t->setHorizontalHeaderLabels(headers);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        t->horizontalHeader()->setStretchLastSection(true);
        v->addWidget(t);
        auto *bar = new QHBoxLayout;
        v->addLayout(bar);
        auto *add = button(bar, "행 추가", (std::string(name) + "Add").c_str());
        auto *remove = button(bar, "선택 행 제거", (std::string(name) + "Remove").c_str());
        connect(add, &QPushButton::clicked, this, [t, defaults, &workspace] {
            auto values = defaults;
            if (!values.empty() && values[0] == "@id")
                values[0] = q(workspace.newId());
            fill(t, values);
        });
        connect(remove, &QPushButton::clicked, this, [t] {
            if (t->currentRow() >= 0)
                t->removeRow(t->currentRow());
        });
        tabs->addTab(page, title);
        return t;
    };
    auto *org = makeTable("EPS·OBS", "governanceOrganizations", {"ID", "이름", "상위 ID", "유형 (EPS/OBS)"},
                          {"@id", "새 조직", "", "OBS"});
    auto *members = makeTable("구성원", "governanceMembers", {"ID", "이름", "OBS ID"}, {"@id", "새 구성원", ""});
    auto *grants =
        makeTable("역할", "governanceGrants",
                  {"구성원 ID", "프로젝트 ID (공란=전체)", "역할 (owner/editor/reviewer/viewer)"}, {"", "", "viewer"});
    auto *portfolio = makeTable(
        "포트폴리오", "governancePortfolio",
        {"프로젝트 ID", "EPS ID", "책임자 ID", "포트폴리오", "통화", "예산 (최소 화폐 단위)", "우선순위 0~100"},
        {"", "", "", "기본", "KRW", "0", "0"});
    auto *risks = makeTable("위험 등록부", "governanceRisks",
                            {"ID", "프로젝트 ID", "작업 ID", "이름", "책임자 ID", "상태 (open/closed)", "상관 그룹",
                             "낙관 (분)", "최빈 (분)", "비관 (분)", "발생 확률 (0~10000)"},
                            {"@id", "", "", "새 위험", "", "open", "", "480", "960", "1440", "10000"});
    const auto &g = state->source.governance;
    for (const auto &o : g.organizations)
        fill(org, {q(o.id), q(o.name), q(o.parentId), q(o.kind)});
    for (const auto &m : g.members)
        fill(members, {q(m.id), q(m.name), q(m.organizationId)});
    for (const auto &r : g.grants)
        fill(grants, {q(r.memberId), q(r.projectId), q(r.role)});
    for (const auto &p : g.portfolio)
        fill(portfolio, {q(p.projectId), q(p.epsId), q(p.ownerId), q(p.portfolio), q(p.currency),
                         QString::number(p.budgetMinor), QString::number(p.priority)});
    for (const auto &r : g.risks)
        fill(risks, {q(r.id), q(r.projectId), q(r.taskId), q(r.name), q(r.ownerId), q(r.state), q(r.group),
                     QString::number(r.optimistic), QString::number(r.likely), QString::number(r.pessimistic),
                     QString::number(r.probability)});
    connect(actor, &QComboBox::currentIndexChanged, this,
            [&workspace, actor] { workspace.selectActor(s(actor->currentData().toString())); });
    connect(apply, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            if (state->catalog != workspace.catalogGeneration())
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            auto next = workspace.configuration();
            auto &n = next.governance;
            n.organizations.clear();
            n.members.clear();
            n.grants.clear();
            n.portfolio.clear();
            n.risks.clear();
            for (int r = 0; r < org->rowCount(); ++r)
                n.organizations.push_back(
                    {s(value(org, r, 0)), s(value(org, r, 1)), s(value(org, r, 2)), s(value(org, r, 3))});
            for (int r = 0; r < members->rowCount(); ++r)
                n.members.push_back({s(value(members, r, 0)), s(value(members, r, 1)), s(value(members, r, 2))});
            for (int r = 0; r < grants->rowCount(); ++r)
                n.grants.push_back({s(value(grants, r, 0)), s(value(grants, r, 1)), s(value(grants, r, 2))});
            for (int r = 0; r < portfolio->rowCount(); ++r)
                n.portfolio.push_back({s(value(portfolio, r, 0)), s(value(portfolio, r, 1)), s(value(portfolio, r, 2)),
                                       s(value(portfolio, r, 3)), s(value(portfolio, r, 4)), number(portfolio, r, 5),
                                       bounded(portfolio, r, 6, 100)});
            for (int r = 0; r < risks->rowCount(); ++r)
                n.risks.push_back({s(value(risks, r, 0)), s(value(risks, r, 1)), s(value(risks, r, 2)),
                                   s(value(risks, r, 3)), s(value(risks, r, 4)), s(value(risks, r, 5)),
                                   s(value(risks, r, 6)), number(risks, r, 7), number(risks, r, 8), number(risks, r, 9),
                                   bounded(risks, r, 10, 10000)});
            workspace.applyConfiguration(std::move(next));
            state->source = workspace.configuration();
            state->catalog = workspace.catalogGeneration();
            const QSignalBlocker blocked(actor);
            actor->clear();
            actor->addItem("구성원 선택", "");
            for (const auto &m : state->source.governance.members)
                actor->addItem(q(m.name + " (" + m.id + ")"), q(m.id));
            actor->setCurrentIndex(std::max(0, actor->findData(q(workspace.actorId()))));
            status->setText("설정을 적용했습니다. 구성원을 선택하고 작업공간을 저장하세요.");
        });
    });
    auto *audit = new QPlainTextEdit;
    audit->setReadOnly(true);
    audit->setObjectName("governanceAudit");
    tabs->addTab(audit, "운영 감사");
    auto *eventsPage = new QWidget;
    auto *ev = new QVBoxLayout(eventsPage);
    auto *events = new QTableWidget(0, 8);
    events->setObjectName("governanceEvents");
    events->setHorizontalHeaderLabels({"ID", "프로젝트", "작성자", "종류", "내용", "시각", "대상 요청", "revision"});
    events->setEditTriggers(QAbstractItemView::NoEditTriggers);
    events->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ev->addWidget(events);
    const auto refreshEvents = [events, audit, &workspace] {
        QString history;
        for (const auto &a : workspace.configuration().governance.audit)
            history += q(a.createdAt + " · " + a.actorId + " · " + a.action + " · " + a.detail) + '\n';
        audit->setPlainText(history);
        events->setRowCount(0);
        for (const auto &e : workspace.configuration().governance.events)
            fill(events, {q(e.id), q(e.projectId), q(e.actorId), q(e.kind), q(e.text), q(e.createdAt), q(e.targetId),
                          QString::number(e.projectRevision)});
    };
    refreshEvents();
    auto *project = new QComboBox;
    project->setObjectName("governanceProject");
    for (const auto &p : state->source.projects)
        project->addItem(q(p.name + " (" + p.documentId + ")"), q(p.documentId));
    auto *selection = new QHBoxLayout;
    layout->insertLayout(2, selection);
    selection->addWidget(new QLabel("분석·협업·자동화 프로젝트"));
    selection->addWidget(project, 1);
    auto *reference = new QPlainTextEdit;
    reference->setReadOnly(true);
    reference->setMaximumHeight(90);
    QString ids;
    for (const auto &p : workspace.snapshots()) {
        ids += q(p->name + " = " + p->documentId) + '\n';
        for (const auto &t : p->tasks)
            ids += q("  " + t.name + " = " + t.id) + '\n';
    }
    reference->setPlainText(ids.left(20000));
    layout->insertWidget(3, reference);
    auto *eventBar = new QHBoxLayout;
    ev->addLayout(eventBar);
    auto *kind = new QComboBox;
    kind->addItems({"comment", "request", "approve", "reject", "resolve"});
    kind->setObjectName("governanceEventKind");
    auto *target = new QLineEdit;
    target->setPlaceholderText("대상 요청 ID (승인/반려/해결)");
    target->setObjectName("governanceEventTarget");
    auto *message = new QLineEdit;
    message->setPlaceholderText("내용");
    message->setObjectName("governanceEventText");
    eventBar->addWidget(kind);
    eventBar->addWidget(target);
    eventBar->addWidget(message, 1);
    auto *append = button(eventBar, "기록 추가", "governanceAppendEvent");
    connect(append, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            const auto id = s(project->currentData().toString());
            auto *doc = workspace.document(id);
            if (!doc || !doc->project())
                throw AppError(ErrorCode::DOCUMENT_NOT_OPEN);
            workspace.recordCollaboration({workspace.newId(), id, workspace.actorId(), s(kind->currentText()),
                                           s(message->text()),
                                           s(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")),
                                           s(target->text()), doc->project()->revision});
            state->source = workspace.configuration();
            state->catalog = workspace.catalogGeneration();
            refreshEvents();
            status->setText("협업 기록을 추가했습니다. 작업공간 저장으로 보존하세요.");
        });
    });
    tabs->addTab(eventsPage, "협업 기록");
    auto *analysisPage = new QWidget;
    auto *av = new QVBoxLayout(analysisPage);
    auto *description = new QLabel("삼각 분포는 위험 발생 시 작업 전체 기간입니다. 미발생 시 원래 기간을 사용합니다. "
                                   "같은 그룹은 발생/기간 순위를 공유합니다.\n"
                                   "계획 CPM 기반이며 자원 평준화·잔여 예측은 포함하지 않습니다. P10/50/80/90은 최근접 "
                                   "순위, 반기 중앙값 차이는 수렴 참고 지표입니다.");
    description->setWordWrap(true);
    av->addWidget(description);
    auto *analysisBar = new QHBoxLayout;
    av->addLayout(analysisBar);
    auto *iterations = new QSpinBox;
    iterations->setRange(2, 10000);
    iterations->setValue(1000);
    iterations->setObjectName("riskIterations");
    auto *seed = new QLineEdit("1");
    seed->setObjectName("riskSeed");
    auto *deadline = new QLineEdit;
    deadline->setPlaceholderText("마감 yyyy-MM-dd HH:mm (선택)");
    deadline->setObjectName("riskDeadline");
    analysisBar->addWidget(new QLabel("반복"));
    analysisBar->addWidget(iterations);
    analysisBar->addWidget(new QLabel("Seed"));
    analysisBar->addWidget(seed);
    analysisBar->addWidget(deadline);
    auto *riskRun = button(analysisBar, "위험 분석", "riskAnalyze");
    auto *portfolioRun = button(analysisBar, "포트폴리오 집계", "portfolioAnalyze");
    auto *cancel = button(analysisBar, "취소", "riskCancel");
    cancel->setEnabled(false);
    auto *output = new QPlainTextEdit;
    output->setObjectName("governanceAnalysisResult");
    output->setReadOnly(true);
    av->addWidget(output);
    auto *exportBar = new QHBoxLayout;
    av->addLayout(exportBar);
    auto *exportResult = button(exportBar, "결과 저장…", "governanceExport");
    const auto launch = [=, this, &workspace](bool risk) {
        guard([&] {
            if (state->control)
                return;
            if (state->catalog != workspace.catalogGeneration())
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto id = s(project->currentData().toString());
            if (!permits(workspace.configuration(), workspace.actorId(), id, WorkspaceAction::View))
                throw AppError(ErrorCode::DOCUMENT_NOT_EDITABLE);
            const auto snapshot = workspace.document(id) ? workspace.document(id)->snapshot() : nullptr;
            if (risk && !snapshot)
                throw AppError(ErrorCode::DOCUMENT_NOT_OPEN);
            bool ok = false;
            const auto seedValue = seed->text().toULongLong(&ok);
            if (risk && !ok)
                throw AppError(ErrorCode::WORKSPACE_INVALID, {"seed는 64비트 부호 없는 정수입니다."});
            RiskOptions options{seedValue, iterations->value(),
                                deadline->text().isEmpty() ? std::nullopt : std::optional{s(deadline->text())}};
            auto source = workspace.configuration();
            std::erase_if(source.governance.portfolio, [&](const auto &p) {
                return !permits(workspace.configuration(), workspace.actorId(), p.projectId, WorkspaceAction::View);
            });
            const auto inputs = workspace.snapshots();
            const auto generation = workspace.generation();
            state->control = std::make_shared<CalculationControl>();
            const auto control = state->control;
            riskRun->setEnabled(false);
            portfolioRun->setEnabled(false);
            cancel->setEnabled(true);
            state->exportText.clear();
            auto *watcher = new QFutureWatcher<Analysis>(this);
            connect(watcher, &QFutureWatcher<Analysis>::finished, this, [=, &workspace] {
                const auto result = watcher->result();
                watcher->deleteLater();
                state->control.reset();
                riskRun->setEnabled(true);
                portfolioRun->setEnabled(true);
                cancel->setEnabled(false);
                if (generation != workspace.generation()) {
                    output->setPlainText("원본/역할이 변경되어 결과를 게시하지 않았습니다.");
                    return;
                }
                output->setPlainText(result.text);
                if (!result.cancelled) {
                    state->exportText = result.text;
                    state->resultGeneration = generation;
                }
            });
            connect(this, &QObject::destroyed, watcher, [control] { control->cancel(); });
            watcher->setFuture(QtConcurrent::run([=] {
                try {
                    if (risk) {
                        const auto r = analyzeScheduleRisk(*snapshot, source.governance.risks, options, control);
                        if (r.status == CalculationStatus::Cancelled)
                            return Analysis{"분석을 취소했습니다.", true};
                        QString text =
                            QString("프로젝트 %1 · revision %2\n모델 %3 · seed %4 · 표본 %5\n결정론 완료 %6\n")
                                .arg(q(r.documentId))
                                .arg(r.revision)
                                .arg(q(r.model))
                                .arg(r.seed)
                                .arg(r.iterations)
                                .arg(q(r.baselineFinish));
                        for (const auto &[quantile, finish] : r.finishQuantiles)
                            text += QString("P%1: %2\n").arg(quantile).arg(q(finish));
                        text += QString("반기 중앙값 차이: %1 근무 분\n").arg(r.medianDriftMinutes);
                        if (options.deadline)
                            text += QString("마감 %1 이내: %2 / %3\n")
                                        .arg(q(*options.deadline))
                                        .arg(r.deadlineHits)
                                        .arg(r.iterations);
                        for (const auto &[task, count] : r.criticalCounts)
                            text += QString("임계 빈도 %1: %2 / %3\n").arg(q(task)).arg(count).arg(r.iterations);
                        return Analysis{text, false};
                    }
                    EvaluationScope scope(control);
                    QString text = "포트폴리오 · 통화별 독립 집계 (환산 없음)\n";
                    for (const auto &r : summarizePortfolio(source, inputs)) {
                        calculationCheckpoint();
                        text += QString("%1 [%2] 예산 %3 최소 단위 · 프로젝트 %4 / 열림 %5 / 정상 CPM %6 · 열린 위험 "
                                        "%7 · 로드된 정상 계획 완료 %8\n")
                                    .arg(q(r.portfolio))
                                    .arg(q(r.currency))
                                    .arg(r.budgetMinor)
                                    .arg(r.projects)
                                    .arg(r.loaded)
                                    .arg(r.validSchedules)
                                    .arg(r.openRisks)
                                    .arg(r.finish ? q(*r.finish) : "미정");
                    }
                    return Analysis{text, false};
                } catch (const CalculationCancelled &) {
                    return Analysis{"분석을 취소했습니다.", true};
                } catch (const std::exception &e) {
                    return Analysis{qErrorText(e), true};
                }
            }));
        });
    };
    connect(riskRun, &QPushButton::clicked, this, [launch] { launch(true); });
    connect(portfolioRun, &QPushButton::clicked, this, [launch] { launch(false); });
    connect(cancel, &QPushButton::clicked, this, [state] {
        if (state->control)
            state->control->cancel();
    });
    connect(exportResult, &QPushButton::clicked, this, [=, this] {
        guard([&] {
            if (state->exportText.isEmpty())
                return;
            const auto path = QFileDialog::getSaveFileName(this, "분석 결과 저장", {}, "텍스트 (*.txt)");
            if (path.isEmpty() || !authorizeSandboxFile(this, path))
                return;
            QSaveFile file(path);
            const auto bytes = state->exportText.toUtf8();
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                throw AppError(ErrorCode::FILE_OPERATION_FAILED);
        });
    });
    tabs->addTab(analysisPage, "위험·포트폴리오 분석");
    auto *automationPage = new QWidget;
    auto *au = new QVBoxLayout(automationPage);
    auto *help = new QLabel("JSON 일괄 변경은 안정 작업 ID와 현재 revision을 요구합니다. 미리보기 후 한 번 적용되며 "
                            "실행 취소가 가능합니다. 파일 저장은 별도입니다.\nPPM-Automation 명령줄 도구로 프로젝트 "
                            "조회·미리보기·원자적 파일 저장을 실행할 수 있습니다.");
    help->setWordWrap(true);
    au->addWidget(help);
    auto *request = new QPlainTextEdit;
    request->setObjectName("automationRequest");
    au->addWidget(request);
    auto *auBar = new QHBoxLayout;
    au->addLayout(auBar);
    auto *inspect = button(auBar, "요청 예제 만들기", "automationExample");
    auto *preview = button(auBar, "미리보기", "automationPreview");
    auto *execute = button(auBar, "미리보기 적용", "automationApply");
    execute->setEnabled(false);
    auto *receipt = new QPlainTextEdit;
    receipt->setReadOnly(true);
    receipt->setObjectName("automationReceipt");
    au->addWidget(receipt);
    connect(inspect, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            auto *doc = workspace.document(s(project->currentData().toString()));
            if (!doc || !doc->project())
                throw AppError(ErrorCode::DOCUMENT_NOT_OPEN);
            const auto &p = *doc->project();
            QJsonArray operations;
            if (!p.tasks.empty())
                operations.append(QJsonObject{{"taskId", q(p.tasks[0].id)}, {"name", q(p.tasks[0].name)}});
            request->setPlainText(QJsonDocument(QJsonObject{{"format", "PPM-AUTOMATION-1"},
                                                            {"documentId", q(p.documentId)},
                                                            {"revision", qint64(p.revision)},
                                                            {"requestId", q(workspace.newId())},
                                                            {"operations", operations}})
                                      .toJson());
        });
    });
    connect(request, &QPlainTextEdit::textChanged, this, [state, execute] {
        state->automation.reset();
        execute->setEnabled(false);
    });
    connect(preview, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            state->automation.reset();
            execute->setEnabled(false);
            const auto id = s(project->currentData().toString());
            auto *doc = workspace.document(id);
            if (!doc)
                throw AppError(ErrorCode::DOCUMENT_NOT_OPEN);
            state->automation = previewAutomation(*doc, request->toPlainText().toUtf8());
            state->automationProject = id;
            receipt->setPlainText(QJsonDocument(state->automation->receipt).toJson());
            execute->setEnabled(true);
        });
    });
    connect(execute, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            execute->setEnabled(false);
            if (!state->automation || state->automationProject != s(project->currentData().toString()))
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            auto *doc = workspace.document(state->automationProject);
            if (!doc)
                throw AppError(ErrorCode::DOCUMENT_NOT_OPEN);
            receipt->setPlainText(
                QJsonDocument(workspace.executeAutomation(state->automationProject, *state->automation)).toJson());
            state->automation.reset();
            state->source = workspace.configuration();
            state->catalog = workspace.catalogGeneration();
            refreshEvents();
        });
    });
    tabs->addTab(automationPage, "JSON 자동화");
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [state, output, audit, &workspace] {
        if (!state->exportText.isEmpty() && state->resultGeneration != workspace.generation()) {
            state->exportText.clear();
            output->appendPlainText("원본/역할이 변경되었습니다. 다시 분석한 후 결과를 저장하세요.");
        }
        QString history;
        for (const auto &a : workspace.configuration().governance.audit)
            history += q(a.createdAt + " · " + a.actorId + " · " + a.action + " · " + a.detail) + '\n';
        if (audit->toPlainText() != history)
            audit->setPlainText(history);
    });
    timer->start(200);
    auto *closeBar = new QHBoxLayout;
    layout->addLayout(closeBar);
    auto *close = button(closeBar, "닫기", "governanceClose");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    prepareEditorDialog(*this);
}
} // namespace ppm
