#include "ui/ProjectCurrencyDialog.h"
#include "ui/EditorUi.h"
#include "ui/MoneyFields.h"
#include "application/Document.h"
#include "domain/ProjectCurrency.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "formatting/DiagnosticText.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
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
class MoneyModel final : public QAbstractTableModel {
    std::vector<CurrencyMoneyRow> before_, after_;

public:
    MoneyModel(std::vector<CurrencyMoneyRow> a, std::vector<CurrencyMoneyRow> b, QObject *parent)
        : QAbstractTableModel(parent), before_(std::move(a)), after_(std::move(b)) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(before_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 5;
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return o == Qt::Horizontal && role == Qt::DisplayRole
                   ? QVariant(
                         QStringList{"금액 위치 / 기록 ID", "원래 통화", "원래 금액", "대상 통화", "환산 금액"}.at(c))
                   : QVariant{};
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        const auto &a = before_.at(i.row()), &b = after_.at(i.row());
        return QStringList{q(a.path), q(a.currency), moneyui::format(a.amount, a.currency, "미정"), q(b.currency),
                           moneyui::format(b.amount, b.currency, "미정")}
            .at(i.column());
    }
};
struct Preview {
    std::shared_ptr<Document::PreparedEdit> edit;
    std::shared_ptr<const Project> before, after;
    std::vector<CurrencyMoneyRow> beforeRows, afterRows;
    QString error;
};
} // namespace
struct ProjectCurrencyDialog::Impl {
    ProjectCurrencyDialog &dialog;
    Document &doc;
    std::shared_ptr<const Project> source, beforeView, afterView;
    std::uint64_t session, listener = 0;
    bool busy = false, committing = false;
    std::shared_ptr<CalculationControl> control;
    std::shared_ptr<Document::PreparedEdit> prepared;
    QComboBox *target, *rounding;
    QLineEdit *numerator, *denominator, *date, *reference, *reporter, *reason;
    QCheckBox *confirm;
    QLabel *message;
    QTableView *money;
    QTabWidget *tabs;
    QPushButton *analyze, *submit, *cancel, *inspect;
    Impl(ProjectCurrencyDialog &d, Document &document)
        : dialog(d), doc(document), source(document.snapshot()), session(document.sessionGeneration()) {
        auto *l = new QVBoxLayout(&dialog);
        auto *note = new QLabel("프로젝트의 단가·예산·원가·날짜별 원장·기준선/시나리오·기간 마감과 연결 재무 투영을 "
                                "환산합니다.\n기록 ID·날짜·공수·물리 진척은 유지하고 전환 전후 전체 원본을 이력에 "
                                "보존합니다. 개별 반올림으로 합계/원장 대사가 깨지면 제출하지 않습니다. 사용자 필드의 "
                                "임의 숫자·단위/계산식, 조직/다른 프로젝트의 금액은 자동 환산하지 않습니다. "
                                "일반 실행 취소로 전환 이력을 지우지 않으며 추가 전환은 새 이력으로 기록합니다.",
                                &dialog);
        note->setWordWrap(true);
        l->addWidget(note);
        auto *form = new QFormLayout;
        target = new QComboBox(&dialog);
        target->setObjectName("projectCurrencyTarget");
        for (const auto &c : QStringList{"KRW", "USD", "EUR", "JPY"})
            if (!source || c != q(source->currency))
                target->addItem(c);
        form->addRow("현재 " + (source ? q(source->currency) : QString{}) + " → 대상 통화", target);
        auto edit = [&](const char *name, const QString &value, int limit) {
            auto *v = new QLineEdit(value, &dialog);
            v->setObjectName(name);
            v->setMaxLength(limit);
            return v;
        };
        numerator = edit("projectCurrencyNumerator", "1", 10);
        denominator = edit("projectCurrencyDenominator", "1", 10);
        form->addRow("원래 1통화 단위 = 대상 통화 · 분자", numerator);
        form->addRow("환율 분모 · 예: USD 1 = KRW 1300이면 1300 / 1", denominator);
        rounding = new QComboBox(&dialog);
        rounding->setObjectName("projectCurrencyRounding");
        rounding->addItem("최소 통화 단위 반올림 · 정확히 절반이면 0에서 멀어짐", "half-away-from-zero-v1");
        rounding->addItem("정확한 최소 단위만 허용 · 반올림 필요 시 거절", "exact-minor-v1");
        form->addRow("반올림", rounding);
        date = edit("projectCurrencyDate", QDate::currentDate().toString(Qt::ISODate), 10);
        reference = edit("projectCurrencyReference", {}, 4096);
        reporter = edit("projectCurrencyReporter", {}, 1024);
        reason = edit("projectCurrencyReason", {}, 4096);
        form->addRow("환율 기준일", date);
        form->addRow("환율 출처·근거", reference);
        form->addRow("작성자", reporter);
        form->addRow("전환 사유", reason);
        l->addLayout(form);
        tabs = new QTabWidget(&dialog);
        tabs->setObjectName("projectCurrencyTabs");
        money = new QTableView(tabs);
        money->setObjectName("projectCurrencyMoney");
        money->horizontalHeader()->setDefaultSectionSize(180);
        tabs->addTab(money, "금액 전후 · 가로 스크롤로 조회");
        auto *history = new QTableWidget(source ? int(source->currencyConversions.size()) : 0, 6, tabs);
        history->setObjectName("projectCurrencyHistory");
        history->setHorizontalHeaderLabels(
            {"기록 UTC", "통화", "환율 분자/분모", "기준일/근거", "작성자/사유", "이력 ID"});
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->horizontalHeader()->setDefaultSectionSize(190);
        if (source)
            for (std::size_t i = 0; i < source->currencyConversions.size(); ++i) {
                const auto &e = source->currencyConversions[i];
                const QStringList v{q(e.recordedAt),
                                    q(e.rate.fromCurrency) + " → " + q(e.rate.targetCurrency),
                                    QString::number(e.rate.numerator) + " / " + QString::number(e.rate.denominator),
                                    q(e.rate.effectiveDate) + " · " + q(e.rate.reference),
                                    q(e.reporter) + " · " + q(e.reason),
                                    q(e.id)};
                for (int c = 0; c < 6; ++c)
                    history->setItem(int(i), c, new QTableWidgetItem(v[c]));
            }
        tabs->addTab(history, "불변 이력 · 두 번 눌러 조회");
        l->addWidget(tabs, 1);
        confirm = new QCheckBox(
            "환율·최소 단위/반올림과 전후 금액을 검토했고, 과거 원본 보존 및 프로젝트 전체 통화 전환을 확인했습니다.",
            &dialog);
        confirm->setObjectName("projectCurrencyConfirm");
        l->addWidget(confirm);
        message = new QLabel(&dialog);
        message->setObjectName("projectCurrencyMessage");
        message->setWordWrap(true);
        message->setTextFormat(Qt::PlainText);
        l->addWidget(message);
        auto *buttons = new QHBoxLayout;
        auto button = [&](const char *name, const QString &text) {
            auto *b = new QPushButton(text, &dialog);
            b->setObjectName(name);
            buttons->addWidget(b);
            return b;
        };
        analyze = button("projectCurrencyAnalyze", "환산 검증·미리보기");
        submit = button("projectCurrencySubmit", "검증한 통화 전환 제출");
        cancel = button("projectCurrencyCancel", "계산 취소");
        inspect = button("projectCurrencyInspect", "전후 원본 조회…");
        buttons->addStretch();
        auto *close = button("projectCurrencyClose", "닫기");
        l->addLayout(buttons);
        auto invalidate = [this] {
            const bool had = bool(prepared);
            prepared.reset();
            confirm->setChecked(false);
            if (had)
                message->setText("환율/입력이 바뀌었습니다. 전후 금액을 다시 검증하세요.");
            controls();
        };
        for (auto *v : {numerator, denominator, date, reference, reporter, reason})
            QObject::connect(v, &QLineEdit::textChanged, &dialog, invalidate);
        for (auto *v : {target, rounding})
            QObject::connect(v, &QComboBox::currentIndexChanged, &dialog, invalidate);
        QObject::connect(confirm, &QCheckBox::toggled, &dialog, [this] { controls(); });
        QObject::connect(analyze, &QPushButton::clicked, &dialog, [this] { prepare(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] {
            if (control)
                control->cancel();
        });
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] {
            if (!editable() || busy || !prepared || !confirm->isChecked())
                return;
            committing = true;
            const auto result = doc.applyPreparedEdit(*prepared);
            committing = false;
            prepared.reset();
            if (result == EditState::Applied)
                dialog.accept();
            else {
                message->setText("문서·권한 변경 또는 취소로 통화를 전환하지 않았습니다.");
                controls();
            }
        });
        QObject::connect(history, &QTableWidget::cellDoubleClicked, &dialog, [this](int row, int) {
            if (!source || busy || row < 0)
                return;
            prepared.reset();
            confirm->setChecked(false);
            const auto &e = source->currencyConversions.at(row);
            loadHistory(e);
        });
        QObject::connect(inspect, &QPushButton::clicked, &dialog, [this] {
            if (!beforeView || !afterView)
                return;
            QDialog child(&dialog);
            child.setObjectName("projectCurrencyOriginals");
            child.setWindowTitle("프로젝트 통화 전환 전후 원본");
            auto *l = new QVBoxLayout(&child);
            auto *tabs = new QTabWidget(&child);
            for (bool before : {true, false}) {
                auto *text = new QPlainTextEdit(
                    QString::fromUtf8(QJsonDocument(json::encodeProject(before ? *beforeView : *afterView)).toJson()),
                    tabs);
                text->setReadOnly(true);
                tabs->addTab(text, before ? "전환 전 원본" : "환산 결과");
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
                message->setText("문서·권한이 바뀌었습니다. 통화 전환 창을 다시 여세요.");
                controls();
            }
        });
        controls();
        dialog.resize(1100, 850);
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
        for (auto *v : {numerator, denominator, date, reference, reporter, reason})
            v->setEnabled(enabled);
        for (auto *v : {target, rounding})
            v->setEnabled(enabled);
        analyze->setEnabled(enabled);
        confirm->setEnabled(enabled && prepared);
        submit->setEnabled(enabled && prepared && confirm->isChecked());
        cancel->setEnabled(busy);
        inspect->setEnabled(!busy && beforeView && afterView);
    }
    void show(std::shared_ptr<const Project> a, std::shared_ptr<const Project> b, std::vector<CurrencyMoneyRow> av,
              std::vector<CurrencyMoneyRow> bv) {
        beforeView = std::move(a);
        afterView = std::move(b);
        auto *old = money->model();
        money->setModel(new MoneyModel(std::move(av), std::move(bv), money));
        money->setColumnWidth(0, 360);
        money->setColumnWidth(1, 80);
        money->setColumnWidth(2, 150);
        money->setColumnWidth(3, 80);
        money->setColumnWidth(4, 150);
        money->horizontalHeader()->setStretchLastSection(true);
        if (old)
            old->deleteLater();
        tabs->setCurrentWidget(money);
        QTimer::singleShot(0, &dialog, [this] {
            if (auto *scroll = dialog.findChild<QScrollArea *>("editorScrollArea"))
                scroll->ensureWidgetVisible(confirm, 8, 8);
        });
    }
    void loadHistory(const CurrencyConversion &e) {
        busy = true;
        control = std::make_shared<CalculationControl>();
        controls();
        message->setText("저장된 통화 전환의 전후 금액을 불러오고 있습니다…");
        auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
        const auto description = "저장된 통화 전환 · " + q(e.rate.effectiveDate) + " · " + q(e.rate.reference) + " · " +
                                 q(e.reporter) + " · " + q(e.reason);
        QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog,
                         [this, watcher, description] {
                             auto r = watcher->result();
                             watcher->deleteLater();
                             busy = false;
                             if (current() && !control->cancelRequested.load() && r->error.isEmpty()) {
                                 show(r->before, r->after, std::move(r->beforeRows), std::move(r->afterRows));
                                 message->setText(description);
                             } else
                                 message->setText(r->error.isEmpty() ? "이력 조회를 취소했습니다." : r->error);
                             controls();
                         });
        watcher->setFuture(QtConcurrent::run([a = e.before, b = e.after, c = control] {
            auto r = std::make_shared<Preview>();
            try {
                EvaluationScope scope(c);
                r->before = a;
                r->after = b;
                r->beforeRows = currencyMoneyRows(*a);
                r->afterRows = currencyMoneyRows(*b);
            } catch (const CalculationCancelled &) {
                r->error = "취소했습니다.";
            } catch (const std::exception &e) {
                r->error = q(errorText(e));
            }
            return r;
        }));
    }
    void prepare() {
        if (!editable() || busy)
            return;
        try {
            CurrencyConversionSubmission s;
            s.rate.fromCurrency = source->currency;
            s.rate.targetCurrency = target->currentText().toStdString();
            s.rate.effectiveDate = date->text().trimmed().toStdString();
            s.rate.reference = reference->text().trimmed().toStdString();
            s.rate.rounding = rounding->currentData().toString().toStdString();
            bool n = false, d = false;
            s.rate.numerator = numerator->text().toLongLong(&n);
            s.rate.denominator = denominator->text().toLongLong(&d);
            if (!n || !d)
                throw AppError(Diagnostic{"CURRENCY_CONVERSION_INVALID",
                                          {"환율은 양의 정수 분자/분모로 입력하세요."},
                                          {},
                                          "CURRENCY_CONVERSION_INVALID"});
            s.reporter = reporter->text().trimmed().toStdString();
            s.reason = reason->text().trimmed().toStdString();
            s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
            s.confirmed = true;
            auto request = doc.makeEditRequest(CurrencyConversionEdit{std::move(s)});
            prepared.reset();
            confirm->setChecked(false);
            busy = true;
            control = std::make_shared<CalculationControl>();
            controls();
            message->setText("프로젝트 전체 금액·원장·기간/외부 재무 집계의 환산과 보존 정보를 검증하고 있습니다…");
            auto *watcher = new QFutureWatcher<std::shared_ptr<Preview>>(&dialog);
            QObject::connect(watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, &dialog, [this, watcher] {
                auto r = watcher->result();
                watcher->deleteLater();
                busy = false;
                if (current() && !control->cancelRequested.load() && r->error.isEmpty()) {
                    prepared = r->edit;
                    show(r->before, r->after, std::move(r->beforeRows), std::move(r->afterRows));
                    message->setText("환산 검증 완료 · 금액 항목 " + QString::number(money->model()->rowCount()) +
                                     "개 · " + q(r->before->currency) + " → " + q(r->after->currency) +
                                     "\n개별 금액의 반올림과 계산 합계의 차이는 있을 수 있습니다. 원래 문서/원장과 "
                                     "환율은 이력에 보존합니다.");
                } else {
                    prepared.reset();
                    message->setText(r->error.isEmpty() ? "취소되었거나 문서가 바뀌었습니다." : r->error);
                }
                controls();
            });
            watcher->setFuture(QtConcurrent::run([request = std::move(request), c = control]() mutable {
                auto r = std::make_shared<Preview>();
                try {
                    EvaluationScope scope(c);
                    r->edit = Document::prepareEdit(std::move(request), c);
                    if (r->edit->state() != EditState::Ready) {
                        r->error = r->edit->failure() ? q(diagnosticMessage(*r->edit->failure()))
                                                      : "통화 전환 검증을 취소했습니다.";
                        return r;
                    }
                    const auto p = r->edit->previewProject();
                    const auto storage = QJsonDocument(QJsonObject{{"format", "JOBS"},
                                                                   {"schemaVersion", 34},
                                                                   {"payload", json::encodeProject(*p)},
                                                                   {"sha256", QString(64, '0')}})
                                             .toJson();
                    if (storage.size() > projectFileLimitBytes - 4096)
                        throw AppError(ErrorCode::FILE_SIZE_LIMIT);
                    const auto &e = p->currencyConversions.back();
                    r->before = e.before;
                    r->after = e.after;
                    r->beforeRows = currencyMoneyRows(*e.before);
                    r->afterRows = currencyMoneyRows(*e.after);
                    if (r->beforeRows.size() != r->afterRows.size())
                        throw std::logic_error("currency preview row mismatch");
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
ProjectCurrencyDialog::ProjectCurrencyDialog(Document &doc, QWidget *parent) : QDialog(parent) {
    setObjectName("projectCurrencyDialog");
    setWindowTitle("프로젝트 전체 통화 전환");
    impl_ = std::make_unique<Impl>(*this, doc);
}
ProjectCurrencyDialog::~ProjectCurrencyDialog() = default;
void ProjectCurrencyDialog::reject() {
    if (impl_->control)
        impl_->control->cancel();
    QDialog::reject();
}
} // namespace jobs
