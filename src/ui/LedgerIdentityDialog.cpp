#include "ui/LedgerIdentityDialog.h"
#include "ui/EditorUi.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectJson.h"
#include "formatting/DiagnosticText.h"
#include "ui/MoneyFields.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <map>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
using Counts = std::map<std::pair<std::string, std::string>, std::size_t>;
Counts counts(const Project &p) {
    Counts n;
    for (const auto &v : p.progress)
        ++n[{"task", v.taskId}];
    for (const auto &v : p.costEntries)
        ++n[{"task", v.taskId}];
    for (const auto &v : p.stepReports)
        ++n[{"task", v.taskId}];
    for (const auto &v : p.periodActuals) {
        ++n[{"task", v.taskId}];
        if (!v.resourceId.empty())
            ++n[{"resource", v.resourceId}];
    }
    return n;
}
struct Owner {
    std::string kind, id, name, type;
    std::size_t count = 0;
    QString mode, newId;
};
class Owners final : public QAbstractTableModel {
    std::vector<Owner> rows_;
    std::map<std::pair<std::string, std::string>, QString> names_;

public:
    std::function<void()> changed;
    Owners(const Project &p, QObject *parent) : QAbstractTableModel(parent) {
        const auto n = counts(p);
        for (const auto &t : p.tasks) {
            names_[{"task", t.id}] = q(t.name);
            if (n.contains({"task", t.id})) {
                Owner r{"task", t.id, t.name, "task", 0, {}, {}};
                r.count = n.at({"task", t.id});
                rows_.push_back(std::move(r));
            }
        }
        for (const auto &v : p.resources) {
            names_[{"resource", v.id}] = q(v.name);
            if (n.contains({"resource", v.id})) {
                Owner r{"resource", v.id, v.name, v.type, 0, {}, {}};
                r.count = n.at({"resource", v.id});
                rows_.push_back(std::move(r));
            }
        }
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 6;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"종류", "원래 이름", "원래 ID", "관측 기록 수", "이관 대상",
                                          "대상 ID / 새 ID 입력"}
                                  .at(c))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid())
            return {};
        const auto &r = rows_.at(i.row());
        if (role == Qt::UserRole)
            return q(r.type);
        if (role == Qt::EditRole)
            return i.column() == 4 ? r.mode : r.newId;
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
            return {};
        QString target = r.mode == "fresh"          ? "새 ID 부여"
                         : r.mode.startsWith("id:") ? names_.at({r.kind, r.mode.mid(3).toStdString()})
                                                    : "이관하지 않음";
        return QStringList{r.kind == "task"    ? "작업"
                           : r.type == "labor" ? "인력"
                                               : "장비",
                           q(r.name),
                           q(r.id),
                           QString::number(r.count),
                           target,
                           r.mode == "fresh"          ? r.newId
                           : r.mode.startsWith("id:") ? r.mode.mid(3)
                                                      : QString{}}
            .at(i.column());
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        return QAbstractTableModel::flags(i) |
               ((i.column() == 4 || (i.column() == 5 && rows_.at(i.row()).mode == "fresh")) ? Qt::ItemIsEditable
                                                                                            : Qt::NoItemFlags);
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (!i.isValid() || role != Qt::EditRole)
            return false;
        auto &r = rows_.at(i.row());
        if (i.column() == 4) {
            const auto value = v.toString();
            if (!value.isEmpty() && value != "fresh" &&
                (!value.startsWith("id:") || !names_.contains({r.kind, value.mid(3).toStdString()})))
                return false;
            r.mode = value;
        } else if (i.column() == 5 && r.mode == "fresh")
            r.newId = v.toString().trimmed();
        else
            return false;
        emit dataChanged(index(i.row(), 4), index(i.row(), 5));
        if (changed)
            changed();
        return true;
    }
    std::vector<LedgerIdLink> links() const {
        std::vector<LedgerIdLink> out;
        for (const auto &r : rows_)
            if (!r.mode.isEmpty())
                out.push_back({r.kind, r.id, r.mode == "fresh" ? r.newId.toStdString() : r.mode.mid(3).toStdString()});
        return out;
    }
};
class Delegate final : public QStyledItemDelegate {
    QStandardItemModel tasks_, labor_, equipment_;

public:
    Delegate(const Project &p, QObject *parent) : QStyledItemDelegate(parent) {
        const auto n = counts(p);
        auto add = [](QStandardItemModel &m, const QString &name, const QString &id) {
            auto *v = new QStandardItem(name);
            v->setData(id, Qt::UserRole);
            m.appendRow(v);
        };
        for (auto *m : {&tasks_, &labor_, &equipment_}) {
            add(*m, "이관하지 않음", {});
            add(*m, "새 ID 부여", "fresh");
        }
        for (const auto &t : p.tasks)
            if (!n.contains({"task", t.id}))
                add(tasks_, q(t.name) + " · " + q(t.id), "id:" + q(t.id));
        for (const auto &r : p.resources)
            if (!n.contains({"resource", r.id}))
                add(r.type == "labor" ? labor_ : equipment_, q(r.name) + " · " + q(r.id), "id:" + q(r.id));
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &i) const override {
        if (i.column() == 5) {
            auto *e = new QLineEdit(parent);
            e->setMaxLength(1024);
            return e;
        }
        auto *c = new QComboBox(parent);
        const auto type = i.data(Qt::UserRole).toString();
        c->setModel(const_cast<QStandardItemModel *>(type == "task"    ? &tasks_
                                                     : type == "labor" ? &labor_
                                                                       : &equipment_));
        return c;
    }
    void setEditorData(QWidget *w, const QModelIndex &i) const override {
        if (auto *e = qobject_cast<QLineEdit *>(w))
            e->setText(i.data(Qt::EditRole).toString());
        else {
            auto *c = qobject_cast<QComboBox *>(w);
            c->setCurrentIndex(c->findData(i.data(Qt::EditRole)));
        }
    }
    void setModelData(QWidget *w, QAbstractItemModel *m, const QModelIndex &i) const override {
        m->setData(i,
                   qobject_cast<QLineEdit *>(w) ? QVariant(qobject_cast<QLineEdit *>(w)->text())
                                                : qobject_cast<QComboBox *>(w)->currentData(),
                   Qt::EditRole);
    }
};
class Facts final : public QAbstractTableModel {
    std::shared_ptr<const Project> before_, after_;

public:
    Facts(std::shared_ptr<const Project> a, std::shared_ptr<const Project> b, QObject *p)
        : QAbstractTableModel(p), before_(std::move(a)), after_(std::move(b)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0
                           : int(before_->progress.size() + before_->costEntries.size() +
                                 before_->periodActuals.size() + before_->stepReports.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 16;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"원장", "기록 ID", "이전 작업 ID", "이관 작업 ID", "이전 자원 ID",
                                          "이관 자원 ID", "상태일", "관측일", "기록 UTC", "작성자", "사유",
                                          "기록 revision", "이전 보고 ID", "원가 보고 ID", "기록 수치", "출처 문서"}
                                  .at(c))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        std::size_t row = i.row();
        QStringList v;
        auto common = [&](const auto &a, const auto &b, const QString &kind, const QString &value,
                          const QString &previous) {
            v = {kind,
                 q(a.id),
                 q(a.taskId),
                 q(b.taskId),
                 {},
                 {},
                 q(a.statusAt),
                 {},
                 q(a.recordedAt),
                 q(a.reporter),
                 q(a.reason),
                 QString::number(a.recordedRevision),
                 previous,
                 {},
                 value,
                 q(a.sourceDocumentId)};
        };
        if (row < before_->progress.size()) {
            const auto &a = before_->progress[row], &b = after_->progress[row];
            common(a, b, "진행",
                   q(a.actualStart.value_or("미시작")) + " ~ " + q(a.actualFinish.value_or("미완료")) + " · 실제 " +
                       QString::number(a.actualDurationMinutes) + "분 / 잔여 " + QString::number(a.remainingMinutes) +
                       "분",
                   q(a.previousEntryId));
        } else if ((row -= before_->progress.size()) < before_->costEntries.size()) {
            const auto &a = before_->costEntries[row], &b = after_->costEntries[row];
            common(a, b, "누계 원가", moneyui::format(a.actualMinor, a.currency, "미정"), q(a.previousEntryId));
        } else if ((row -= before_->costEntries.size()) < before_->periodActuals.size()) {
            const auto &a = before_->periodActuals[row], &b = after_->periodActuals[row];
            common(a, b, "날짜별 원장",
                   moneyui::format(a.actualMinor, a.currency, "미정") + " · " +
                       (a.workUnitMinutes ? QString::number(*a.workUnitMinutes) + " (1/100분)" : "공수 미정"),
                   q(a.previousEntryId));
            v[4] = q(a.resourceId);
            v[5] = q(b.resourceId);
            v[7] = q(a.date);
            v[13] = q(a.costEntryId);
        } else {
            row -= before_->periodActuals.size();
            const auto &a = before_->stepReports[row], &b = after_->stepReports[row];
            common(a, b, "Steps", QString::number(a.steps.size()) + "개 단계", {});
        }
        return v.at(i.column());
    }
};
struct Preview {
    std::shared_ptr<Document::PreparedEdit> edit;
    std::shared_ptr<const Project> before, after;
    QString error;
};
} // namespace
struct LedgerIdentityDialog::Impl {
    LedgerIdentityDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source, beforeView, afterView;
    std::uint64_t session, listener = 0;
    bool busy = false, committing = false;
    std::shared_ptr<CalculationControl> control;
    std::shared_ptr<Document::PreparedEdit> prepared;
    Owners *owners;
    QTableView *mapping, *facts;
    QTabWidget *tabs;
    QLineEdit *reporter, *reason;
    QCheckBox *confirmed;
    QLabel *message;
    QPushButton *analyze, *submit, *cancel, *inspect;
    Impl(LedgerIdentityDialog &d, Document &p)
        : dialog(d), doc(p), source(p.snapshot()), session(p.sessionGeneration()) {
        auto *layout = new QVBoxLayout(&dialog);
        auto *note =
            new QLabel("관측 기록의 작업·자원 ID와 현재 문서 안의 참조를 이관합니다. 기록 ID·정정 "
                       "연결·금액·공수·날짜·작성자는 유지합니다.\n기존 대상으로 합칠 때는 대상의 현재 작업/자원 정의를 "
                       "사용하고 원래 중복 정의를 제거합니다. 두 관측 이력이나 동결 계획의 두 객체는 합산하지 "
                       "않습니다. 원본과 전후 상태를 불변 이력에 보존하며 일반 실행 취소로 삭제하지 않습니다.",
                       &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *search = new QLineEdit(&dialog);
        search->setObjectName("ledgerIdSearch");
        search->setPlaceholderText("원래 이름·ID 검색");
        layout->addWidget(search);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("ledgerIdTabs");
        layout->addWidget(tabs, 1);
        mapping = new QTableView(tabs);
        mapping->setObjectName("ledgerIdLinks");
        owners = new Owners(source ? *source : Project{}, mapping);
        auto *proxy = new QSortFilterProxyModel(mapping);
        proxy->setSourceModel(owners);
        proxy->setFilterKeyColumn(-1);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        mapping->setModel(proxy);
        if (source) {
            auto *delegate = new Delegate(*source, mapping);
            mapping->setItemDelegateForColumn(4, delegate);
            mapping->setItemDelegateForColumn(5, delegate);
        }
        mapping->horizontalHeader()->setDefaultSectionSize(185);
        tabs->addTab(mapping, "이관할 원장 ID 연결");
        QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        facts = new QTableView(tabs);
        facts->setObjectName("ledgerIdFacts");
        facts->horizontalHeader()->setDefaultSectionSize(190);
        tabs->addTab(facts, "원장 기록·참조 전후");
        auto *history = new QTableWidget(source ? int(source->ledgerIdMigrations.size()) : 0, 5, tabs);
        history->setObjectName("ledgerIdHistory");
        history->setHorizontalHeaderLabels({"기록 UTC", "작성자", "사유", "이관 ID 수", "이력 ID"});
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->horizontalHeader()->setDefaultSectionSize(185);
        if (source)
            for (std::size_t i = 0; i < source->ledgerIdMigrations.size(); ++i) {
                const auto &e = source->ledgerIdMigrations[i];
                const QStringList v{q(e.recordedAt), q(e.reporter), q(e.reason), QString::number(e.links.size()),
                                    q(e.id)};
                for (int c = 0; c < 5; ++c)
                    history->setItem(int(i), c, new QTableWidgetItem(v[c]));
            }
        tabs->addTab(history, "이관 이력 · 두 번 눌러 조회");
        auto *form = new QFormLayout;
        reporter = new QLineEdit(&dialog);
        reporter->setObjectName("ledgerIdReporter");
        reporter->setMaxLength(1024);
        reason = new QLineEdit(&dialog);
        reason->setObjectName("ledgerIdReason");
        reason->setMaxLength(4096);
        form->addRow("작성자", reporter);
        form->addRow("같은 작업/자원으로 확인한 근거", reason);
        layout->addLayout(form);
        confirmed = new QCheckBox(
            "선택한 ID가 같은 작업·자원임을 확인했고 원장·계획·기준선의 참조 이관과 전후 결과를 검토했습니다.",
            &dialog);
        confirmed->setObjectName("ledgerIdConfirm");
        layout->addWidget(confirmed);
        message = new QLabel(&dialog);
        message->setObjectName("ledgerIdMessage");
        message->setWordWrap(true);
        message->setTextFormat(Qt::PlainText);
        layout->addWidget(message);
        auto *buttons = new QHBoxLayout;
        analyze = new QPushButton("이관 검증·미리보기", &dialog);
        analyze->setObjectName("ledgerIdAnalyze");
        submit = new QPushButton("검증한 ID 이관 제출", &dialog);
        submit->setObjectName("ledgerIdSubmit");
        cancel = new QPushButton("계산 취소", &dialog);
        cancel->setObjectName("ledgerIdCancel");
        inspect = new QPushButton("이관 전후 원본 조회…", &dialog);
        inspect->setObjectName("ledgerIdInspect");
        auto *close = new QPushButton("닫기", &dialog);
        for (auto *b : {analyze, submit, cancel, inspect})
            buttons->addWidget(b);
        buttons->addStretch();
        buttons->addWidget(close);
        layout->addLayout(buttons);
        auto invalidate = [this] {
            prepared.reset();
            confirmed->setChecked(false);
            controls();
        };
        owners->changed = invalidate;
        for (auto *e : {reporter, reason})
            QObject::connect(e, &QLineEdit::textChanged, &dialog, invalidate);
        QObject::connect(confirmed, &QCheckBox::toggled, &dialog, [this] { controls(); });
        QObject::connect(analyze, &QPushButton::clicked, &dialog, [this] { prepare(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] {
            if (!editable() || busy || !prepared || !confirmed->isChecked())
                return;
            committing = true;
            const auto state = doc.applyPreparedEdit(*prepared);
            committing = false;
            prepared.reset();
            if (state == EditState::Applied)
                dialog.accept();
            else {
                message->setText("취소·권한·문서 변경으로 ID 이관을 제출하지 않았습니다.");
                controls();
            }
        });
        QObject::connect(history, &QTableWidget::cellDoubleClicked, &dialog, [this](int row, int) {
            if (!source || busy || row < 0)
                return;
            prepared.reset();
            confirmed->setChecked(false);
            const auto &e = source->ledgerIdMigrations.at(row);
            show(e.before, e.after);
            message->setText("저장된 ID 이관 · " + q(e.reporter) + " · " + q(e.reason));
            controls();
        });
        QObject::connect(inspect, &QPushButton::clicked, &dialog, [this] {
            if (!beforeView || !afterView)
                return;
            QDialog child(&dialog);
            child.setObjectName("ledgerIdOriginals");
            child.setWindowTitle("관측 원장 ID 이관 전후 원본");
            auto *l = new QVBoxLayout(&child);
            auto *tabs = new QTabWidget(&child);
            for (bool before : {true, false}) {
                auto *text = new QPlainTextEdit(
                    QString::fromUtf8(QJsonDocument(json::encodeProject(before ? *beforeView : *afterView)).toJson()),
                    tabs);
                text->setReadOnly(true);
                tabs->addTab(text, before ? "이관 전 원본" : "ID 참조 이관 결과");
            }
            l->addWidget(tabs);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &child);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &child, &QDialog::reject);
            l->addWidget(buttons);
            child.resize(1000, 700);
            prepareEditorDialog(child);
            child.exec();
        });
        listener = doc.subscribe([this](Document::Event) {
            if (!committing && !current()) {
                if (control)
                    control->cancel();
                prepared.reset();
                message->setText("문서·권한이 바뀌었습니다. 창을 다시 여세요.");
                controls();
            }
        });
        controls();
        dialog.resize(1160, 740);
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
        doc.unsubscribe(listener);
    }
    bool current() const {
        return source && doc.project() && doc.sessionGeneration() == session &&
               doc.project()->documentId == source->documentId && doc.project()->revision == source->revision;
    }
    bool editable() const {
        return current() && !doc.readOnly();
    }
    void controls() {
        const bool enabled = editable() && !busy;
        mapping->setEnabled(enabled);
        reporter->setEnabled(enabled);
        reason->setEnabled(enabled);
        analyze->setEnabled(enabled && owners->rowCount() > 0);
        confirmed->setEnabled(enabled && bool(prepared));
        submit->setEnabled(enabled && prepared && confirmed->isChecked());
        cancel->setEnabled(busy);
        inspect->setEnabled(!busy && beforeView && afterView);
    }
    void show(std::shared_ptr<const Project> a, std::shared_ptr<const Project> b) {
        beforeView = std::move(a);
        afterView = std::move(b);
        auto *old = facts->model();
        facts->setModel(new Facts(beforeView, afterView, facts));
        if (old)
            old->deleteLater();
        tabs->setCurrentWidget(facts);
    }
    void prepare() {
        if (!editable() || busy)
            return;
        try {
            LedgerIdSubmission s;
            s.links = owners->links();
            s.reporter = reporter->text().trimmed().toStdString();
            s.reason = reason->text().trimmed().toStdString();
            s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
            s.confirmed = true;
            auto request = doc.makeEditRequest(LedgerIdEdit{std::move(s)});
            prepared.reset();
            confirmed->setChecked(false);
            busy = true;
            control = std::make_shared<CalculationControl>();
            controls();
            message->setText("원장·기준선·재무/계획 이력의 ID 참조와 보존값을 검증하고 있습니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog, [this, watcher] {
                auto r = watcher->result();
                watcher->deleteLater();
                busy = false;
                if (current() && !control->cancelRequested.load() && r->error.isEmpty()) {
                    prepared = r->edit;
                    show(r->before, r->after);
                    message->setText("ID 이관 검증 완료 · 원장 기록 " + QString::number(facts->model()->rowCount()) +
                                     "건 · 작업 " + QString::number(r->before->tasks.size()) + " → " +
                                     QString::number(r->after->tasks.size()) + " · 자원 " +
                                     QString::number(r->before->resources.size()) + " → " +
                                     QString::number(r->after->resources.size()) +
                                     "\n기록 ID·수치·관측/기록 시점은 보존합니다. 계획/원장/기준선 참조의 이관을 "
                                     "확인한 뒤 제출하세요.");
                } else {
                    prepared.reset();
                    message->setText(r->error.isEmpty() ? "취소되었거나 문서가 바뀌었습니다." : r->error);
                }
                controls();
            });
            watcher->setFuture(QtConcurrent::run([request = std::move(request), c = control]() mutable {
                auto r = std::make_shared<Preview>();
                try {
                    EvaluationScope run(c);
                    r->edit = Document::prepareEdit(std::move(request), c);
                    if (r->edit->state() != EditState::Ready) {
                        r->error = r->edit->failure() ? q(diagnosticMessage(*r->edit->failure()))
                                                      : "이관 검증을 취소했습니다.";
                        return r;
                    }
                    const auto p = r->edit->previewProject();
                    const auto &e = p->ledgerIdMigrations.back();
                    r->before = e.before;
                    r->after = e.after;
                } catch (const CalculationCancelled &) {
                    r->error = "취소했습니다.";
                } catch (const std::exception &e) {
                    r->error = q(errorText(e));
                }
                return r;
            }));
        } catch (const std::exception &e) {
            showEditorError(*message, e);
        }
    }
};
LedgerIdentityDialog::LedgerIdentityDialog(Document &d, QWidget *p) : QDialog(p) {
    setObjectName("ledgerIdentityDialog");
    setWindowTitle("관측 원장 ID 이관");
    impl_ = std::make_unique<Impl>(*this, d);
}
LedgerIdentityDialog::~LedgerIdentityDialog() = default;
void LedgerIdentityDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace jobs
