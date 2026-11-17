#include "ui/ResourcePlanningDialog.h"
#include "ui/EditorUi.h"
#include "application/Document.h"
#include "calculation/ResourcePlanning.h"
#include "persistence/ResourceCurveExchange.h"
#include "formatting/QtDiagnosticText.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
struct Preview {
    std::shared_ptr<Document::PreparedEdit> edit;
    QString summary;
};
struct PlanValue {
    std::int64_t duration = 0, work = 0;
    QString finish;
};
PlanValue value(Project p, const std::string &task, const std::string &resource) {
    p.calculationProfile = effortProfile;
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    p.levelingDecisions.clear();
    const auto plan = calculatePlan(p);
    if (plan.status != CalculationStatus::Valid)
        throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
    const auto t = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == task; });
    const auto r = std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &v) { return v.taskId == task; });
    TaskExecution model(p, *t);
    PlanValue out;
    out.duration = model.duration(*r);
    out.finish = q(r->finish);
    for (const auto &s : model.spans(*r))
        if (s.resourceId == resource)
            out.work += (s.finish - s.start) * s.units;
    return out;
}
} // namespace
struct ResourcePlanningDialog::Impl {
    ResourcePlanningDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::uint64_t session = 0, listener = 0;
    bool busy = false, committing = false, loading = false;
    std::shared_ptr<CalculationControl> control;
    std::shared_ptr<Document::PreparedEdit> prepared;
    QComboBox *resource, *assignment, *preset;
    QTabWidget *tabs;
    QLineEdit *unit, *rate, *date, *reference, *reporter, *reason, *quantity, *curveName;
    QTableWidget *points;
    QCheckBox *keepDuration, *confirm;
    QLabel *message, *history;
    QPushButton *saveRate, *clearRate, *analyze, *submit, *cancel, *reload, *clearEstimate, *clearCurve;
    Impl(ResourcePlanningDialog &d, Document &document) : dialog(d), doc(document) {
        auto *layout = new QVBoxLayout(&dialog);
        auto *note = new QLabel(
            "생산성은 물량/자원시간입니다. 단가·등급·가용량에서 추정하지 않습니다. 물량은 소수3자리이며 "
            "필요 공수는1/100 자원분으로 올림합니다.\n고정 기간은 기간을 유지합니다. 고정 공수·단위의 단일 자원은 "
            "배정률/곡선 유지와 기간 역산을 "
            "선택할 수 있습니다. 다중 자원은 기간 유지로 선택 공수를 배분하며 다른 자원의 공수를 보존합니다.\n"
            "계획만 편집하고 관측 원장·기준선·기존 시나리오는 유지합니다. 과거 산정/외부 곡선은 당시 정의이며 수동 "
            "편집·달력 변경 뒤에는 다시 검토하세요.",
            &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        assignment = new QComboBox(&dialog);
        assignment->setObjectName("resourcePlanningAssignment");
        layout->addWidget(assignment);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("resourcePlanningTabs");
        layout->addWidget(tabs, 1);
        auto line = [&](const char *name, int max = 4096) {
            auto *v = new QLineEdit(&dialog);
            v->setObjectName(name);
            v->setMaxLength(max);
            return v;
        };
        auto *production = new QWidget(tabs);
        auto *form = new QFormLayout(production);
        resource = new QComboBox(production);
        resource->setObjectName("productivityResource");
        form->addRow("생산성 정의 자원", resource);
        unit = line("productivityUnit", 64);
        rate = line("productivityRate", 24);
        date = line("productivityDate", 10);
        reference = line("productivityReference");
        form->addRow("물량 단위", unit);
        form->addRow("자원1단위·1시간당 물량", rate);
        form->addRow("기준일", date);
        form->addRow("측정/가정 근거", reference);
        auto *rateButtons = new QWidget(production);
        auto *rateRow = new QHBoxLayout(rateButtons);
        rateRow->setContentsMargins(0, 0, 0, 0);
        saveRate = new QPushButton("생산성 정의 저장", rateButtons);
        saveRate->setObjectName("saveProductivityRate");
        rateRow->addWidget(saveRate);
        clearRate = new QPushButton("정의 해제", rateButtons);
        clearRate->setObjectName("clearProductivityRate");
        rateRow->addWidget(clearRate);
        form->addRow(rateButtons);
        quantity = line("productivityQuantity", 24);
        form->addRow("선택 배정의 작업 물량", quantity);
        keepDuration = new QCheckBox("현재 계산 기간 유지 · 필요 배정률로 반영", production);
        keepDuration->setObjectName("productivityKeepDuration");
        form->addRow(keepDuration);
        clearEstimate = new QPushButton("선택 배정의 산정 기록만 해제", production);
        clearEstimate->setObjectName("clearProductivityEstimate");
        form->addRow(clearEstimate);
        tabs->addTab(production, "물량·생산성 → 계획 공수");
        auto *curves = new QWidget(tabs);
        auto *curvesLayout = new QVBoxLayout(curves);
        preset = new QComboBox(curves);
        preset->setObjectName("nativeCurvePreset");
        preset->addItems({"MS Flat", "MS Back Loaded", "MS Front Loaded", "MS Double Peak", "MS Early Peak",
                          "MS Late Peak", "MS Bell", "MS Turtle", "사용자 21지점"});
        curvesLayout->addWidget(preset);
        curveName = line("nativeCurveName", 1024);
        curveName->setPlaceholderText("곡선 이름 / 원본 정의 이름");
        curvesLayout->addWidget(curveName);
        points = new QTableWidget(21, 2, curves);
        points->setObjectName("nativeCurvePoints");
        points->setHorizontalHeaderLabels({"기간 지점(%)", "구간 공수 비중(%)"});
        points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for (int i = 0; i <= 20; ++i) {
            auto *x = new QTableWidgetItem(QString::number(i * 5));
            x->setFlags(x->flags() & ~Qt::ItemIsEditable);
            points->setItem(i, 0, x);
            points->setItem(i, 1, new QTableWidgetItem(i ? "5.00" : "0.00"));
        }
        curvesLayout->addWidget(points, 1);
        auto *hint =
            new QLabel("0% 지점은0, 나머지20개 비중의 합계는100%입니다. XML은 계획 시간대별 공수, XER은21지점 분포를 "
                       "교환합니다. 시간대별 실제공수는 계획 곡선으로 채택하지 않습니다. P6 투영의 세부 배정률 차이는 "
                       "교환 진단에 표시합니다.",
                       curves);
        hint->setWordWrap(true);
        curvesLayout->addWidget(hint);
        clearCurve = new QPushButton("원본 곡선 기록만 해제 · 계획 구간 유지", curves);
        clearCurve->setObjectName("clearNativeCurve");
        curvesLayout->addWidget(clearCurve);
        tabs->addTab(curves, "외부 고유 곡선·21지점");
        history = new QLabel(&dialog);
        history->setObjectName("resourcePlanningHistory");
        history->setWordWrap(true);
        history->setTextFormat(Qt::PlainText);
        layout->addWidget(history);
        auto *common = new QFormLayout;
        reporter = line("resourcePlanningReporter", 1024);
        reason = line("resourcePlanningReason");
        common->addRow("작성자", reporter);
        common->addRow("적용 근거/사유", reason);
        layout->addLayout(common);
        confirm =
            new QCheckBox("원래/변경 계획 공수·기간과 산정/곡선 근거를 검토했고 계획 변경을 확인했습니다.", &dialog);
        confirm->setObjectName("resourcePlanningConfirm");
        layout->addWidget(confirm);
        message = new QLabel(&dialog);
        message->setObjectName("resourcePlanningMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        reload = buttons->addButton("최신 문서 불러오기", QDialogButtonBox::ActionRole);
        reload->setObjectName("reloadResourcePlanning");
        analyze = buttons->addButton("검산 미리보기", QDialogButtonBox::ActionRole);
        analyze->setObjectName("previewResourcePlanning");
        submit = buttons->addButton("검토한 계획 적용", QDialogButtonBox::ActionRole);
        submit->setObjectName("applyResourcePlanning");
        cancel = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
        cancel->setObjectName("cancelResourcePlanning");
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(reload, &QPushButton::clicked, &dialog, [this] { load(); });
        QObject::connect(resource, &QComboBox::currentIndexChanged, &dialog, [this] {
            if (!loading) {
                const auto id = resource->currentData().toString().toStdString();
                int index = -1;
                if (source)
                    for (std::size_t i = 0; i < source->assignments.size(); ++i)
                        if (source->assignments[i].resourceId == id) {
                            index = int(i);
                            break;
                        }
                {
                    QSignalBlocker block(assignment);
                    assignment->setCurrentIndex(index);
                }
                loadRate();
                loadAssignment();
                changed();
            }
        });
        QObject::connect(assignment, &QComboBox::currentIndexChanged, &dialog, [this] {
            if (!loading) {
                loadAssignment();
                changed();
            }
        });
        QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [this] {
            if (preset->currentIndex() < 8)
                fill(mspWorkContour(preset->currentIndex()));
            changed();
        });
        QObject::connect(points, &QTableWidget::itemChanged, &dialog, [this] { changed(); });
        for (auto *v : {unit, rate, date, reference, reporter, reason, quantity, curveName})
            QObject::connect(v, &QLineEdit::textChanged, &dialog, [this] { changed(); });
        QObject::connect(keepDuration, &QCheckBox::toggled, &dialog, [this] { changed(); });
        QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, [this] { changed(); });
        QObject::connect(confirm, &QCheckBox::toggled, &dialog, [this] { controls(); });
        QObject::connect(saveRate, &QPushButton::clicked, &dialog, [this] { start("rate"); });
        QObject::connect(clearRate, &QPushButton::clicked, &dialog, [this] { start("rate", true); });
        QObject::connect(clearEstimate, &QPushButton::clicked, &dialog, [this] { start("estimate", true); });
        QObject::connect(clearCurve, &QPushButton::clicked, &dialog, [this] { start("curve", true); });
        QObject::connect(analyze, &QPushButton::clicked, &dialog,
                         [this] { start(tabs->currentIndex() == 0 ? "estimate" : "curve"); });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] { commit(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        listener = doc.subscribe([this](Document::Event) {
            if (!committing && (!current() || doc.readOnly())) {
                if (control)
                    control->cancel();
                prepared.reset();
                message->setText("문서·권한이 바뀌었습니다. 최신 문서를 불러와 다시 검토하세요.");
                controls();
            }
        });
        load();
        dialog.resize(1100, 900);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return source && doc.project() && session == doc.sessionGeneration() &&
               source->documentId == doc.project()->documentId && source->revision == doc.project()->revision;
    }
    const Assignment *selected() const {
        return source && assignment->currentIndex() >= 0
                   ? &source->assignments.at(assignment->currentData().toULongLong())
                   : nullptr;
    }
    void controls() {
        const bool edit = current() && !doc.readOnly() && !busy;
        for (auto *v : {unit, rate, date, reference, reporter, reason, quantity, curveName})
            v->setEnabled(edit);
        for (auto *v : {resource, assignment, preset})
            v->setEnabled(!busy);
        keepDuration->setEnabled(edit);
        saveRate->setEnabled(edit && resource->count());
        clearRate->setEnabled(edit && resource->count());
        clearEstimate->setEnabled(edit && selected() && selected()->productivityEstimate);
        clearCurve->setEnabled(edit && selected() && selected()->nativeCurve);
        points->setEditTriggers(edit ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                     : QAbstractItemView::NoEditTriggers);
        analyze->setEnabled(edit && selected());
        confirm->setEnabled(edit && prepared);
        submit->setEnabled(edit && prepared && confirm->isChecked());
        cancel->setEnabled(busy);
        reload->setEnabled(!busy);
    }
    void changed() {
        if (loading)
            return;
        prepared.reset();
        confirm->setChecked(false);
        controls();
    }
    void fill(const NativeCurve &c) {
        QSignalBlocker block(points);
        if (c.weights.size() == 21)
            for (int i = 0; i <= 20; ++i)
                points->item(i, 1)->setText(QString::number(c.weights[i] / 1000000.0, 'f', 6));
        curveName->setText(q(c.name));
    }
    void loadRate() {
        const auto id = resource->currentData().toString().toStdString();
        const Resource *r = nullptr;
        if (source)
            for (const auto &v : source->resources)
                if (v.id == id)
                    r = &v;
        const auto old = loading;
        loading = true;
        unit->setText(r && r->productivity ? q(r->productivity->unit) : QString{});
        rate->setText(r && r->productivity ? QString::number(r->productivity->quantityMilliPerHour / 1000.0, 'f', 3)
                                           : QString{});
        date->setText(r && r->productivity ? q(r->productivity->effectiveDate)
                                           : QDate::currentDate().toString(Qt::ISODate));
        reference->setText(r && r->productivity ? q(r->productivity->reference) : QString{});
        if (r && r->productivity)
            reporter->setText(q(r->productivity->reporter));
        loading = old;
    }
    void loadAssignment() {
        const auto old = loading;
        loading = true;
        const auto *a = selected();
        if (a) {
            resource->setCurrentIndex(resource->findData(q(a->resourceId)));
            loadRate();
            quantity->setText(a->productivityEstimate
                                  ? QString::number(a->productivityEstimate->quantityMilli / 1000.0, 'f', 3)
                                  : QString{});
            const auto t = std::find_if(source->tasks.begin(), source->tasks.end(),
                                        [&](const auto &v) { return v.id == a->taskId; });
            keepDuration->setChecked(t != source->tasks.end() && t->taskType == TaskType::FixedDuration);
            if (a->nativeCurve) {
                preset->setCurrentIndex(8);
                fill(*a->nativeCurve);
            } else {
                preset->setCurrentIndex(0);
                fill(mspWorkContour(0));
            }
            QString text;
            if (a->productivityEstimate) {
                const auto &e = *a->productivityEstimate;
                text = QString("저장 산정 · 물량 %1 %2 / 시간당 %3 · 공수 %4 h · %5 · %6\n")
                           .arg(e.quantityMilli / 1000.0)
                           .arg(q(e.rate.unit))
                           .arg(e.rate.quantityMilliPerHour / 1000.0)
                           .arg(q(plannedWorkHours(e.workUnitMinutes)),
                                (e.application == "duration-rates-v1" ? QString("기간 유지")
                                                                      : QString("배정률 유지·기간 역산")),
                                q(e.reason));
            }
            if (a->nativeCurve) {
                const auto &c = *a->nativeCurve;
                text += QString("보존 곡선 · %1 · 원본 ID %2 · %3 · 당시 기간 %4분/공수 %5 h")
                            .arg(q(c.source), q(c.externalId), q(c.name))
                            .arg(c.durationMinutes)
                            .arg(q(plannedWorkHours(c.workUnitMinutes)));
            }
            history->setText(text);
        } else
            history->clear();
        loading = old;
    }
    void load() {
        const auto oldResource = resource->currentData();
        const auto oldAssignment = assignment->currentIndex();
        if (control)
            control->cancel();
        prepared.reset();
        confirm->setChecked(false);
        source = doc.snapshot();
        session = doc.sessionGeneration();
        loading = true;
        resource->clear();
        assignment->clear();
        if (source) {
            for (const auto &r : source->resources)
                resource->addItem(q(r.name), q(r.id));
            for (std::size_t i = 0; i < source->assignments.size(); ++i) {
                const auto &a = source->assignments[i];
                auto t = std::find_if(source->tasks.begin(), source->tasks.end(),
                                      [&](const auto &v) { return v.id == a.taskId; });
                auto r = std::find_if(source->resources.begin(), source->resources.end(),
                                      [&](const auto &v) { return v.id == a.resourceId; });
                assignment->addItem(q(t->name + " · " + r->name), qulonglong(i));
            }
        }
        if (resource->findData(oldResource) >= 0)
            resource->setCurrentIndex(resource->findData(oldResource));
        if (oldAssignment >= 0 && oldAssignment < assignment->count())
            assignment->setCurrentIndex(oldAssignment);
        else if (oldResource.isValid())
            assignment->setCurrentIndex(-1);
        loadRate();
        loadAssignment();
        loading = false;
        message->setText(doc.readOnly()
                             ? "읽기 전용 · 생산성/원본 곡선/산정 기록 조회"
                             : "생산성 정의는 기존 계획을 바꾸지 않습니다. 물량 또는 곡선을 검산한 뒤 적용하세요.");
        controls();
    }
    void commit() {
        if (!current() || doc.readOnly() || busy || !prepared)
            return;
        committing = true;
        const auto state = doc.applyPreparedEdit(*prepared);
        committing = false;
        prepared.reset();
        if (state == EditState::Applied || state == EditState::NoChange)
            load();
        else {
            message->setText("문서·권한 변경/취소로 적용하지 않았습니다.");
            controls();
        }
    }
    void start(const std::string &action, bool clear = false) {
        if (!current() || doc.readOnly() || busy)
            return;
        try {
            ResourcePlanningSubmission s;
            s.action = action;
            s.resourceId = resource->currentData().toString().toStdString();
            s.reason = reason->text().trimmed().toStdString();
            s.reporter = reporter->text().trimmed().toStdString();
            s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
            s.confirmed = true;
            if (action == "rate") {
                if (!clear)
                    s.rate = ProductivityRate{unit->text().trimmed().toStdString(), date->text().toStdString(),
                                              reference->text().trimmed().toStdString(), s.reporter,
                                              curveDecimal(rate->text(), 1000, 1000000000)};
            } else {
                const auto *a = selected();
                if (!a)
                    return;
                s.taskId = a->taskId;
                s.resourceId = a->resourceId;
                s.keepDuration = keepDuration->isChecked();
                if (action == "estimate" && !clear) {
                    const auto r = std::find_if(source->resources.begin(), source->resources.end(),
                                                [&](const auto &v) { return v.id == a->resourceId; });
                    if (r->productivity &&
                        (unit->text().trimmed().toStdString() != r->productivity->unit ||
                         curveDecimal(rate->text(), 1000, 1000000000) != r->productivity->quantityMilliPerHour ||
                         date->text().toStdString() != r->productivity->effectiveDate ||
                         reference->text().trimmed().toStdString() != r->productivity->reference))
                        throw AppError(
                            Diagnostic{"RESOURCE_PLANNING_INVALID",
                                       {"화면의 생산성 정의가 저장값과 다릅니다. 정의를 먼저 저장한 뒤 산정하세요."},
                                       {},
                                       "RESOURCE_PLANNING_INVALID"});
                    s.quantityMilli = curveDecimal(quantity->text(), 1000, 1000000000);
                }
                if (action == "curve" && !clear) {
                    NativeCurve c;
                    c.source = "native-21";
                    c.externalId = "user";
                    c.name = curveName->text().trimmed().toStdString();
                    for (int i = 0; i <= 20; ++i)
                        c.weights.push_back(int(curveDecimal(points->item(i, 1)->text(), 1000000, 100000000)));
                    if (preset->currentIndex() < 8 && c.weights == mspWorkContour(preset->currentIndex()).weights) {
                        auto name = c.name;
                        c = mspWorkContour(preset->currentIndex());
                        c.name = name;
                    }
                    s.curve = std::move(c);
                }
            }
            auto request = doc.makeEditRequest(ResourcePlanningEdit{s});
            auto before = source;
            prepared.reset();
            confirm->setChecked(false);
            control = std::make_shared<CalculationControl>();
            busy = true;
            controls();
            message->setText("계획 공수·달력·구간·보존 범위 검산 중…");
            auto *watcher = new QFutureWatcher<Preview>(&dialog);
            const bool immediate = action == "rate" || clear;
            QObject::connect(watcher, &QFutureWatcher<Preview>::finished, &dialog, [this, watcher, immediate] {
                auto result = watcher->result();
                watcher->deleteLater();
                busy = false;
                if (!current() || control->cancelRequested.load()) {
                    message->setText("문서 변경 또는 취소로 적용하지 않았습니다.");
                    controls();
                    return;
                }
                if (result.edit->failure())
                    showEditorError(*message, AppError(*result.edit->failure()));
                else if (result.edit->state() == EditState::Ready || result.edit->state() == EditState::NoChange) {
                    prepared = result.edit;
                    message->setText("검산 완료 · 아직 문서에 적용하지 않았습니다.\n" + result.summary);
                    if (immediate) {
                        commit();
                        return;
                    }
                } else
                    message->setText("검산을 취소했습니다.");
                controls();
            });
            watcher->setFuture(QtConcurrent::run([request = std::move(request), before, s, control = control,
                                                  immediate]() mutable {
                Preview out;
                out.edit = Document::prepareEdit(std::move(request), control);
                if (!immediate && out.edit->state() == EditState::Ready) {
                    try {
                        EvaluationScope scope(control);
                        const auto a = value(*before, s.taskId, s.resourceId),
                                   b = value(*out.edit->previewProject(), s.taskId, s.resourceId);
                        out.summary = QString("선택 자원 계획 공수 %1 → %2 h · 계산 기간 %3 → %4분\n계획 완료 %5 → %6")
                                          .arg(q(plannedWorkHours(a.work)), q(plannedWorkHours(b.work)))
                                          .arg(a.duration)
                                          .arg(b.duration)
                                          .arg(a.finish, b.finish);
                    } catch (const std::exception &e) {
                        out.summary = qErrorText(e);
                    }
                }
                return out;
            }));
        } catch (const std::exception &e) {
            showEditorError(*message, e);
            controls();
        }
    }
};
ResourcePlanningDialog::ResourcePlanningDialog(Document &doc, QWidget *parent) : QDialog(parent) {
    setObjectName("resourcePlanningDialog");
    setWindowTitle("자원 생산성·외부 고유 곡선");
    impl_ = std::make_unique<Impl>(*this, doc);
}
ResourcePlanningDialog::~ResourcePlanningDialog() = default;
} // namespace jobs
