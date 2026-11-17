#include "ui/FinancialPeriodDialog.h"
#include "ui/EditorUi.h"
#include "ui/ReportDialog.h"
#include "domain/FinancialPeriods.h"
#include "calculation/FinancialPeriodReports.h"
#include "formatting/Money.h"
#include "formatting/QtDiagnosticText.h"
#include "reporting/ReportExport.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string s(const QString &v) {
    return v.toStdString();
}
class ValuesModel final : public QAbstractTableModel {
public:
    struct Row {
        std::string id, name;
        QString pv, ev;
    };
    std::vector<Row> rows;
    bool editable = true;
    explicit ValuesModel(QObject *parent) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows.size());
    }
    int columnCount(const QModelIndex & = {}) const override {
        return 4;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return o == Qt::Horizontal && role == Qt::DisplayRole
                   ? QVariant(QStringList{"기준선 작업", "작업 ID", "기간 예산 PV", "기간 인정 EV"}.value(c))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
            return {};
        const auto &r = rows.at(std::size_t(i.row()));
        return QStringList{q(r.name), q(r.id), r.pv, r.ev}.value(i.column());
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        return QAbstractTableModel::flags(i) | (editable && i.column() >= 2 ? Qt::ItemIsEditable : Qt::NoItemFlags);
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (!i.isValid() || !editable || i.column() < 2 || role != Qt::EditRole)
            return false;
        auto &r = rows.at(std::size_t(i.row()));
        (i.column() == 2 ? r.pv : r.ev) = v.toString();
        emit dataChanged(i, i, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    void load(const Baseline *b, const FinancialPeriodDefinition *d, const std::string &currency) {
        beginResetModel();
        rows.clear();
        std::map<std::string, const FinancialPeriodValue *> index;
        if (d)
            for (const auto &v : d->values)
                index[v.taskId] = &v;
        if (b)
            for (const auto &t : b->input.tasks) {
                Row r{t.id, t.name, {}, {}};
                if (auto it = index.find(t.id); it != index.end()) {
                    r.pv = moneyui::format(it->second->budget, currency, "");
                    r.ev = moneyui::format(it->second->earned, currency, "");
                }
                rows.push_back(std::move(r));
            }
        endResetModel();
    }
    std::vector<FinancialPeriodValue> values(const std::string &currency) const {
        std::vector<FinancialPeriodValue> out;
        for (const auto &r : rows) {
            auto ev = r.ev.trimmed();
            bool negative = ev.startsWith('-');
            auto earned = moneyui::parse(negative ? ev.mid(1) : ev, currency);
            if (negative && !earned)
                throw std::runtime_error("EV 정정 금액을 입력하세요.");
            if (negative)
                earned = -*earned;
            auto budget = moneyui::parse(r.pv, currency);
            if (budget || earned)
                out.push_back({r.id, budget, earned});
        }
        return out;
    }
};
class PreviewModel final : public QAbstractTableModel {
    std::shared_ptr<const ProjectReport> report_;

public:
    PreviewModel(std::shared_ptr<const ProjectReport> r, QObject *p) : QAbstractTableModel(p), report_(std::move(r)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(1 + report_->tables[1].rows.size());
    }
    int columnCount(const QModelIndex & = {}) const override {
        return int(report_->tables[0].columns.size());
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return o == Qt::Horizontal && role == Qt::DisplayRole
                   ? QVariant(q(report_->tables[0].columns.at(std::size_t(c)).title))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || role != Qt::DisplayRole)
            return {};
        const auto &row =
            i.row() == 0 ? report_->tables[0].rows[0] : report_->tables[1].rows.at(std::size_t(i.row() - 1));
        return displayCell(row.at(std::size_t(i.column())), report_->currency);
    }
};
struct Prepared {
    std::shared_ptr<Document::PreparedEdit> edit;
    std::shared_ptr<const ProjectReport> report;
    QString error;
};
} // namespace
struct FinancialPeriodDialog::Impl {
    FinancialPeriodDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::uint64_t session = 0, listener = 0, generation = 0;
    bool busy = false, committing = false, loading = false, stale = false, unsaved = false;
    QComboBox *period, *baseline, *action, *formula;
    QLineEdit *name, *reporter, *reason;
    QDateEdit *from, *to;
    ValuesModel *values;
    QTableView *grid, *preview;
    QTableWidget *history;
    QTabWidget *tabs;
    QLabel *message;
    QCheckBox *confirmed;
    QPushButton *prepare, *apply, *cancel, *report;
    std::shared_ptr<CalculationControl> control;
    std::shared_ptr<Prepared> prepared;
    Impl(FinancialPeriodDialog &dlg, Document &d, const std::string &bid) : dialog(dlg), doc(d) {
        dialog.setObjectName("financialPeriodDialog");
        dialog.setWindowTitle("사용자 정의 기간·기간 마감");
        dialog.resize(1100, 760);
        auto *body = new QVBoxLayout(&dialog);
        auto *note = new QLabel(
            "기준선별 겹치지 않는 기간을 정의합니다. 빈 금액은 native 기간값, 0은 명시0, EV 음수는 정정입니다.\n"
            "마감은 당시 revision의 값·근거를 고정합니다. 마감 이전 실적 정정은 최신 기간부터 재개방하세요.");
        note->setWordWrap(true);
        body->addWidget(note);
        tabs = new QTabWidget;
        tabs->setObjectName("financialPeriodTabs");
        body->addWidget(tabs, 1);
        auto *editor = new QWidget;
        auto *editLayout = new QVBoxLayout(editor);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto *formWidget = new QWidget;
        auto *form = new QFormLayout(formWidget);
        period = new QComboBox;
        period->setObjectName("financialPeriodChoice");
        baseline = new QComboBox;
        baseline->setObjectName("financialPeriodBaseline");
        action = new QComboBox;
        action->setObjectName("financialPeriodAction");
        action->addItem("기간 정의 저장", "define");
        action->addItem("기간 마감", "close");
        action->addItem("마감 재개방", "reopen");
        name = new QLineEdit;
        name->setObjectName("financialPeriodName");
        from = new QDateEdit;
        to = new QDateEdit;
        from->setObjectName("financialPeriodFrom");
        to->setObjectName("financialPeriodTo");
        for (auto *date : {from, to}) {
            date->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
            date->setDisplayFormat("yyyy-MM-dd");
            date->setCalendarPopup(true);
        }
        formula = new QComboBox;
        formula->setObjectName("financialPeriodFormula");
        formula->addItem("예산 잔여", "budget-remaining-v1");
        formula->addItem("CPI", "cpi-v1");
        formula->addItem("CPI × SPI", "cpi-spi-v1");
        reporter = new QLineEdit;
        reason = new QLineEdit;
        reporter->setObjectName("financialPeriodReporter");
        reason->setObjectName("financialPeriodReason");
        form->addRow("사용자 기간", period);
        form->addRow("동작", action);
        form->addRow("이름", name);
        form->addRow("승인 기준선", baseline);
        form->addRow("시작일 (포함)", from);
        form->addRow("종료일 (포함)", to);
        form->addRow("native 누계 추정식", formula);
        form->addRow("작성자", reporter);
        form->addRow("사유", reason);
        scroll->setWidget(formWidget);
        editLayout->addWidget(scroll, 1);
        grid = new QTableView;
        grid->setObjectName("financialPeriodValues");
        values = new ValuesModel(grid);
        grid->setModel(values);
        grid->horizontalHeader()->setStretchLastSection(true);
        grid->setColumnWidth(0, 210);
        grid->setColumnWidth(1, 280);
        grid->setColumnWidth(2, 150);
        editLayout->addWidget(grid, 1);
        tabs->addTab(editor, "기간 정의 / 작업별 금액");
        preview = new QTableView;
        preview->setObjectName("financialPeriodPreview");
        preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tabs->addTab(preview, "검토 값");
        history = new QTableWidget(0, 8);
        history->setObjectName("financialPeriodHistory");
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->setHorizontalHeaderLabels(
            {"기간", "동작", "시작일", "종료일", "지식 revision", "작성자 / 사유", "이력 ID", "기록 UTC"});
        tabs->addTab(history, "불변 마감 이력");
        confirmed = new QCheckBox("기간·금액·미정·마감 근거와 입력 잠금을 확인했습니다.");
        confirmed->setObjectName("confirmFinancialPeriod");
        body->addWidget(confirmed);
        message = new QLabel;
        message->setObjectName("financialPeriodMessage");
        message->setWordWrap(true);
        message->setTextFormat(Qt::PlainText);
        body->addWidget(message);
        auto *buttons = new QHBoxLayout;
        body->addLayout(buttons);
        prepare = new QPushButton("검토 계산");
        apply = new QPushButton("확인 후 반영");
        cancel = new QPushButton("계산 취소");
        report = new QPushButton("기간 보고서…");
        auto *close = new QPushButton("닫기");
        prepare->setObjectName("prepareFinancialPeriod");
        apply->setObjectName("applyFinancialPeriod");
        cancel->setObjectName("cancelFinancialPeriod");
        report->setObjectName("financialPeriodReport");
        for (auto *b : {prepare, apply, cancel, report, close})
            buttons->addWidget(b);
        reload({}, bid);
        QObject::connect(period, &QComboBox::currentIndexChanged, &dialog, [this] {
            if (!loading)
                loadPeriod();
        });
        QObject::connect(baseline, &QComboBox::currentIndexChanged, &dialog, [this] {
            if (!loading) {
                loadValues(nullptr);
                invalidate();
            }
        });
        QObject::connect(action, &QComboBox::currentIndexChanged, &dialog, [this] {
            invalidate();
            controls();
        });
        QObject::connect(formula, &QComboBox::currentIndexChanged, &dialog, [this] { invalidate(); });
        for (auto *v : {name, reporter, reason})
            QObject::connect(v, &QLineEdit::textChanged, &dialog, [this] { invalidate(); });
        for (auto *v : {from, to})
            QObject::connect(v, &QDateEdit::dateChanged, &dialog, [this] { invalidate(); });
        QObject::connect(values, &QAbstractItemModel::dataChanged, &dialog, [this] { invalidate(); });
        QObject::connect(confirmed, &QCheckBox::toggled, &dialog, [this] { controls(); });
        QObject::connect(prepare, &QPushButton::clicked, &dialog, [this] { start(); });
        QObject::connect(apply, &QPushButton::clicked, &dialog, [this] { commit(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            invalidate();
            message->setText("계산을 취소했습니다.");
        });
        QObject::connect(close, &QPushButton::clicked, &dialog, [this] { dialog.reject(); });
        QObject::connect(report, &QPushButton::clicked, &dialog, [this] {
            if (!source || period->currentData().toString().isEmpty())
                return;
            ReportDialog view(doc, "", s(baseline->currentData().toString()), &dialog, true,
                              s(period->currentData().toString()));
            view.exec();
        });
        listener = doc.subscribe([this](Document::Event) {
            if (!committing) {
                stale = true;
                invalidate();
                message->setText("문서가 변경되었습니다. 창을 다시 열어 최신 상태를 조회하세요.");
            }
        });
        prepareEditorDialog(dialog);
        controls();
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return source && !stale && doc.project() && source->documentId == doc.project()->documentId &&
               source->revision == doc.project()->revision && session == doc.sessionGeneration() &&
               unsaved == !doc.revisionSaved();
    }
    void invalidate() {
        if (loading)
            return;
        if (control)
            control->cancel();
        ++generation;
        busy = false;
        prepared.reset();
        confirmed->setChecked(false);
        message->setText("초안이 변경되었습니다. 검토 계산을 다시 실행하세요.");
        if (auto *m = preview->model()) {
            preview->setModel(nullptr);
            m->deleteLater();
        }
        controls();
    }
    void reload(const std::string &id, const std::string &bid = {}) {
        loading = true;
        source = doc.snapshot();
        session = doc.sessionGeneration();
        unsaved = !doc.revisionSaved();
        stale = false;
        period->clear();
        baseline->clear();
        period->addItem("새 사용자 기간", "");
        if (source) {
            for (const auto &b : source->baselines)
                baseline->addItem(q(b.name), q(b.id));
            for (const auto &[key, e] : financialPeriods(*source))
                period->addItem(q(e->period.name) + (e->action == "close" ? " · 마감" : " · 열림"), q(key));
            history->setRowCount(int(source->financialPeriodEvents.size()));
            int i = 0;
            for (const auto &e : source->financialPeriodEvents) {
                const QStringList row{q(e.period.name),
                                      q(e.action),
                                      q(e.period.fromDate),
                                      q(e.period.toDate),
                                      QString::number(e.knowledgeRevision),
                                      q(e.reporter + " / " + e.reason),
                                      q(e.id),
                                      q(e.recordedAt)};
                for (int c = 0; c < row.size(); ++c)
                    history->setItem(i, c, new QTableWidgetItem(row[c]));
                ++i;
            }
        }
        baseline->setCurrentIndex(std::max(0, baseline->findData(q(bid))));
        period->setCurrentIndex(std::max(0, period->findData(q(id))));
        loading = false;
        loadPeriod();
    }
    void loadValues(const FinancialPeriodDefinition *def) {
        const Baseline *b = nullptr;
        if (source)
            for (const auto &v : source->baselines)
                if (v.id == s(baseline->currentData().toString()))
                    b = &v;
        values->load(b, def, source ? source->currency : "KRW");
    }
    void loadPeriod() {
        invalidate();
        loading = true;
        const auto id = s(period->currentData().toString());
        const FinancialPeriodEvent *e = source && !id.empty() ? &financialPeriod(*source, id) : nullptr;
        name->setText(e ? q(e->period.name) : "");
        if (e)
            baseline->setCurrentIndex(baseline->findData(q(e->period.baselineId)));
        const auto date = source ? QDate::fromString(q(source->startDate), Qt::ISODate) : QDate::currentDate();
        from->setDate(e ? QDate::fromString(q(e->period.fromDate), Qt::ISODate) : date);
        to->setDate(e ? QDate::fromString(q(e->period.toDate), Qt::ISODate) : date.addDays(6));
        action->setCurrentIndex(e && e->action == "close" ? 2 : 0);
        loadValues(e ? &e->period : nullptr);
        loading = false;
        controls();
    }
    void controls() {
        const bool available = current() && !busy && !doc.readOnly();
        const bool defining = action->currentData() == "define";
        const auto id = s(period->currentData().toString());
        const bool closed = source && !id.empty() && financialPeriod(*source, id).action == "close";
        const bool editable = available && defining && !closed;
        for (auto *w : std::vector<QWidget *>{name, baseline, from, to, grid})
            w->setEnabled(editable);
        values->editable = editable;
        period->setEnabled(!busy);
        action->setEnabled(available);
        formula->setEnabled(available);
        reporter->setEnabled(available);
        reason->setEnabled(available);
        prepare->setEnabled(available && (defining ? !closed : !id.empty()));
        confirmed->setEnabled(available && bool(prepared));
        apply->setEnabled(available && prepared && confirmed->isChecked());
        cancel->setEnabled(busy || bool(prepared));
        report->setEnabled(current() && !busy && !id.empty());
    }
    void start() {
        invalidate();
        if (!current() || doc.readOnly())
            return;
        try {
            FinancialPeriodSubmission sub;
            sub.action = s(action->currentData().toString());
            sub.period.id = s(period->currentData().toString());
            if (sub.action == "define") {
                sub.period.name = s(name->text().trimmed());
                sub.period.baselineId = s(baseline->currentData().toString());
                sub.period.fromDate = s(from->date().toString(Qt::ISODate));
                sub.period.toDate = s(to->date().toString(Qt::ISODate));
                sub.period.values = values->values(source->currency);
            } else
                sub.period = financialPeriod(*source, sub.period.id).period;
            sub.formula = s(formula->currentData().toString());
            sub.reporter = s(reporter->text().trimmed());
            sub.reason = s(reason->text().trimmed());
            sub.recordedAt = s(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"));
            sub.confirmed = true;
            auto request = doc.makeEditRequest(FinancialPeriodEdit{sub});
            control = std::make_shared<CalculationControl>();
            const auto c = control;
            const auto version = ++generation;
            busy = true;
            controls();
            message->setText("기간 정의/마감 검증 중…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Prepared>>(&dialog);
            QObject::connect(
                watcher, &QFutureWatcher<std::shared_ptr<Prepared>>::finished, &dialog, [this, watcher, version, c] {
                    auto result = watcher->result();
                    watcher->deleteLater();
                    if (version != generation)
                        return;
                    busy = false;
                    if (!current() || c->cancelRequested.load())
                        message->setText("취소/문서 변경으로 반영하지 않았습니다.");
                    else if (!result->error.isEmpty())
                        message->setText(result->error);
                    else {
                        prepared = result;
                        if (result->report) {
                            preview->setModel(new PreviewModel(result->report, preview));
                            tabs->setCurrentWidget(preview);
                        }
                        message->setText("검증 완료. 기간값과 미정, 마감 시 입력 잠금을 확인한 뒤 반영하세요.");
                    }
                    controls();
                });
            watcher->setFuture(QtConcurrent::run([p = source, sub, c, request = std::move(request)]() mutable {
                auto out = std::make_shared<Prepared>();
                try {
                    EvaluationScope evaluation(c);
                    out->edit = Document::prepareEdit(std::move(request), c);
                    if (out->edit->state() != EditState::Ready) {
                        out->error =
                            out->edit->failure() ? q(diagnosticMessage(*out->edit->failure())) : q(out->edit->error());
                        if (out->error.isEmpty())
                            out->error = "기간 검증 실패 또는 취소";
                        return out;
                    }
                    if (sub.action != "define") {
                        ReportOptions o;
                        o.formula = sub.formula;
                        o.reportId = "financial-period-preview";
                        o.generatedAt = sub.recordedAt;
                        out->report = std::make_shared<ProjectReport>(buildFinancialPeriodReport(*p, o, sub.period.id));
                    }
                    calculationCheckpoint();
                } catch (const CalculationCancelled &) {
                    out->error = "계산을 취소했습니다.";
                } catch (const std::exception &e) {
                    out->error = qErrorText(e);
                }
                return out;
            }));
        } catch (const std::exception &e) {
            message->setText(qErrorText(e));
            controls();
        }
    }
    void commit() {
        if (!current() || busy || doc.readOnly() || !prepared || !confirmed->isChecked())
            return;
        const auto id = s(period->currentData().toString());
        committing = true;
        const auto result = doc.applyPreparedEdit(*prepared->edit);
        committing = false;
        invalidate();
        if (result == EditState::Applied) {
            const auto selected = doc.project()->financialPeriodEvents.back().period.id;
            reload(selected);
            message->setText("새 불변 이력으로 반영했습니다.");
            tabs->setCurrentIndex(2);
        } else {
            message->setText("문서/권한/저장 상태가 바뀌어 반영하지 않았습니다.");
            controls();
        }
        (void)id;
    }
};
FinancialPeriodDialog::FinancialPeriodDialog(Document &d, const std::string &b, QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, d, b)) {}
FinancialPeriodDialog::~FinancialPeriodDialog() = default;
void FinancialPeriodDialog::reject() {
    impl_->invalidate();
    QDialog::reject();
}
} // namespace jobs
