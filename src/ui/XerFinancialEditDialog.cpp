#include "ui/XerFinancialEditDialog.h"
#include "domain/XerFinancialAllocation.h"
#include "persistence/XerFinancialJson.h"
#include "formatting/Money.h"
#include "formatting/DiagnosticText.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QDate>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrent>
#include <algorithm>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string str(const QString &s) {
    return s.toStdString();
}
using Metric = std::optional<std::int64_t> XerFinancialPeriod::*;
const Metric metrics[] = {&XerFinancialPeriod::work,          &XerFinancialPeriod::actualMinor,
                          &XerFinancialPeriod::laborWork,     &XerFinancialPeriod::equipmentWork,
                          &XerFinancialPeriod::laborMinor,    &XerFinancialPeriod::equipmentMinor,
                          &XerFinancialPeriod::materialMinor, &XerFinancialPeriod::expenseMinor,
                          &XerFinancialPeriod::bcwp,          &XerFinancialPeriod::bcws};
QString units(std::optional<std::int64_t> n) {
    return n ? QString::number(*n) + "u" : "";
}
class DraftModel final : public QAbstractTableModel {
    XerFinancialSnapshot &s_;
    int kind_;
    std::function<void()> changed_;
    std::function<void(const std::exception &)> error_;
    std::map<std::string, QString> tasks_, resources_;
    QString task(const std::string &id) const {
        const auto it = tasks_.find(id);
        return it == tasks_.end() ? q(id) : it->second;
    }
    QString resource(const std::string &id) const {
        const auto it = resources_.find(id);
        return it == resources_.end() ? QString("작업 요약/직접비") : it->second;
    }

public:
    DraftModel(XerFinancialSnapshot &s, int kind, QObject *parent, std::function<void()> changed,
               std::function<void(const std::exception &)> error)
        : QAbstractTableModel(parent), s_(s), kind_(kind), changed_(std::move(changed)), error_(std::move(error)) {
        for (const auto &t : s.tasks)
            tasks_[t.id] = q(t.name);
        for (const auto &r : s.resources)
            resources_[r.id] = q(r.name);
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0
                           : int(kind_ == 0   ? s_.tasks.size()
                                 : kind_ == 1 ? s_.assignments.size()
                                              : s_.periods.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : kind_ == 0 ? 2 : kind_ == 1 ? 6 : 14;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (o == Qt::Vertical)
            return c + 1;
        const QStringList a{"작업", "누계 AC"},
            b{"작업", "자원", "정규 공수 h/u", "초과 공수 h/u", "정규 원가", "초과 원가"},
            d{"작업",        "자원/구분",   "시작 YYYY-MM-DD HH:mm", "종료 YYYY-MM-DD HH:mm",
              "공수 h/u",    "자원 원가",   "요약 인력 공수 h/u",    "요약 장비 공수 h/u",
              "요약 인건비", "요약 장비비", "요약 자재비",           "경비/직접비",
              "외부 EV",     "외부 PV"};
        return (kind_ == 0 ? a : kind_ == 1 ? b : d).value(c);
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        auto f = QAbstractTableModel::flags(i);
        if (i.column() >= (kind_ == 0 ? 1 : 2)) {
            if (kind_ == 2) {
                const auto &p = s_.periods[std::size_t(i.row())];
                if ((p.resourceId.empty() && (i.column() == 4 || i.column() == 5)) ||
                    (!p.resourceId.empty() && i.column() >= 6))
                    return f;
            }
            f |= Qt::ItemIsEditable;
        }
        return f;
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
            return {};
        const auto row = std::size_t(i.row());
        const auto c = i.column();
        auto money = [&](auto n) { return moneyui::format(n, s_.currency, ""); };
        if (kind_ == 0)
            return c == 0 ? q(s_.tasks[row].name) : money(s_.tasks[row].actualMinor);
        if (kind_ == 1) {
            const auto &a = s_.assignments[row];
            const QStringList v{task(a.taskId),        resource(a.resourceId), units(a.regularWork),
                                units(a.overtimeWork), money(a.regularMinor),  money(a.overtimeMinor)};
            return v.value(c);
        }
        const auto &p = s_.periods[row];
        if (c == 0)
            return task(p.taskId);
        if (c == 1)
            return resource(p.resourceId);
        if (c == 2)
            return q(p.start);
        if (c == 3)
            return q(p.finish);
        const auto n = p.*metrics[c - 4];
        return (c == 4 || c == 6 || c == 7) ? units(n) : money(n);
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (role != Qt::EditRole || !(flags(i) & Qt::ItemIsEditable))
            return false;
        try {
            const auto text = v.toString().trimmed();
            const auto row = std::size_t(i.row());
            const auto c = i.column();
            const auto work = [&]() -> std::optional<std::int64_t> {
                return text.isEmpty() ? std::nullopt : std::optional{parseActualHours(str(text))};
            };
            if (kind_ == 0)
                s_.tasks[row].actualMinor = moneyui::parse(text, s_.currency);
            else if (kind_ == 1) {
                auto &a = s_.assignments[row];
                if (c == 2)
                    a.regularWork = work();
                if (c == 3)
                    a.overtimeWork = work();
                if (c == 4)
                    a.regularMinor = moneyui::parse(text, s_.currency);
                if (c == 5)
                    a.overtimeMinor = moneyui::parse(text, s_.currency);
            } else {
                auto &p = s_.periods[row];
                if (c == 2)
                    p.start = str(text);
                else if (c == 3)
                    p.finish = str(text);
                else
                    p.*metrics[c - 4] = (c == 4 || c == 6 || c == 7) ? work() : moneyui::parse(text, s_.currency);
            }
            emit dataChanged(i, i);
            changed_();
            return true;
        } catch (const std::exception &e) {
            error_(e);
            return false;
        }
    }
    void append(XerFinancialPeriod p) {
        const auto i = int(s_.periods.size());
        beginInsertRows({}, i, i);
        s_.periods.push_back(std::move(p));
        endInsertRows();
        changed_();
    }
};
struct Preview {
    std::shared_ptr<Document::PreparedEdit> edit;
    std::map<PeriodActualKey, PeriodActualEntry> rows;
};
} // namespace
struct XerFinancialEditDialog::Impl {
    XerFinancialEditDialog &dialog;
    Document &doc;
    Project source;
    XerFinancialSnapshot draft;
    std::vector<XerFinancialAllocation> allocations;
    std::uint64_t session = 0, listener = 0;
    bool busy = false, committing = false, loading = false, sourceCurrent = false;
    int selected = -1;
    std::vector<QWidget *> extraInputs;
    QTabWidget *tabs;
    QTableView *tasks, *assignments, *periods;
    DraftModel *periodModel;
    QPlainTextEdit *days;
    QLineEdit *start, *finish, *reporter, *reason;
    QLabel *message, *note;
    QPushButton *preview = nullptr, *submit = nullptr, *cancel = nullptr;
    QTableWidget *result;
    std::shared_ptr<Preview> prepared;
    std::shared_ptr<CalculationControl> control;
    Impl(XerFinancialEditDialog &d, Document &document) : dialog(d), doc(document) {
        if (doc.project())
            source = *doc.project();
        if (const auto *s = activeXerFinancials(source))
            draft = *s;
        allocations = source.xerFinancialRevisions.empty() ? initialXerFinancialAllocations(draft)
                                                           : source.xerFinancialRevisions.back().allocations;
        session = doc.sessionGeneration();
        sourceCurrent = activeXerFinancials(source) && xerFinancialsCurrent(source);
        auto *body = new QVBoxLayout(&dialog);
        note =
            new QLabel("원본은 보존하며 집계 정정·배분을 새 이력으로 제출합니다. 누계 AC에는 날짜 배분액이 포함됩니다. "
                       "외부 EV/PV는 내부 EVM에 합산하지 않습니다.\n공수는 h 또는 정확한 정수 u(6000u=1h), 금액은 표시 "
                       "통화 기준입니다. 빈칸은 미보고, 0은 명시 0입니다.\n날짜 배분은 선택 기간 전체를 지정한 "
                       "비중으로 나눕니다. 휴일도 명시한 날짜에 포함되며 제출 후에는 새 정정으로 변경합니다.",
                       &dialog);
        note->setText("상태일 " + q(draft.statusAt) + " · 통화 " + q(draft.currency) + "\n" + note->text());
        note->setTextFormat(Qt::PlainText);
        note->setWordWrap(true);
        body->addWidget(note);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("xerEditTabs");
        body->addWidget(tabs, 1);
        auto changed = [this] { invalidate(); };
        auto error = [this](const std::exception &e) { showEditorError(*message, e); };
        const QStringList titles{"작업 누계 AC", "배정 정규·초과근무 누계", "재무기간 집계", "날짜별 배분 미리보기"};
        QTableView **views[] = {&tasks, &assignments, &periods};
        for (int k = 0; k < 3; ++k) {
            auto *v = new QTableView(tabs);
            *views[k] = v;
            v->setObjectName(QString("xerEditTable%1").arg(k));
            auto *m = new DraftModel(draft, k, v, changed, error);
            v->setModel(m);
            if (k == 2)
                periodModel = m;
            v->setSelectionBehavior(QAbstractItemView::SelectRows);
            v->setSelectionMode(QAbstractItemView::SingleSelection);
            v->horizontalHeader()->setDefaultSectionSize(145);
            tabs->addTab(v, titles[k]);
        }
        result = new QTableWidget(0, 4, tabs);
        result->setObjectName("xerEditPreviewRows");
        result->setHorizontalHeaderLabels({"작업 / 자원", "실제 날짜", "공수 u", "원가"});
        result->setEditTriggers(QAbstractItemView::NoEditTriggers);
        result->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tabs->addTab(result, titles[3]);
        auto *dates = new QHBoxLayout;
        start = new QLineEdit(q(draft.statusAt.substr(0, 10)), &dialog);
        finish = new QLineEdit(start->text(), &dialog);
        start->setObjectName("xerNewPeriodStart");
        finish->setObjectName("xerNewPeriodFinish");
        dates->addWidget(new QLabel("새 기간 시작/종료 날짜", &dialog));
        dates->addWidget(start);
        dates->addWidget(finish);
        auto *add = new QPushButton("배정 잔량 기간 추가", &dialog);
        add->setObjectName("xerAddResidualPeriod");
        dates->addWidget(add);
        auto *direct = new QPushButton("작업 직접비 잔량 기간 추가", &dialog);
        direct->setObjectName("xerAddDirectPeriod");
        dates->addWidget(direct);
        body->addLayout(dates);
        auto *allocation = new QHBoxLayout;
        days = new QPlainTextEdit(&dialog);
        days->setObjectName("xerAllocationDays");
        days->setPlaceholderText("재무기간 행 선택 후 날짜,비중 입력 · 한 줄에 "
                                 "하나\n2026-10-19,1\n2026-10-20,2\n빈칸으로 적용하면 날짜 배분 철회");
        days->setMaximumHeight(100);
        allocation->addWidget(days, 1);
        auto *buttons = new QVBoxLayout;
        auto *equal = new QPushButton("선택 기간의 모든 날짜에 균등 배분", &dialog);
        equal->setObjectName("xerEqualDays");
        auto *set = new QPushButton("선택 기간의 날짜·비중 적용", &dialog);
        set->setObjectName("xerSetDays");
        buttons->addWidget(equal);
        buttons->addWidget(set);
        allocation->addLayout(buttons);
        body->addLayout(allocation);
        auto *form = new QFormLayout;
        reporter = new QLineEdit(&dialog);
        reporter->setObjectName("xerEditReporter");
        reason = new QLineEdit(&dialog);
        reason->setObjectName("xerEditReason");
        form->addRow("작성자", reporter);
        form->addRow("정정 / 배분 사유", reason);
        body->addLayout(form);
        message = new QLabel(&dialog);
        message->setObjectName("xerEditMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        body->addWidget(message);
        auto *actions = new QHBoxLayout;
        preview = new QPushButton("검증·배분 미리보기", &dialog);
        preview->setObjectName("xerEditPreview");
        submit = new QPushButton("미리본 집계·배분 제출", &dialog);
        submit->setObjectName("xerEditSubmit");
        cancel = new QPushButton("준비 취소", &dialog);
        cancel->setObjectName("xerEditCancel");
        auto *close = new QPushButton("닫기", &dialog);
        actions->addWidget(preview);
        actions->addWidget(submit);
        actions->addWidget(cancel);
        actions->addStretch();
        actions->addWidget(close);
        body->addLayout(actions);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(days, &QPlainTextEdit::textChanged, &dialog, [this] {
            if (!loading)
                invalidate();
        });
        for (auto *w : {reporter, reason})
            QObject::connect(w, &QLineEdit::textChanged, &dialog, [this] { invalidate(); });
        QObject::connect(periods->selectionModel(), &QItemSelectionModel::currentRowChanged, &dialog,
                         [this](const QModelIndex &i, const QModelIndex &old) {
                             try {
                                 saveDays();
                                 loadDays(i.row());
                             } catch (const std::exception &e) {
                                 showEditorError(*message, e);
                                 QSignalBlocker block(periods->selectionModel());
                                 periods->setCurrentIndex(old);
                             }
                         });
        extraInputs = {start, finish, add, direct, equal, set};
        QObject::connect(set, &QPushButton::clicked, &dialog, [this] {
            try {
                saveDays();
                message->setText("날짜·비중을 초안에 적용했습니다. 미리보기를 확인하세요.");
                invalidate();
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        QObject::connect(equal, &QPushButton::clicked, &dialog, [this] {
            try {
                if (selected < 0)
                    throw std::runtime_error("재무기간 행을 먼저 선택하세요.");
                const auto &p = draft.periods.at(std::size_t(selected));
                auto a = QDate::fromString(q(p.start.substr(0, 10)), Qt::ISODate),
                     b = QDate::fromString(q(p.finish.substr(0, 10)), Qt::ISODate);
                if (!a.isValid() || !b.isValid() || a > b || a.daysTo(b) >= 366)
                    throw std::runtime_error("균등 배분은 유효한 366일 이하 기간을 선택하세요.");
                QStringList text;
                for (; a <= b; a = a.addDays(1))
                    text << a.toString(Qt::ISODate) + ",1";
                days->setPlainText(text.join('\n'));
                saveDays();
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        QObject::connect(add, &QPushButton::clicked, &dialog, [this] {
            try {
                const auto row = assignments->currentIndex().row();
                if (row < 0)
                    throw std::runtime_error("배정 누계 탭에서 배정을 선택하세요.");
                const auto &a = draft.assignments.at(std::size_t(row));
                XerFinancialPeriod p;
                p.taskId = a.taskId;
                p.resourceId = a.resourceId;
                p.start = str(start->text().trimmed()) + " 00:00";
                p.finish = str(finish->text().trimmed()) + " 23:59";
                if (a.regularWork || a.overtimeWork)
                    p.work = a.regularWork.value_or(0) + a.overtimeWork.value_or(0);
                if (a.regularMinor || a.overtimeMinor)
                    p.actualMinor = a.regularMinor.value_or(0) + a.overtimeMinor.value_or(0);
                for (const auto &e : draft.periods)
                    if (e.taskId == p.taskId && e.resourceId == p.resourceId) {
                        if (p.work)
                            *p.work -= e.work.value_or(0);
                        if (p.actualMinor)
                            *p.actualMinor -= e.actualMinor.value_or(0);
                    }
                if ((p.work && *p.work < 0) || (p.actualMinor && *p.actualMinor < 0) || (!p.work && !p.actualMinor))
                    throw std::runtime_error("먼저 누계와 기간 합계를 확인하세요.");
                auto proposal = draft;
                proposal.periods.push_back(p);
                validateXerFinancials(proposal);
                saveDays();
                periodModel->append(std::move(p));
                tabs->setCurrentIndex(2);
                periods->setCurrentIndex(periodModel->index(periodModel->rowCount() - 1, 0));
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        QObject::connect(direct, &QPushButton::clicked, &dialog, [this] {
            try {
                const auto row = tasks->currentIndex().row();
                if (row < 0)
                    throw std::runtime_error("작업 누계 탭에서 작업을 선택하세요.");
                const auto &t = draft.tasks.at(std::size_t(row));
                if (!t.actualMinor)
                    throw std::runtime_error("먼저 작업 누계 AC를 입력하세요.");
                XerFinancialPeriod p;
                p.taskId = t.id;
                p.start = str(start->text().trimmed()) + " 00:00";
                p.finish = str(finish->text().trimmed()) + " 23:59";
                p.expenseMinor = t.actualMinor;
                for (const auto &a : draft.assignments)
                    if (a.taskId == t.id)
                        *p.expenseMinor -= a.regularMinor.value_or(0) + a.overtimeMinor.value_or(0);
                for (const auto &e : draft.periods)
                    if (e.taskId == t.id && e.resourceId.empty())
                        *p.expenseMinor -= e.expenseMinor.value_or(0);
                auto proposal = draft;
                proposal.periods.push_back(p);
                validateXerFinancials(proposal);
                saveDays();
                periodModel->append(std::move(p));
                tabs->setCurrentIndex(2);
                periods->setCurrentIndex(periodModel->index(periodModel->rowCount() - 1, 0));
            } catch (const std::exception &e) {
                showEditorError(*message, e);
            }
        });
        QObject::connect(preview, &QPushButton::clicked, &dialog, [this] { prepare(); });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] { commit(); });
        listener = doc.subscribe([this](Document::Event) {
            if (committing)
                return;
            if (!current()) {
                if (control)
                    control->cancel();
                prepared.reset();
                message->setText("문서/저장 상태가 바뀌었습니다. 창을 다시 열어 최신 입력을 확인하세요.");
                controls();
            }
        });
        if (!current())
            message->setText("현재 문서와 집계가 다릅니다. 파일 → XER 원본·현재 원장 병합에서 대조한 뒤 편집하세요.");
        controls();
        dialog.resize(1100, 820);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return doc.project() && !doc.readOnly() && doc.sessionGeneration() == session &&
               doc.project()->documentId == source.documentId && doc.project()->revision == source.revision &&
               sourceCurrent;
    }
    void controls() {
        const bool editable = current() && !busy;
        tabs->setEnabled(editable);
        days->setEnabled(editable);
        reporter->setEnabled(editable);
        reason->setEnabled(editable);
        for (auto *w : extraInputs)
            w->setEnabled(editable);
        preview->setEnabled(editable);
        submit->setEnabled(editable && bool(prepared));
        cancel->setEnabled(busy);
    }
    void invalidate() {
        if (prepared) {
            result->setRowCount(0);
        }
        message->setText("초안이 변경되었습니다. 미리보기를 검증한 뒤 제출하세요.");
        prepared.reset();
        if (submit)
            submit->setEnabled(false);
    }
    void loadDays(int row) {
        selected = row;
        loading = true;
        QStringList text;
        for (const auto &a : allocations)
            if (int(a.periodIndex) == row)
                for (const auto &d : a.days)
                    text << q(d.date) + "," + QString::number(d.weight);
        days->setPlainText(text.join('\n'));
        loading = false;
    }
    void saveDays() {
        if (selected < 0)
            return;
        XerFinancialAllocation a;
        a.periodIndex = std::size_t(selected);
        for (const auto &line : days->toPlainText().split('\n')) {
            if (line.trimmed().isEmpty())
                continue;
            const auto fields = line.trimmed().split(',');
            bool ok = false;
            const auto w = fields.value(1).toLongLong(&ok);
            if (fields.size() != 2 || !ok || w < 1 || w > 1000000 || a.days.size() >= 1000)
                throw std::runtime_error("날짜,양의 정수 비중(1~1000000)을 한 줄에 하나씩 입력하세요.");
            a.days.push_back({str(fields[0].trimmed()), w});
        }
        std::erase_if(allocations, [&](const auto &old) { return old.periodIndex == a.periodIndex; });
        if (!a.days.empty())
            allocations.push_back(std::move(a));
    }
    void prepare() {
        try {
            if (!current() || busy)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            saveDays();
            prepared.reset();
            XerFinancialSubmission s{draft, allocations, str(reporter->text().trimmed()), str(reason->text().trimmed()),
                                     str(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"))};
            auto request = doc.makeEditRequest(XerFinancialEdit{s, xerFinancialProjectionHash});
            control = std::make_shared<CalculationControl>();
            busy = true;
            controls();
            message->setText("집계 대사와 날짜 배분을 검증하고 있습니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog, [this, watcher] {
                auto p = watcher->result();
                watcher->deleteLater();
                busy = false;
                if (p->edit->state() == EditState::Ready && current()) {
                    prepared = p;
                    result->setRowCount(int(p->rows.size()));
                    std::map<std::string, QString> taskNames, resourceNames;
                    for (const auto &t : draft.tasks)
                        taskNames[t.id] = q(t.name);
                    for (const auto &r : draft.resources)
                        resourceNames[r.id] = q(r.name);
                    int row = 0;
                    for (const auto &[key, e] : p->rows) {
                        (void)key;
                        const QStringList values{
                            taskNames[e.taskId] + " / " +
                                (e.resourceId.empty() ? QString("작업 직접비") : resourceNames[e.resourceId]),
                            q(e.date), e.workUnitMinutes ? QString::number(*e.workUnitMinutes) : "미보고",
                            moneyui::format(e.actualMinor, draft.currency, "미보고")};
                        for (int c = 0; c < 4; ++c)
                            result->setItem(row, c, new QTableWidgetItem(values[c]));
                        ++row;
                    }
                    QStringList totals;
                    for (std::size_t i = 0; i < std::min<std::size_t>(3, draft.tasks.size()); ++i) {
                        const auto &t = draft.tasks[i];
                        totals << q(t.name).left(60) + "=" + moneyui::format(t.actualMinor, draft.currency);
                    }
                    if (draft.tasks.size() > 3)
                        totals << "전체 작업 누계는 작업 누계 AC 탭에서 확인";
                    message->setText("검증 완료 · " + QString::number(row) +
                                     "개 날짜 행 · 누계 AC(배분액 포함): " + totals.join(" / ") +
                                     "\n기존 날짜는 정정하며 철회된 날짜는 미보고 기록으로 남깁니다.");
                    tabs->setCurrentIndex(3);
                } else {
                    prepared.reset();
                    message->setText(p->edit->failure() ? q(diagnosticMessage(*p->edit->failure()))
                                                        : "취소되었거나 문서가 변경되어 제출하지 않았습니다.");
                }
                controls();
            });
            watcher->setFuture(
                QtConcurrent::run([request = std::move(request), s = std::move(s), c = control]() mutable {
                    auto out = std::make_shared<Preview>();
                    out->edit = Document::prepareEdit(std::move(request), c);
                    if (out->edit->state() == EditState::Ready)
                        out->rows = allocateXerFinancials(s.financials, s.allocations);
                    return out;
                }));
        } catch (const std::exception &e) {
            showEditorError(*message, e);
            controls();
        }
    }
    void commit() {
        if (!prepared || !current() || busy)
            return;
        committing = true;
        const auto state = doc.applyPreparedEdit(*prepared->edit);
        committing = false;
        prepared.reset();
        if (state == EditState::Applied) {
            message->setText("집계·날짜 배분을 제출했습니다. 원본과 이전 보고는 보존됩니다.");
            dialog.accept();
        } else {
            message->setText("문서 변경 또는 취소로 제출하지 않았습니다. 다시 열어 주세요.");
            controls();
        }
    }
};
XerFinancialEditDialog::XerFinancialEditDialog(Document &d, QWidget *parent) : QDialog(parent) {
    setObjectName("xerFinancialEditDialog");
    setWindowTitle("XER 집계 편집·날짜별 배분");
    impl_ = std::make_unique<Impl>(*this, d);
}
XerFinancialEditDialog::~XerFinancialEditDialog() = default;
void XerFinancialEditDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace ppm
