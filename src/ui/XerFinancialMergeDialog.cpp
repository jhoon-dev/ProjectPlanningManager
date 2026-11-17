#include "ui/XerFinancialMergeDialog.h"
#include "persistence/XerFinancialJson.h"
#include "formatting/Money.h"
#include "formatting/DiagnosticText.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <set>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
using Pair = std::pair<std::string, std::string>;
class TotalsModel final : public QAbstractTableModel {
    const Project &source_;
    std::vector<XerFinancialAssignment> initial_, rows_;
    std::map<std::string, QString> tasks_, resources_;
    std::function<void()> changed_;
    std::function<void(const std::exception &)> error_;

public:
    TotalsModel(const Project &p, QObject *parent, std::function<void()> changed,
                std::function<void(const std::exception &)> error)
        : QAbstractTableModel(parent), source_(p), changed_(std::move(changed)), error_(std::move(error)) {
        std::map<Pair, XerFinancialAssignment> base;
        if (auto *s = activeXerFinancials(p))
            for (const auto &a : s->assignments)
                base[{a.taskId, a.resourceId}] = a;
        for (const auto &t : p.tasks)
            tasks_[t.id] = q(t.name);
        for (const auto &r : p.resources)
            resources_[r.id] = q(r.name);
        for (const auto &a : p.assignments) {
            const Pair key{a.taskId, a.resourceId};
            rows_.push_back(base.contains(key) ? base.at(key)
                                               : XerFinancialAssignment{a.taskId, a.resourceId, {}, {}, {}, {}});
        }
        initial_ = rows_;
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 6;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (o == Qt::Vertical)
            return c + 1;
        return QStringList{"작업", "자원", "정규 공수 h/u", "초과 공수 h/u", "정규 원가", "초과 원가"}.value(c);
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        return QAbstractTableModel::flags(i) | (i.column() >= 2 ? Qt::ItemIsEditable : Qt::NoItemFlags);
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
            return {};
        const auto &a = rows_.at(std::size_t(i.row()));
        auto work = [](auto n) { return n ? QString::number(*n) + "u" : QString{}; };
        return QStringList{tasks_.at(a.taskId),
                           resources_.at(a.resourceId),
                           work(a.regularWork),
                           work(a.overtimeWork),
                           moneyui::format(a.regularMinor, source_.currency, ""),
                           moneyui::format(a.overtimeMinor, source_.currency, "")}
            .value(i.column());
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (role != Qt::EditRole || i.column() < 2)
            return false;
        try {
            auto &a = rows_.at(std::size_t(i.row()));
            const auto text = v.toString().trimmed();
            if (i.column() == 2 || i.column() == 3) {
                const auto n = text.isEmpty() ? std::optional<std::int64_t>{}
                                              : std::optional{parseActualHours(text.toStdString())};
                (i.column() == 2 ? a.regularWork : a.overtimeWork) = n;
            } else
                (i.column() == 4 ? a.regularMinor : a.overtimeMinor) = moneyui::parse(text, source_.currency);
            emit dataChanged(i, i);
            changed_();
            return true;
        } catch (const std::exception &e) {
            error_(e);
            return false;
        }
    }
    std::vector<XerFinancialAssignment> overrides() const {
        std::vector<XerFinancialAssignment> out;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i] != initial_[i])
                out.push_back(rows_[i]);
        return out;
    }
    void reset() {
        beginResetModel();
        rows_ = initial_;
        endResetModel();
        changed_();
    }
};
class IssuesModel final : public QAbstractTableModel {
    const Project &source_;
    std::map<PeriodActualKey, const PeriodActualEntry *> native_;
    std::vector<XerFinancialMergeIssue> rows_;
    std::set<std::size_t> &choices_;
    std::function<void()> changed_;
    std::map<std::string, QString> names_;

public:
    IssuesModel(const Project &p, std::set<std::size_t> &choices, QObject *parent, std::function<void()> changed)
        : QAbstractTableModel(parent), source_(p), choices_(choices), changed_(std::move(changed)) {
        if (p.statusAt)
            native_ = periodActualsAsOf(p, *p.statusAt);
        for (const auto &t : p.tasks)
            names_[t.id] = q(t.name);
        for (const auto &r : p.resources)
            names_[r.id] = q(r.name);
    }
    void setRows(std::vector<XerFinancialMergeIssue> rows) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 6;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        return o == Qt::Vertical ? QVariant(c + 1)
                                 : QVariant(QStringList{"기간 교체 선택", "작업 / 자원", "기간", "원본 공수u / 원가",
                                                        "현재 공수u / 원가", "검토 내용"}
                                                .value(c));
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        auto f = QAbstractTableModel::flags(i);
        if (i.column() == 0 && rows_.at(std::size_t(i.row())).periodIndex)
            f |= Qt::ItemIsUserCheckable;
        return f;
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid())
            return {};
        const auto &r = rows_.at(std::size_t(i.row()));
        if (role == Qt::CheckStateRole && i.column() == 0 && r.periodIndex)
            return choices_.contains(*r.periodIndex) ? Qt::Checked : Qt::Unchecked;
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
            return {};
        const auto name = [&](const auto &id) {
            auto it = names_.find(id);
            return it == names_.end() ? q(id) : it->second;
        };
        QString original = "—", current = "—";
        if (r.periodIndex) {
            const auto &period = activeXerFinancials(source_)->periods.at(*r.periodIndex);
            const auto text = [&](std::optional<std::int64_t> work, std::optional<std::int64_t> cost) {
                return (work ? QString::number(*work) + "u" : QString("—")) + " / " +
                       moneyui::format(cost, source_.currency);
            };
            original = text(period.resourceId.empty() ? std::nullopt : period.work,
                            period.resourceId.empty() ? period.expenseMinor : period.actualMinor);
            std::optional<std::int64_t> work, cost;
            for (const auto &[key, e] : native_) {
                (void)key;
                if (e->taskId != period.taskId || e->resourceId != period.resourceId ||
                    e->date < period.start.substr(0, 10) || e->date > period.finish.substr(0, 10))
                    continue;
                if (e->workUnitMinutes)
                    work = work.value_or(0) + *e->workUnitMinutes;
                if (e->actualMinor)
                    cost = cost.value_or(0) + *e->actualMinor;
            }
            current = text(work, cost);
        }
        return QStringList{r.periodIndex ? "현재 원장으로 교체" : "별도 정정 필요",
                           name(r.taskId) + (r.resourceId.empty() ? "" : " / " + name(r.resourceId)),
                           q(r.start) + " ~ " + q(r.finish),
                           original,
                           current,
                           q(r.message)}
            .value(i.column());
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (role != Qt::CheckStateRole || i.column() != 0)
            return false;
        const auto p = rows_.at(std::size_t(i.row())).periodIndex;
        if (!p)
            return false;
        if (v.toInt() == Qt::Checked)
            choices_.insert(*p);
        else
            choices_.erase(*p);
        emit dataChanged(i, i);
        changed_();
        return true;
    }
};
class BalanceModel final : public QAbstractTableModel {
    struct Row {
        QString name;
        std::optional<std::int64_t> ac;
        std::int64_t native = 0, resource = 0, expense = 0;
    };
    std::vector<Row> rows_;
    std::string currency_;

public:
    explicit BalanceModel(QObject *parent) : QAbstractTableModel(parent) {}
    void set(const Project &p, const XerFinancialSnapshot &s) {
        beginResetModel();
        rows_.clear();
        currency_ = p.currency;
        auto native = actualCostAllocations(p, *p.statusAt);
        std::map<std::string, std::int64_t> resources, expenses;
        for (const auto &a : s.assignments)
            resources[a.taskId] += a.regularMinor.value_or(0) + a.overtimeMinor.value_or(0);
        for (const auto &e : s.periods)
            if (e.resourceId.empty())
                expenses[e.taskId] += e.expenseMinor.value_or(0);
        for (const auto &t : s.tasks)
            rows_.push_back(
                {q(t.name), native[t.id].cumulative, native[t.id].allocated, resources[t.id], expenses[t.id]});
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 6;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        return o == Qt::Vertical ? QVariant(c + 1)
                                 : QVariant(QStringList{"작업", "현재 누계 AC", "현재 날짜 원가", "병합 자원 누계",
                                                        "병합 경비 명세", "집계 미배분"}
                                                .value(c));
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || role != Qt::DisplayRole)
            return {};
        const auto &r = rows_.at(std::size_t(i.row()));
        if (i.column() == 0)
            return r.name;
        if (i.column() == 1)
            return moneyui::format(r.ac, currency_);
        if (i.column() == 2)
            return moneyui::format(r.native, currency_);
        if (i.column() == 3)
            return moneyui::format(r.resource, currency_);
        if (i.column() == 4)
            return moneyui::format(r.expense, currency_);
        return moneyui::format(r.ac ? std::optional<std::int64_t>(*r.ac - r.resource - r.expense) : std::nullopt,
                               currency_);
    }
};
struct MergePreview {
    XerFinancialMergeResult result;
    std::shared_ptr<Document::PreparedEdit> edit;
    QString error;
    bool cancelled = false;
};
} // namespace
struct XerFinancialMergeDialog::Impl {
    XerFinancialMergeDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::optional<XerRelinkAudit> relink;
    std::uint64_t session = 0, listener = 0;
    bool busy = false, committing = false;
    std::set<std::size_t> choices;
    TotalsModel *totals;
    IssuesModel *issues;
    BalanceModel *balance;
    QTabWidget *tabs;
    QTableWidget *details;
    QLabel *message;
    QLineEdit *reporter, *reason, *search;
    QPushButton *analyze, *submit, *cancel, *reset;
    std::shared_ptr<MergePreview> prepared;
    std::shared_ptr<CalculationControl> control;
    Impl(XerFinancialMergeDialog &d, Document &document, std::optional<XerRelinkAudit> incoming)
        : dialog(d), doc(document), relink(std::move(incoming)) {
        source = std::make_shared<const Project>(relink ? projectForXerRelink(*doc.project(), *relink)
                                                        : (doc.project() ? *doc.project() : Project{}));
        session = doc.sessionGeneration();
        auto *body = new QVBoxLayout(&dialog);
        auto *note = new QLabel("현재 날짜 원장과 누계 AC는 유지하며 원본/최신 집계의 미배분 잔량에 날짜 명세를 "
                                "연결합니다.\n날짜 없는 집계와 현재 날짜 실적이 겹치면 기간 교체를 선택한 뒤 다시 "
                                "분석하세요. 정규/초과근무 구분이 부족하면 누계 구분 탭에서 명시 입력하세요.\n병합은 "
                                "새 이력이며 원본과 이전 기록을 지우지 않습니다. 검증된 미리보기만 제출합니다.",
                                &dialog);
        note->setTextFormat(Qt::PlainText);
        if (relink)
            note->setText("새 원본 " + q(relink->incoming.sourceName) + " · 프로젝트 " + q(relink->incoming.projectId) +
                          "\n" + note->text());
        note->setWordWrap(true);
        note->setText("상태일 " + q(source->statusAt.value_or("미설정")) + " · 통화 " + q(source->currency) + "\n" +
                      note->text());
        body->addWidget(note);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("xerMergeTabs");
        body->addWidget(tabs, 1);
        auto changed = [this] { invalidate(); };
        auto error = [this](const std::exception &e) { showEditorError(*message, e); };
        auto *review = new QTableView(tabs);
        review->setObjectName("xerMergeIssues");
        issues = new IssuesModel(*source, choices, review, changed);
        review->setModel(issues);
        review->horizontalHeader()->setDefaultSectionSize(170);
        review->horizontalHeader()->setStretchLastSection(true);
        review->setWordWrap(false);
        tabs->addTab(review, "병합 검토·충돌");
        auto *totalPage = new QWidget(tabs);
        auto *totalLayout = new QVBoxLayout(totalPage);
        search = new QLineEdit(totalPage);
        search->setPlaceholderText("작업·자원 검색");
        totalLayout->addWidget(search);
        auto *table = new QTableView(totalPage);
        table->setObjectName("xerMergeTotals");
        totals = new TotalsModel(*source, table, changed, error);
        auto *proxy = new QSortFilterProxyModel(table);
        proxy->setSourceModel(totals);
        proxy->setFilterKeyColumn(-1);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        table->setModel(proxy);
        table->horizontalHeader()->setDefaultSectionSize(160);
        totalLayout->addWidget(table);
        QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        tabs->addTab(totalPage, "정규·초과 누계 구분");
        auto *costs = new QTableView(tabs);
        costs->setObjectName("xerMergeBalance");
        balance = new BalanceModel(costs);
        costs->setModel(balance);
        costs->horizontalHeader()->setDefaultSectionSize(165);
        tabs->addTab(costs, "원가 대사");
        details = new QTableWidget(0, 4, tabs);
        details->setObjectName("xerMergeLedger");
        details->setHorizontalHeaderLabels({"작업 / 자원", "실제 날짜", "공수 u", "원가"});
        details->setEditTriggers(QAbstractItemView::NoEditTriggers);
        details->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tabs->addTab(details, "연결할 날짜 원장");
        auto *form = new QFormLayout;
        reporter = new QLineEdit(&dialog);
        reporter->setObjectName("xerMergeReporter");
        reason = new QLineEdit(&dialog);
        reason->setObjectName("xerMergeReason");
        form->addRow("작성자", reporter);
        form->addRow("병합 사유", reason);
        body->addLayout(form);
        message = new QLabel("분석을 실행해 자동 병합 결과와 충돌을 확인하세요.", &dialog);
        message->setObjectName("xerMergeMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        body->addWidget(message);
        auto *actions = new QHBoxLayout;
        analyze = new QPushButton("병합 분석·미리보기", &dialog);
        analyze->setObjectName("xerMergeAnalyze");
        submit = new QPushButton("검증된 병합 제출", &dialog);
        submit->setObjectName("xerMergeSubmit");
        cancel = new QPushButton("분석 취소", &dialog);
        cancel->setObjectName("xerMergeCancel");
        reset = new QPushButton("선택 초기화", &dialog);
        reset->setObjectName("xerMergeReset");
        auto *close = new QPushButton("닫기", &dialog);
        actions->addWidget(analyze);
        actions->addWidget(submit);
        actions->addWidget(cancel);
        actions->addWidget(reset);
        actions->addStretch();
        actions->addWidget(close);
        body->addLayout(actions);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(reset, &QPushButton::clicked, &dialog, [this] {
            choices.clear();
            totals->reset();
            issues->setRows({});
            invalidate();
        });
        for (auto *w : {reporter, reason})
            QObject::connect(w, &QLineEdit::textChanged, &dialog, [this] { invalidate(); });
        QObject::connect(analyze, &QPushButton::clicked, &dialog, [this] { preview(); });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] { commit(); });
        listener = doc.subscribe([this](Document::Event) {
            if (committing)
                return;
            if (!current()) {
                if (control)
                    control->cancel();
                prepared.reset();
                message->setText("문서/저장 상태가 변경되었습니다. 창을 다시 열어 분석하세요.");
                controls();
            }
        });
        controls();
        dialog.resize(1100, 760);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return doc.project() && !doc.readOnly() && activeXerFinancials(*source) && doc.sessionGeneration() == session &&
               doc.project()->documentId == source->documentId && doc.project()->revision == source->revision;
    }
    void controls() {
        const bool edit = current() && !busy;
        tabs->setEnabled(edit);
        reporter->setEnabled(edit);
        reason->setEnabled(edit);
        analyze->setEnabled(edit);
        reset->setEnabled(edit);
        submit->setEnabled(edit && prepared && prepared->edit && prepared->edit->state() == EditState::Ready);
        cancel->setEnabled(busy);
    }
    void invalidate() {
        prepared.reset();
        submit->setEnabled(false);
        message->setText("입력이 바뀌었습니다. 다시 분석한 뒤 제출하세요.");
    }
    void preview() {
        try {
            if (!current() || busy)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            XerFinancialMergeSubmission s{
                {{choices.begin(), choices.end()}, totals->overrides()},
                reporter->text().trimmed().toStdString(),
                reason->text().trimmed().toStdString(),
                QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString()};
            EditCommand command = relink ? EditCommand(XerRelinkEdit{{*relink, s}, xerFinancialProjectionHash})
                                         : EditCommand(XerFinancialMergeEdit{s, xerFinancialProjectionHash});
            auto request = doc.makeEditRequest(std::move(command));
            prepared.reset();
            control = std::make_shared<CalculationControl>();
            busy = true;
            controls();
            message->setText("원본과 현재 원장의 날짜·누계·소유를 대조하고 있습니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<MergePreview>>(&dialog);
            QObject::connect(
                watcher, &QFutureWatcher<std::shared_ptr<MergePreview>>::finished, &dialog, [this, watcher] {
                    auto p = watcher->result();
                    watcher->deleteLater();
                    busy = false;
                    if (!current() || p->cancelled)
                        message->setText("분석이 취소되었거나 문서가 변경되어 반영하지 않았습니다.");
                    else if (!p->error.isEmpty())
                        message->setText(p->error);
                    else {
                        issues->setRows(p->result.issues);
                        if (source->statusAt)
                            balance->set(*source, p->result.financials);
                        std::map<std::string, const PeriodActualEntry *> entries;
                        std::map<std::string, QString> names;
                        for (const auto &e : source->periodActuals)
                            entries[e.id] = &e;
                        for (const auto &t : source->tasks)
                            names[t.id] = q(t.name);
                        for (const auto &r : source->resources)
                            names[r.id] = q(r.name);
                        details->setRowCount(int(p->result.entryIds.size()));
                        int row = 0;
                        for (const auto &id : p->result.entryIds) {
                            const auto &e = *entries.at(id);
                            const QStringList values{
                                names[e.taskId] + " / " +
                                    (e.resourceId.empty() ? QString("작업 직접비") : names[e.resourceId]),
                                q(e.date), e.workUnitMinutes ? QString::number(*e.workUnitMinutes) : "미보고",
                                moneyui::format(e.actualMinor, source->currency, "미보고")};
                            for (int c = 0; c < 4; ++c)
                                details->setItem(row, c, new QTableWidgetItem(values[c]));
                            ++row;
                        }
                        if (p->edit && p->edit->state() == EditState::Ready) {
                            prepared = p;
                            message->setText(
                                "병합 검증 완료 · 날짜 원장 " + QString::number(row) +
                                "행 연결 · 현재 원장·누계 AC 유지. 대사와 기간 교체 선택을 확인한 뒤 제출하세요.");
                            tabs->setCurrentIndex(2);
                        } else if (p->edit && p->edit->failure())
                            message->setText(q(diagnosticMessage(*p->edit->failure())));
                        else {
                            message->setText("미해결 충돌이 있습니다. 기간 교체 선택·누계 구분 또는 현재 문서의 "
                                             "연결/AC를 정정한 뒤 다시 분석하세요.");
                            tabs->setCurrentIndex(0);
                        }
                    }
                    controls();
                });
            watcher->setFuture(
                QtConcurrent::run([request = std::move(request), s = std::move(s), p = source, c = control]() mutable {
                    auto out = std::make_shared<MergePreview>();
                    try {
                        EvaluationScope scope(c);
                        out->result = mergeXerFinancials(*p, s.options);
                        if (out->result.ready())
                            out->edit = Document::prepareEdit(std::move(request), c);
                    } catch (const CalculationCancelled &) {
                        out->cancelled = true;
                    } catch (const std::exception &e) {
                        out->error = q(diagnosticMessage(diagnosticFromException(e)));
                    }
                    return out;
                }));
        } catch (const std::exception &e) {
            showEditorError(*message, e);
            controls();
        }
    }
    void commit() {
        if (!current() || !prepared || !prepared->edit || busy)
            return;
        committing = true;
        const auto state = doc.applyPreparedEdit(*prepared->edit);
        committing = false;
        prepared.reset();
        if (state == EditState::Applied)
            dialog.accept();
        else {
            message->setText("취소 또는 문서 변경으로 병합을 제출하지 않았습니다.");
            controls();
        }
    }
};
XerFinancialMergeDialog::XerFinancialMergeDialog(Document &d, QWidget *parent, std::optional<XerRelinkAudit> relink)
    : QDialog(parent) {
    setObjectName("xerFinancialMergeDialog");
    setWindowTitle("XER 원본·현재 원장 자동 병합");
    impl_ = std::make_unique<Impl>(*this, d, std::move(relink));
}
XerFinancialMergeDialog::~XerFinancialMergeDialog() = default;
void XerFinancialMergeDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace jobs
