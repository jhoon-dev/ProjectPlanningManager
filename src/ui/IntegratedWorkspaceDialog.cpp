#include "ui/IntegratedWorkspaceDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/EditorUi.h"
#include "ui/ExchangeOperation.h"
#include "ui/SandboxAccess.h"
#include "reporting/IntegratedExport.h"
#include "reporting/ReportExport.h"
#include "formatting/QtDiagnosticText.h"
#include "domain/ResourcePlan.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <memory>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string s(const QString &v) {
    return v.toUtf8().toStdString();
}
void fill(QTableWidget *t, int r, const QStringList &values) {
    for (int c = 0; c < values.size(); ++c) {
        auto *item = new QTableWidgetItem(values[c]);
        item->setToolTip(values[c]);
        t->setItem(r, c, item);
    }
}
QTableWidget *table(const QStringList &headers, QWidget *parent) {
    auto *t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
QString relation(DependencyType t) {
    return t == DependencyType::FS ? "FS" : t == DependencyType::SS ? "SS" : t == DependencyType::FF ? "FF" : "SF";
}
struct State {
    Workspace config;
    std::uint64_t catalog = 0, resultGeneration = 0;
    std::optional<IntegratedResult> result;
};
} // namespace
IntegratedWorkspaceDialog::IntegratedWorkspaceDialog(DesktopWorkspace &workspace, QWidget *parent) : QDialog(parent) {
    setObjectName("integratedWorkspaceDialog");
    setWindowTitle("프로젝트 간 링크·통합 일정");
    resize(1160, 850);
    auto state = std::make_shared<State>();
    state->config = workspace.configuration();
    state->catalog = workspace.catalogGeneration();
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("프로젝트 간 FS/SS/FF/SF와 부호 있는 지연(근무 분)을 작업공간에 저장합니다. 모든 프로젝트 "
                            "창을 열어야 계산할 수 있습니다.\n"
                            "통합 일정은 원본 입력을 바꾸지 않는 미리보기입니다. 공유 자원 연결은 작업공간의 공유 자원 "
                            "탭에서 설정하세요. 통합 예측에는 동일 상태일이 필요합니다.\n"
                            "실제 자재 잔여 자동 대기는 각 문서의 옵션과 상태일 재고·잔여 소모 확인을 사용합니다. "
                            "같은 이름의 자재도 프로젝트별 재고로 계산합니다.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *links =
        table({"링크 ID", "선행 프로젝트", "선행 작업", "후행 프로젝트", "후행 작업", "관계", "지연(분)"}, this);
    links->setObjectName("integratedLinks");
    links->setMaximumHeight(130);
    layout->addWidget(links);
    auto *form = new QHBoxLayout;
    layout->addLayout(form);
    auto *from = new QComboBox(this), *to = new QComboBox(this), *type = new QComboBox(this);
    from->setObjectName("integratedFrom");
    to->setObjectName("integratedTo");
    type->setObjectName("integratedRelation");
    for (const auto &p : workspace.snapshots())
        for (const auto &t : p->tasks) {
            const auto label = q(p->name) + " / " + q(t.name) + " [" + q(t.id) + "]";
            QStringList ref{q(p->documentId), q(t.id)};
            from->addItem(label, ref);
            to->addItem(label, ref);
        }
    for (auto *combo : {from, to}) {
        combo->setMinimumContentsLength(12);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }
    type->setMaximumWidth(70);
    type->addItems({"FS", "SS", "FF", "SF"});
    auto *lag = new QSpinBox(this);
    lag->setObjectName("integratedLag");
    lag->setRange(-5000000, 5000000);
    lag->setMaximumWidth(120);
    lag->setSuffix(" 분");
    form->addWidget(from, 1);
    form->addWidget(type);
    form->addWidget(lag);
    form->addWidget(to, 1);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    auto button = [this](const QString &label, const char *name, QHBoxLayout *box) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        box->addWidget(b);
        return b;
    };
    auto *add = button("링크 추가", "integratedAdd", buttons),
         *edit = button("선택 링크 변경", "integratedEdit", buttons),
         *remove = button("선택 링크 삭제", "integratedRemove", buttons);
    auto *options = new QHBoxLayout;
    layout->addLayout(options);
    auto *forecast = new QCheckBox("현재 예측", this), *level = new QCheckBox("공유 자원 평준화", this);
    forecast->setObjectName("integratedForecast");
    level->setObjectName("integratedLevel");
    forecast->setChecked(state->config.integrated.forecast);
    level->setChecked(state->config.integrated.levelShared);
    options->addWidget(forecast);
    options->addWidget(level);
    auto *priority = new QComboBox(this);
    priority->setObjectName("integratedPriority");
    for (const auto &[label, key] :
         std::vector<std::pair<QString, QString>>{{"작업 순서", "task-order"},
                                                  {"짧은 작업 우선", "shortest-first"},
                                                  {"긴 작업 우선", "longest-first"},
                                                  {"여유 적은 작업 우선", "least-float-first"},
                                                  {"마감 우선", "deadline-first"}})
        priority->addItem(label, key);
    priority->setCurrentIndex(priority->findData(q(state->config.integrated.priority)));
    options->addWidget(priority);
    auto *horizon = new QSpinBox(this);
    horizon->setObjectName("integratedHorizon");
    horizon->setRange(1, 5200);
    horizon->setValue(state->config.integrated.horizonWorkDays);
    horizon->setSuffix(" 탐색 일");
    options->addWidget(horizon);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto *save = button("링크·옵션 적용", "integratedSave", actions),
         *calculate = button("통합 일정 계산", "integratedCalculate", actions),
         *navigate = button("선택 작업 원본으로 이동", "integratedNavigate", actions);
    auto *exports = new QHBoxLayout;
    layout->addLayout(exports);
    auto *json = button("JSON 저장…", "integratedJson", exports),
         *xlsx = button("XLSX 저장…", "integratedXlsx", exports), *pdf = button("PDF 저장…", "integratedPdf", exports);
    for (auto *b : {navigate, json, xlsx, pdf})
        b->setEnabled(false);
    auto *result = table({"프로젝트", "작업 ID", "작업", "원본 revision", "로컬 시작", "로컬 완료", "통합 시작",
                          "통합 완료", "통합 잔여 시작", "총 여유(분)", "자유 여유(분)", "임계"},
                         this);
    result->setObjectName("integratedTasks");
    layout->addWidget(result, 1);
    auto *materials = table(
        {"프로젝트 / 자재", "자재", "단위", "상태일", "확인 재고", "잔여 소모", "미입고 잔량", "원장 revision"}, this);
    materials->setObjectName("integratedMaterialStocks");
    materials->horizontalHeader()->setStretchLastSection(false);
    materials->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    materials->setMaximumHeight(160);
    materials->hide();
    layout->addWidget(materials);
    auto *load = table({"공유 자원 ID", "날짜", "최대 수요(%)", "가용(%)", "과부하(분)", "미정(분)"}, this);
    load->setObjectName("integratedLoad");
    load->setMaximumHeight(140);
    layout->addWidget(load);
    auto *status = new QLabel(this);
    status->setObjectName("integratedStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *details = new QPlainTextEdit(this);
    details->setObjectName("integratedDiagnostics");
    details->setReadOnly(true);
    details->setMaximumHeight(100);
    layout->addWidget(details);
    auto refreshLinks = [state, links] {
        links->setRowCount(int(state->config.links.size()));
        for (int i = 0; i < links->rowCount(); ++i) {
            const auto &l = state->config.links[std::size_t(i)];
            fill(links, i,
                 {q(l.id), q(l.predecessorProjectId), q(l.predecessorTaskId), q(l.successorProjectId),
                  q(l.successorTaskId), relation(l.type), QString::number(l.lagMinutes)});
        }
    };
    refreshLinks();
    auto invalidate = [state, result, load, materials, navigate, json, xlsx, pdf] {
        state->result.reset();
        result->setRowCount(0);
        load->setRowCount(0);
        materials->setRowCount(0);
        materials->hide();
        for (auto *b : {navigate, json, xlsx, pdf})
            b->setEnabled(false);
    };
    auto change = [=](bool updating) {
        try {
            auto a = from->currentData().toStringList(), b = to->currentData().toStringList();
            if (a.size() != 2 || b.size() != 2)
                throw AppError(ErrorCode::WORKSPACE_INVALID, {"선행·후행 작업을 선택하세요."});
            const int row = links->currentRow();
            if (updating && (row < 0 || row >= int(state->config.links.size())))
                return;
            auto proposed = state->config;
            ProjectLink link{updating ? proposed.links[std::size_t(row)].id
                                      : s(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                             s(a[0]),
                             s(a[1]),
                             s(b[0]),
                             s(b[1]),
                             static_cast<DependencyType>(type->currentIndex()),
                             lag->value()};
            if (updating)
                proposed.links[std::size_t(row)] = link;
            else
                proposed.links.push_back(link);
            validateWorkspace(proposed);
            state->config = std::move(proposed);
            invalidate();
            refreshLinks();
            status->setText("링크 편집됨 · 적용 시 작업공간에 보관됩니다.");
        } catch (const std::exception &e) {
            showEditorError(*status, e);
        }
    };
    connect(add, &QPushButton::clicked, this, [change] { change(false); });
    connect(edit, &QPushButton::clicked, this, [change] { change(true); });
    connect(remove, &QPushButton::clicked, this, [=] {
        int r = links->currentRow();
        if (r >= 0 && r < int(state->config.links.size())) {
            state->config.links.erase(state->config.links.begin() + r);
            invalidate();
            refreshLinks();
        }
    });
    connect(links, &QTableWidget::itemSelectionChanged, this, [=] {
        int r = links->currentRow();
        if (r < 0 || r >= int(state->config.links.size()))
            return;
        const auto &l = state->config.links[std::size_t(r)];
        from->setCurrentIndex(from->findData(QStringList{q(l.predecessorProjectId), q(l.predecessorTaskId)}));
        to->setCurrentIndex(to->findData(QStringList{q(l.successorProjectId), q(l.successorTaskId)}));
        type->setCurrentText(relation(l.type));
        lag->setValue(int(l.lagMinutes));
    });
    auto apply = [&, state, forecast, level, priority, horizon] {
        if (workspace.catalogGeneration() != state->catalog)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        auto proposed = state->config;
        proposed.integrated = {forecast->isChecked(), level->isChecked(), s(priority->currentData().toString()),
                               horizon->value()};
        if (proposed != workspace.configuration())
            workspace.applyConfiguration(proposed);
        state->config = std::move(proposed);
        state->catalog = workspace.catalogGeneration();
    };
    connect(save, &QPushButton::clicked, this, [=] {
        try {
            apply();
            invalidate();
            status->setText("작업공간에 적용했습니다. 파일 보관은 작업공간 저장을 사용하세요.");
        } catch (const std::exception &e) {
            showEditorError(*status, e);
        }
    });
    connect(calculate, &QPushButton::clicked, this,
            [&, state, apply, invalidate, result, load, materials, status, details, navigate, json, xlsx, pdf] {
                try {
                    apply();
                    invalidate();
                    const auto config = state->config;
                    const auto snapshots = workspace.snapshots();
                    if (snapshots.empty())
                        throw AppError(ErrorCode::WORKSPACE_NO_PROJECTS);
                    auto *doc = workspace.document(snapshots.front()->documentId);
                    if (!doc)
                        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                    const auto generation = workspace.generation();
                    auto output = prepareExchange<IntegratedResult>(
                        this, *doc, "프로젝트 간 링크·공유 자원 통합 일정 계산 중…",
                        [config, snapshots] { return calculateIntegratedWorkspace(config, snapshots); });
                    if (!output) {
                        status->setText("계산 취소");
                        return;
                    }
                    if (workspace.generation() != generation)
                        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                    state->result = std::move(*output);
                    state->resultGeneration = generation;
                    const auto &r = *state->result;
                    QStringList messages;
                    for (const auto &d : r.diagnostics)
                        messages << qDiagnosticText(d);
                    for (const auto &d : r.shared.diagnostics)
                        messages << qDiagnosticText(d);
                    details->setPlainText(messages.join('\n'));
                    status->setText(r.status == CalculationStatus::Valid
                                        ? "유효 · " + QString::number(r.tasks.size()) + " 작업 · 완료 " +
                                              q(r.finish.value_or("")) + " · " + q(r.policy)
                                        : "계산 불가 · 진단을 확인하세요.");
                    if (r.status != CalculationStatus::Valid)
                        return;
                    result->setRowCount(int(r.tasks.size()));
                    for (int i = 0; i < result->rowCount(); ++i) {
                        const auto &t = r.tasks[std::size_t(i)];
                        fill(result, i,
                             {q(t.projectName) + " [" + q(t.ref.projectId) + "]", q(t.ref.taskId), q(t.name),
                              QString::number(t.revision), q(t.originalStart.value_or("")),
                              q(t.originalFinish.value_or("")), q(t.schedule.start), q(t.schedule.finish),
                              q(t.schedule.remainingStart.value_or("")),
                              !t.floatAvailable ? "미산정/해당없음" : QString::number(t.schedule.totalFloatMinutes),
                              !t.floatAvailable ? "미산정/해당없음" : QString::number(t.schedule.freeFloatMinutes),
                              !t.floatAvailable     ? "미산정/해당없음"
                              : t.schedule.critical ? "임계"
                                                    : "비임계"});
                    }
                    materials->setRowCount(int(r.materials.size()));
                    materials->setVisible(!r.materials.empty());
                    for (int i = 0; i < materials->rowCount(); ++i) {
                        const auto &m = r.materials[std::size_t(i)];
                        fill(materials, i,
                             {q(m.projectId + " / " + m.materialId), q(m.name), q(m.stock.unit), q(m.stock.at),
                              m.stock.remaining ? q(formatMaterialQuantity(*m.stock.remaining)) : "미정",
                              q(formatMaterialQuantity(m.remainingUse)), q(formatMaterialQuantity(m.incoming)),
                              QString::number(m.revision)});
                    }
                    const int count = int(std::min<std::size_t>(10000, r.shared.days.size()));
                    load->setRowCount(count);
                    for (int i = 0; i < count; ++i) {
                        const auto &d = r.shared.days[std::size_t(i)];
                        fill(load, i,
                             {q(d.sharedId), q(d.date), QString::number(d.peakUnits),
                              d.variableCapacity ? "시간별"
                              : d.capacity       ? QString::number(*d.capacity)
                                                 : "미정",
                              QString::number(d.overloadMinutes), QString::number(d.unknownCapacityMinutes)});
                    }
                    if (r.shared.days.size() > 10000)
                        status->setText(status->text() + " · 부하 화면은 첫 10,000행 (내보내기는 전체)");
                    for (auto *b : {navigate, json, xlsx, pdf})
                        b->setEnabled(true);
                } catch (const std::exception &e) {
                    invalidate();
                    showEditorError(*status, e);
                }
            });
    connect(navigate, &QPushButton::clicked, this, [&, state, result] {
        int row = result->currentRow();
        if (state->result && workspace.generation() == state->resultGeneration && row >= 0 &&
            row < int(state->result->tasks.size())) {
            const auto &ref = state->result->tasks[std::size_t(row)].ref;
            workspace.activate(ref.projectId, ref.taskId);
        }
    });
    auto exportResult = [&, state, status](int format) {
        try {
            if (!state->result || workspace.generation() != state->resultGeneration)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            const auto extension = format == 0 ? "json" : format == 1 ? "xlsx" : "pdf";
            auto path = QFileDialog::getSaveFileName(this, "통합 일정 snapshot 저장", {},
                                                     QString("통합 일정 (*.%1)").arg(extension));
            if (path.isEmpty() || !authorizeSandboxFile(this, path))
                return;
            if (!path.endsWith("." + QString(extension), Qt::CaseInsensitive))
                path += "." + QString(extension);
            if (workspace.generation() != state->resultGeneration)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            if (format == 0) {
                const auto bytes = integratedWorkspaceJson(state->config, *state->result, state->resultGeneration);
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                    throw AppError(ErrorCode::WORKSPACE_INVALID, {"파일 저장 실패"});
            } else {
                auto report = integratedWorkspaceReport(state->config, *state->result, state->resultGeneration,
                                                        s(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
                if (format == 1)
                    exportXlsx(report, path);
                else
                    exportPdf(report, path);
            }
            status->setText("통합 일정 snapshot 저장: " + path);
        } catch (const std::exception &e) {
            showEditorError(*status, e);
        }
    };
    connect(json, &QPushButton::clicked, this, [exportResult] { exportResult(0); });
    connect(xlsx, &QPushButton::clicked, this, [exportResult] { exportResult(1); });
    connect(pdf, &QPushButton::clicked, this, [exportResult] { exportResult(2); });
    for (auto *box : {forecast, level})
        connect(box, &QCheckBox::toggled, this, [invalidate](bool) { invalidate(); });
    connect(priority, &QComboBox::currentIndexChanged, this, [invalidate](int) { invalidate(); });
    connect(horizon, &QSpinBox::valueChanged, this, [invalidate](int) { invalidate(); });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [&, state, invalidate, status] {
        if (state->result && workspace.generation() != state->resultGeneration) {
            invalidate();
            status->setText(qDiagnosticText({"WORKSPACE_SOURCE_CHANGED", {}, {}}));
        }
    });
    timer->start(200);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);
    prepareEditorDialog(*this);
}
} // namespace ppm
