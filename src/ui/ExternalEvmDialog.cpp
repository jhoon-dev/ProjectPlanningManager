#include "ui/ExternalEvmDialog.h"
#include "domain/ExternalEvm.h"
#include "calculation/Costs.h"
#include "formatting/DiagnosticText.h"
#include "formatting/Money.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <set>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
class SourceModel final : public QAbstractTableModel {
    const Project &p_;
    const XerFinancialSnapshot *source_;
    struct Choice {
        std::size_t index;
        bool selected = false;
        Money ev, pv;
    };
    std::vector<Choice> rows_;
    std::set<std::string> eligible_;
    std::map<std::string, QString> names_;

public:
    bool editable = false;
    std::function<void(const QString &)> error;
    SourceModel(const Project &p, QObject *parent)
        : QAbstractTableModel(parent), p_(p), source_(activeXerFinancials(p)) {
        for (const auto &t : p.tasks)
            names_[t.id] = q(t.name);
        if (source_)
            for (std::size_t i = 0; i < source_->periods.size(); ++i) {
                const auto &r = source_->periods[i];
                if (r.resourceId.empty() && (r.bcwp || r.bcws))
                    rows_.push_back({i, false, {}, {}});
            }
    }
    void baseline(const std::string &id) {
        beginResetModel();
        eligible_.clear();
        for (const auto &b : p_.baselines)
            if (b.id == id)
                for (const auto &t : b.input.tasks)
                    if (names_.contains(t.id))
                        eligible_.insert(t.id);
        for (auto &r : rows_)
            r.selected = false;
        endResetModel();
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 10;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        return o == Qt::Vertical
                   ? QVariant(c + 1)
                   : QVariant(QStringList{"채택", "작업", "기간 시작", "기간 마감", "원본 기간 EV", "원본 기간 PV",
                                          "명시 개시 EV", "명시 개시 PV", "채택 마감 EV", "채택 마감 PV"}
                                  .value(c));
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid())
            return {};
        const auto &c = rows_.at(std::size_t(i.row()));
        const auto &r = source_->periods[c.index];
        if (role == Qt::CheckStateRole && i.column() == 0)
            return c.selected ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::ToolTipRole)
            return "작업 ID " + q(r.taskId) + " · 원본 기간 참조 " + QString::number(c.index) +
                   "\n구간 내부/공백은 보간하지 않습니다.";
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        auto m = [&](Money n) {
            return moneyui::format(n, p_.currency, role == Qt::EditRole ? QString{} : QString("미정"));
        };
        auto sum = [](Money a, Money b) -> Money { return a && b && *a <= maxMoney - *b ? Money{*a + *b} : Money{}; };
        const auto [begin, end] = externalEvmBounds(*source_, r);
        switch (i.column()) {
        case 0:
            return eligible_.contains(r.taskId) ? QString{} : QString("대상 없음");
        case 1:
            return names_.contains(r.taskId) ? names_.at(r.taskId) : q(r.taskId);
        case 2:
            return q(begin) + " 미만";
        case 3:
            return q(end) + " 이하";
        case 4:
            return m(r.bcwp);
        case 5:
            return m(r.bcws);
        case 6:
            return m(c.ev);
        case 7:
            return m(c.pv);
        case 8:
            return m(sum(c.ev, r.bcwp));
        case 9:
            return m(sum(c.pv, r.bcws));
        default:
            return {};
        }
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (i.isValid() && editable && eligible_.contains(source_->periods[rows_[std::size_t(i.row())].index].taskId)) {
            if (i.column() == 0)
                flags |= Qt::ItemIsUserCheckable;
            if (i.column() == 6 || i.column() == 7)
                flags |= Qt::ItemIsEditable;
        }
        return flags;
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (!i.isValid() || !editable ||
            !eligible_.contains(source_->periods[rows_[std::size_t(i.row())].index].taskId))
            return false;
        auto &r = rows_[std::size_t(i.row())];
        if (role == Qt::CheckStateRole && i.column() == 0)
            r.selected = v.toInt() == Qt::Checked;
        else if (role == Qt::EditRole && (i.column() == 6 || i.column() == 7)) {
            try {
                auto n = moneyui::parse(v.toString(), p_.currency);
                if (i.column() == 6)
                    r.ev = n;
                else
                    r.pv = n;
            } catch (const std::exception &e) {
                if (error)
                    error(q(diagnosticMessage(diagnosticFromException(e))));
                return false;
            }
        } else
            return false;
        emit dataChanged(index(i.row(), 0), index(i.row(), 9));
        return true;
    }
    void fill(ExternalEvmSubmission &s) const {
        std::set<std::string> tasks;
        for (const auto &r : rows_)
            if (r.selected) {
                s.rows.push_back({r.index, s.useEv ? r.ev : Money{}, s.usePv ? r.pv : Money{}});
                tasks.insert(source_->periods[r.index].taskId);
            }
        s.taskIds.assign(tasks.begin(), tasks.end());
    }
};
struct Preview {
    CostAnalysis before, after;
    std::shared_ptr<Document::PreparedEdit> edit;
    QString error;
};
class PreviewModel final : public QAbstractTableModel {
    std::shared_ptr<const Preview> result_;

public:
    PreviewModel(std::shared_ptr<const Preview> r, QObject *p) : QAbstractTableModel(p), result_(std::move(r)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(result_->after.tasks.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 13;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        return o == Qt::Vertical ? QVariant(c + 1)
                                 : QVariant(QStringList{"작업", "동결 BAC", "현재 PV", "채택 PV", "현재 EV", "채택 EV",
                                                        "native AC", "채택 CV", "채택 SV", "CPI", "SPI", "EAC", "ETC"}
                                                .value(c));
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        const auto &a = result_->before.tasks.at(std::size_t(i.row()));
        const auto &b = result_->after.tasks.at(std::size_t(i.row()));
        if (role == Qt::ToolTipRole)
            return "작업 ID " + q(b.taskId) +
                   "\n채택 범위 밖/내부는 미정. BAC 초과 시 EAC/ETC 미정. AC·물리 진척·원장은 유지합니다.";
        auto m = [&](Money n) { return moneyui::format(n, result_->after.currency, "미정"); };
        auto ratio = [](auto n) { return n ? QString::number(*n, 'f', 4) : QString("미정"); };
        switch (i.column()) {
        case 0:
            return q(b.name);
        case 1:
            return m(b.evm.bac);
        case 2:
            return m(a.evm.pv);
        case 3:
            return m(b.evm.pv);
        case 4:
            return m(a.evm.ev);
        case 5:
            return m(b.evm.ev);
        case 6:
            return m(b.evm.ac);
        case 7:
            return m(b.evm.cv);
        case 8:
            return m(b.evm.sv);
        case 9:
            return ratio(b.evm.cpi);
        case 10:
            return ratio(b.evm.spi);
        case 11:
            return m(b.evm.eac);
        case 12:
            return m(b.evm.etc);
        default:
            return {};
        }
    }
};
} // namespace
struct ExternalEvmDialog::Impl {
    ExternalEvmDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source;
    std::uint64_t session = 0, generation = 0;
    bool busy = false, committing = false, saved = false;
    QComboBox *baseline, *mode;
    QCheckBox *ev, *pv, *confirmed;
    QLineEdit *reporter, *reason, *search;
    QLabel *message;
    QPushButton *analyze, *submit, *cancel;
    QTabWidget *tabs;
    QTableView *values, *preview;
    SourceModel *model;
    std::shared_ptr<Preview> prepared;
    std::shared_ptr<CalculationControl> control;
    Impl(ExternalEvmDialog &d, Document &document, const std::string &id)
        : dialog(d), doc(document), source(std::make_shared<const Project>(*doc.project())),
          session(doc.sessionGeneration()), saved(doc.revisionSaved()) {
        dialog.setObjectName("externalEvmDialog");
        dialog.setWindowTitle("외부 XER EV/PV native 채택 · 명시 대체/철회");
        dialog.resize(1100, 760);
        auto *layout = new QVBoxLayout(&dialog);
        auto *meta = new QLabel(&dialog);
        meta->setObjectName("externalEvmSource");
        meta->setTextFormat(Qt::PlainText);
        meta->setWordWrap(true);
        if (const auto *s = activeXerFinancials(*source))
            meta->setText("보존 집계 " + q(s->sourceName) + " · 프로젝트 " + q(s->projectId) + " · 원본 상태일 " +
                          q(s->statusAt) + " · " + q(s->currency) + "\nSHA256 " + q(s->sourceSha256) +
                          "\n재무 revision " + q(latestExternalEvmSourceId(*source)) + " · native 상태일 " +
                          q(source->statusAt.value_or("미설정")));
        else
            meta->setText("보존한 XER 재무 집계가 없습니다. XER를 가져오거나 새 버전 재연결을 먼저 진행하세요.");
        layout->addWidget(meta);
        auto *choices = new QHBoxLayout;
        layout->addLayout(choices);
        baseline = new QComboBox(&dialog);
        baseline->setObjectName("externalEvmBaseline");
        for (const auto &b : source->baselines)
            baseline->addItem("v" + QString::number(b.version) + " · " + q(b.name) + " · " + q(b.input.currency),
                              q(b.id));
        if (baseline->count())
            baseline->setCurrentIndex(id.empty() ? baseline->count() - 1 : std::max(0, baseline->findData(q(id))));
        mode = new QComboBox(&dialog);
        mode->setObjectName("externalEvmMode");
        mode->addItems({"선택 기간 채택·재채택", "선택 기준선의 활성 채택 전체 철회"});
        choices->addWidget(new QLabel("native 기준선", &dialog));
        choices->addWidget(baseline, 1);
        choices->addWidget(mode, 1);
        ev = new QCheckBox("EV 대체", &dialog);
        pv = new QCheckBox("PV 대체", &dialog);
        ev->setChecked(true);
        pv->setChecked(true);
        ev->setObjectName("externalEvmUseEv");
        pv->setObjectName("externalEvmUsePv");
        choices->addWidget(ev);
        choices->addWidget(pv);
        auto *note = new QLabel(
            "TASKFIN의 기간 금액 + 명시 개시 누계 = 채택 마감 누계. 기본 개시값은 공란입니다.\n확인한 개시·마감만 "
            "알려진 값이며 내부/공백/범위 밖은 미정입니다. native 값에 더하지 않고 대체합니다.\nAC·물리 "
            "진척·Steps·기준선·원본은 유지합니다. 외부 금액이 BAC를 넘으면 EAC/ETC는 미정입니다.",
            &dialog);
        note->setWordWrap(true);
        note->setTextFormat(Qt::PlainText);
        layout->addWidget(note);
        search = new QLineEdit(&dialog);
        search->setObjectName("externalEvmSearch");
        search->setPlaceholderText("작업·기간 검색");
        layout->addWidget(search);
        tabs = new QTabWidget(&dialog);
        layout->addWidget(tabs, 1);
        values = new QTableView(&dialog);
        values->setObjectName("externalEvmPeriods");
        model = new SourceModel(*source, values);
        model->baseline(baseline->currentData().toString().toStdString());
        auto *filter = new QSortFilterProxyModel(values);
        filter->setSourceModel(model);
        filter->setFilterKeyColumn(-1);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        values->setModel(filter);
        values->setAlternatingRowColors(true);
        values->setSelectionBehavior(QAbstractItemView::SelectRows);
        values->setWordWrap(false);
        for (int c = 0; c < 10; ++c)
            values->setColumnWidth(c, c == 0 ? 65 : c == 1 ? 140 : c == 2 || c == 3 ? 185 : 125);
        tabs->addTab(values, "원본 기간·명시 개시");
        preview = new QTableView(&dialog);
        preview->setObjectName("externalEvmPreview");
        preview->setWordWrap(false);
        preview->setAlternatingRowColors(true);
        tabs->addTab(preview, "현재·채택 EVM 미리보기");
        auto *form = new QFormLayout;
        layout->addLayout(form);
        reporter = new QLineEdit(&dialog);
        reason = new QLineEdit(&dialog);
        reporter->setObjectName("externalEvmReporter");
        reason->setObjectName("externalEvmReason");
        form->addRow("작성자", reporter);
        form->addRow("채택/철회 사유", reason);
        confirmed =
            new QCheckBox("선택 기준선·작업과 개시 누계, native EV/PV 대체 및 미정 범위를 확인했습니다.", &dialog);
        confirmed->setObjectName("externalEvmConfirmed");
        layout->addWidget(confirmed);
        message = new QLabel(&dialog);
        message->setObjectName("externalEvmMessage");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        message->setText("기간을 선택하고 개시 누계·작성자·사유를 입력한 뒤 확인하고 검증하세요. 철회는 선택 기준선의 "
                         "활성 채택 전체입니다.");
        layout->addWidget(message);
        auto *footer = new QHBoxLayout;
        layout->addLayout(footer);
        analyze = new QPushButton("채택 검증·미리보기", &dialog);
        analyze->setObjectName("externalEvmAnalyze");
        submit = new QPushButton("검증된 채택/철회 제출", &dialog);
        submit->setObjectName("externalEvmSubmit");
        cancel = new QPushButton("계산 취소", &dialog);
        cancel->setObjectName("externalEvmCancel");
        auto *close = new QPushButton("닫기", &dialog);
        close->setObjectName("externalEvmClose");
        footer->addWidget(analyze);
        footer->addWidget(submit);
        footer->addWidget(cancel);
        footer->addStretch();
        footer->addWidget(close);
        QObject::connect(close, &QPushButton::clicked, &dialog, &ExternalEvmDialog::reject);
        QObject::connect(search, &QLineEdit::textChanged, filter, &QSortFilterProxyModel::setFilterFixedString);
        QObject::connect(model, &QAbstractItemModel::dataChanged, &dialog, [this] { invalidate(true); });
        model->error = [this](const QString &error) {
            invalidate(true);
            message->setText(error);
        };
        QObject::connect(baseline, &QComboBox::currentIndexChanged, &dialog, [this] {
            model->baseline(baseline->currentData().toString().toStdString());
            invalidate(true);
        });
        QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [this] { invalidate(true); });
        for (auto *c : {ev, pv})
            QObject::connect(c, &QCheckBox::toggled, &dialog, [this] { invalidate(true); });
        for (auto *c : {reporter, reason})
            QObject::connect(c, &QLineEdit::textChanged, &dialog, [this] { invalidate(true); });
        QObject::connect(confirmed, &QCheckBox::toggled, &dialog, [this] { invalidate(false); });
        QObject::connect(analyze, &QPushButton::clicked, &dialog, [this] { prepare(); });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] { commit(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            invalidate(false);
            message->setText("계산을 취소했습니다.");
        });
        auto *timer = new QTimer(&dialog);
        timer->setInterval(200);
        QObject::connect(timer, &QTimer::timeout, &dialog, [this] {
            if (!committing && !current()) {
                if (control)
                    control->cancel();
                prepared.reset();
                controls();
                message->setText("문서/revision/session/저장 상태가 변경되었습니다. 창을 다시 열어 검토하세요.");
            }
        });
        timer->start();
        controls();
        prepareEditorDialog(dialog);
    }
    ~Impl() {
        if (control)
            control->cancel();
    }
    bool current() const {
        return doc.project() && doc.project()->documentId == source->documentId &&
               doc.project()->revision == source->revision && doc.sessionGeneration() == session &&
               doc.revisionSaved() == saved;
    }
    void controls() {
        const bool edit = current() && !doc.readOnly();
        analyze->setEnabled(edit && !busy && confirmed->isChecked() && baseline->count() &&
                            activeXerFinancials(*source));
        submit->setEnabled(edit && !busy && confirmed->isChecked() && prepared && prepared->edit &&
                           prepared->edit->state() == EditState::Ready);
        cancel->setEnabled(busy);
        model->editable = edit && !busy && mode->currentIndex() == 0;
        for (auto *c : {baseline, mode})
            c->setEnabled(!busy && current());
        ev->setEnabled(edit && !busy && mode->currentIndex() == 0);
        pv->setEnabled(ev->isEnabled());
        reporter->setReadOnly(!edit || busy);
        reason->setReadOnly(!edit || busy);
        confirmed->setEnabled(edit && !busy);
    }
    void invalidate(bool reset) {
        if (control)
            control->cancel();
        ++generation;
        busy = false;
        prepared.reset();
        if (auto *old = preview->model()) {
            preview->setModel(nullptr);
            old->deleteLater();
        }
        if (reset) {
            QSignalBlocker block(confirmed);
            confirmed->setChecked(false);
        }
        message->setText("입력이 바뀌었습니다. 확인한 뒤 다시 검증하세요.");
        controls();
    }
    ExternalEvmSubmission submission() const {
        auto s = externalEvmDraft(*source, baseline->currentData().toString().toStdString());
        s.useEv = ev->isChecked();
        s.usePv = pv->isChecked();
        s.withdrawn = mode->currentIndex() == 1;
        s.confirmed = confirmed->isChecked();
        s.reporter = reporter->text().trimmed().toStdString();
        s.reason = reason->text().trimmed().toStdString();
        s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
        if (s.withdrawn) {
            s.useEv = s.usePv = false;
            std::map<std::string, bool> active;
            for (const auto &a : source->externalEvmAdoptions)
                if (a.baselineId == s.baselineId)
                    for (const auto &id : a.taskIds)
                        active[id] = !a.withdrawn;
            for (const auto &[id, on] : active)
                if (on)
                    s.taskIds.push_back(id);
        } else
            model->fill(s);
        return s;
    }
    void prepare() {
        if (!current() || doc.readOnly() || busy)
            return;
        try {
            const auto s = submission();
            auto request = doc.makeEditRequest(ExternalEvmEdit{s});
            prepared.reset();
            busy = true;
            control = std::make_shared<CalculationControl>();
            const auto c = control;
            const auto version = ++generation;
            controls();
            message->setText("출처·경계·개시 누계 검증과 현재/채택 EVM을 계산 중입니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog,
                             [this, watcher, version, c] {
                                 auto result = watcher->result();
                                 watcher->deleteLater();
                                 if (version != generation)
                                     return;
                                 busy = false;
                                 if (c->cancelRequested.load() || !current()) {
                                     prepared.reset();
                                     message->setText("취소 또는 문서 변경으로 반영하지 않았습니다.");
                                 } else if (!result->error.isEmpty()) {
                                     message->setText(result->error);
                                 } else {
                                     prepared = result;
                                     if (auto *old = preview->model())
                                         old->deleteLater();
                                     preview->setModel(new PreviewModel(result, preview));
                                     for (int col = 0; col < 13; ++col)
                                         preview->setColumnWidth(col, col == 0 ? 155 : 120);
                                     tabs->setCurrentWidget(preview);
                                     message->setText("채택 검증 완료. 현재/채택 EV·PV와 native AC, 미정·BAC 불일치를 "
                                                      "확인한 뒤 제출하세요. 제출은 새 불변 이력입니다.");
                                 }
                                 controls();
                             });
            watcher->setFuture(QtConcurrent::run([p = source, s, c, request = std::move(request)]() mutable {
                auto out = std::make_shared<Preview>();
                try {
                    EvaluationScope run(c);
                    out->edit = Document::prepareEdit(std::move(request), c);
                    if (out->edit->state() != EditState::Ready) {
                        out->error =
                            out->edit->failure() ? q(diagnosticMessage(*out->edit->failure())) : q(out->edit->error());
                        if (out->error.isEmpty())
                            out->error = "채택 준비가 취소되었습니다.";
                        return out;
                    }
                    auto candidate = *p;
                    auto a = prepareExternalEvmAdoption(
                        candidate, s, QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
                    candidate.externalEvmAdoptions.push_back(std::move(a));
                    ++candidate.revision;
                    out->before = analyzeCosts(*p, s.baselineId, "budget-remaining-v1", s.taskIds);
                    out->after = analyzeCosts(candidate, s.baselineId, "budget-remaining-v1", s.taskIds);
                    calculationCheckpoint();
                } catch (const CalculationCancelled &) {
                    out->error = "계산을 취소했습니다.";
                } catch (const std::exception &e) {
                    out->error = q(diagnosticMessage(diagnosticFromException(e)));
                }
                return out;
            }));
        } catch (const std::exception &e) {
            message->setText(q(diagnosticMessage(diagnosticFromException(e))));
            controls();
        }
    }
    void commit() {
        if (!current() || doc.readOnly() || !prepared || !prepared->edit || busy || !confirmed->isChecked())
            return;
        committing = true;
        auto state = doc.applyPreparedEdit(*prepared->edit);
        committing = false;
        prepared.reset();
        if (state == EditState::Applied)
            dialog.accept();
        else {
            message->setText("취소/권한/문서 변경으로 채택을 제출하지 않았습니다.");
            controls();
        }
    }
};
ExternalEvmDialog::ExternalEvmDialog(Document &doc, const std::string &baseline, QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, doc, baseline)) {}
ExternalEvmDialog::~ExternalEvmDialog() = default;
void ExternalEvmDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    ++impl_->generation;
    QDialog::reject();
}
} // namespace jobs
