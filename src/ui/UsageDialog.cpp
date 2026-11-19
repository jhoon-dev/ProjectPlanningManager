#include "ui/UsageDialog.h"
#include "ui/PeriodActualDialog.h"
#include "ui/EditorUi.h"
#include "ui/SandboxAccess.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/DiagnosticText.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeView>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
struct PreparedUsage {
    std::shared_ptr<UsageResult> result;
    QString error;
};
} // namespace
UsageDialog::UsageDialog(Document &document, const std::string &wbs, const std::string &baseline, QWidget *parent)
    : QDialog(parent), document_(document) {
    setObjectName("usageDialog");
    setWindowTitle("작업·자원 시간축 Usage");
    auto *body = new QVBoxLayout(this);
    auto *note = new QLabel(
        "계획·기준선·잔여 조회 · 인시와 장비시는 별도 단위 · 잔여 원가는 자원 배정만(고정비 제외)\n"
        "보고 실제는 입력한 부분의 합계 · 누적 실제원가와 기간별 배분은 더하지 않음 · 미배분은 전기간 기준 대사",
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    body->addWidget(note);
    auto *selection = new QHBoxLayout;
    scope_ = new QComboBox(this);
    scope_->setObjectName("usageScope");
    scope_->addItem("프로젝트 전체", QString{});
    baseline_ = new QComboBox(this);
    baseline_->setObjectName("usageBaseline");
    baseline_->addItem("기준선 없음", QString{});
    if (document_.project()) {
        const auto &p = *document_.project();
        for (const auto &n : p.wbsNodes)
            scope_->addItem(qs(n.name), qs(n.id));
        for (const auto &b : p.baselines) {
            baseline_->addItem(qs(b.name) + " · v" + QString::number(b.version), qs(b.id));
            for (const auto &n : b.input.wbsNodes)
                if (scope_->findData(qs(n.id)) < 0)
                    scope_->addItem(qs(n.name) + " · 기준선", qs(n.id));
        }
    }
    scope_->setCurrentIndex(std::max(0, scope_->findData(qs(wbs))));
    baseline_->setCurrentIndex(std::max(0, baseline_->findData(qs(baseline))));
    selection->addWidget(new QLabel("범위", this));
    selection->addWidget(scope_, 1);
    selection->addWidget(new QLabel("기준선", this));
    selection->addWidget(baseline_, 1);
    body->addLayout(selection);
    auto *range = new QHBoxLayout;
    from_ = new QDateEdit(this);
    from_->setObjectName("usageFrom");
    to_ = new QDateEdit(this);
    to_->setObjectName("usageTo");
    for (auto *edit : {from_, to_}) {
        edit->setDisplayFormat("yyyy-MM-dd");
        edit->setCalendarPopup(true);
        edit->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    }
    const auto start =
        document_.project() ? QDate::fromString(qs(document_.project()->startDate), Qt::ISODate) : QDate(2026, 11, 3);
    from_->setDate(start);
    to_->setDate(start.addDays(13));
    period_ = new QComboBox(this);
    period_->setObjectName("usagePeriod");
    period_->addItems({"일", "주 · 월요일 시작", "월"});
    hierarchy_ = new QComboBox(this);
    hierarchy_->setObjectName("usageHierarchy");
    hierarchy_->addItems({"작업 → 배정", "자원 → 배정"});
    metric_ = new QComboBox(this);
    metric_->setObjectName("usageMetric");
    metric_->addItems({"계획 공수", "기준선 공수", "잔여 공수", "계획 원가", "기준선 원가", "잔여 배정원가",
                       "보고 실제공수", "보고 실제원가"});
    range->addWidget(from_);
    range->addWidget(new QLabel("~", this));
    range->addWidget(to_);
    range->addWidget(period_);
    range->addWidget(hierarchy_);
    range->addWidget(metric_);
    body->addLayout(range);
    auto *actions = new QHBoxLayout;
    refresh_ = new QPushButton("조회", this);
    refresh_->setObjectName("refreshUsage");
    cancel_ = new QPushButton("계산 취소", this);
    cancel_->setObjectName("cancelUsage");
    cancel_->setEnabled(false);
    xlsx_ = new QPushButton("XLSX", this);
    xlsx_->setObjectName("exportUsageXlsx");
    pdf_ = new QPushButton("PDF", this);
    pdf_->setObjectName("exportUsagePdf");
    for (auto *button : {refresh_, cancel_, xlsx_, pdf_})
        actions->addWidget(button);
    actions->addStretch();
    auto *actual = new QPushButton("기간 실적 입력/정정…", this);
    actual->setObjectName("editPeriodActuals");
    actions->addWidget(actual);
    connect(actual, &QPushButton::clicked, this, [this] {
        const auto task = tree_->currentIndex().data(Qt::UserRole).toString().toStdString();
        PeriodActualDialog dialog(document_, task, this);
        dialog.exec();
        if (!exporting_)
            prepare();
    });
    body->addLayout(actions);
    message_ = new QLabel(this);
    message_->setObjectName("usageMessage");
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    body->addWidget(message_);
    tree_ = new QTreeView(this);
    tree_->setObjectName("usageTree");
    model_ = new UsageModel(tree_);
    tree_->setModel(model_);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
    body->addWidget(tree_, 1);
    diagnostics_ = new QLabel(this);
    diagnostics_->setObjectName("usageDiagnostics");
    diagnostics_->setWordWrap(true);
    diagnostics_->setTextFormat(Qt::PlainText);
    body->addWidget(diagnostics_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    body->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &UsageDialog::reject);
    connect(refresh_, &QPushButton::clicked, this, [this] { prepare(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    connect(hierarchy_, &QComboBox::currentIndexChanged, this, [this] {
        if (result_)
            showResult();
    });
    connect(metric_, &QComboBox::currentIndexChanged, this, [this] {
        model_->setMetric(metric_->currentIndex());
        tree_->expandToDepth(1);
    });
    auto invalidate = [this] {
        if (control_)
            control_->cancel();
        ++generation_;
        result_.reset();
        model_->setUsage({}, UsageHierarchy::Tasks);
        xlsx_->setEnabled(false);
        pdf_->setEnabled(false);
        cancel_->setEnabled(false);
        message_->setText("조회 조건이 바뀌었습니다. 조회를 눌러 갱신하세요.");
    };
    for (auto *choice : {scope_, baseline_, period_})
        connect(choice, &QComboBox::currentIndexChanged, this, invalidate);
    for (auto *edit : {from_, to_})
        connect(edit, &QDateEdit::dateChanged, this, invalidate);
    listener_ = document_.subscribe([this](Document::Event) {
        if (control_)
            control_->cancel();
        ++generation_;
        result_.reset();
        model_->setUsage({}, UsageHierarchy::Tasks);
        xlsx_->setEnabled(false);
        pdf_->setEnabled(false);
        cancel_->setEnabled(false);
        message_->setText("문서가 변경되었습니다. 창을 다시 열어 최신 입력으로 조회하세요.");
    });
    connect(xlsx_, &QPushButton::clicked, this, [this] { exportFile(false); });
    connect(pdf_, &QPushButton::clicked, this, [this] { exportFile(true); });
    xlsx_->setEnabled(false);
    pdf_->setEnabled(false);
    resize(1260, 760);
    prepareEditorDialog(*this);
    QTimer::singleShot(0, this, [this] { prepare(); });
}
UsageDialog::~UsageDialog() {
    if (control_)
        control_->cancel();
    document_.unsubscribe(listener_);
}
bool UsageDialog::current() const {
    return result_ && document_.project() && document_.sessionGeneration() == sourceSession_ &&
           document_.project()->documentId == result_->documentId &&
           document_.project()->revision == result_->revision && (!document_.revisionSaved()) == sourceUnsaved_;
}
void UsageDialog::prepare() {
    if (exporting_ || !document_.project())
        return;
    if (control_)
        control_->cancel();
    control_ = std::make_shared<CalculationControl>();
    const auto generation = ++generation_;
    result_.reset();
    model_->setUsage({}, UsageHierarchy::Tasks);
    diagnostics_->clear();
    xlsx_->setEnabled(false);
    pdf_->setEnabled(false);
    cancel_->setEnabled(true);
    message_->setText("시간축 계산 중…");
    sourceSession_ = document_.sessionGeneration();
    sourceUnsaved_ = !document_.revisionSaved();
    auto snapshot = *document_.project();
    UsageOptions options{
        from_->date().toString(Qt::ISODate).toStdString(), to_->date().toString(Qt::ISODate).toStdString(),
        baseline_->currentData().toString().toStdString(), scope_->currentData().toString().toStdString(),
        static_cast<UsagePeriod>(period_->currentIndex())};
    auto *watcher = new QFutureWatcher<PreparedUsage>(this);
    connect(watcher, &QFutureWatcher<PreparedUsage>::finished, this, [this, watcher, generation] {
        auto prepared = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        cancel_->setEnabled(false);
        if (!prepared.error.isEmpty()) {
            message_->setText(prepared.error);
            return;
        }
        result_ = std::move(prepared.result);
        if (!current()) {
            result_.reset();
            message_->setText("문서가 변경되어 계산 결과를 폐기했습니다.");
            return;
        }
        showResult();
        xlsx_->setEnabled(true);
        pdf_->setEnabled(true);
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot), options, control = control_] {
        PreparedUsage result;
        try {
            result.result = std::make_shared<UsageResult>(buildUsage(snapshot, options, control));
        } catch (const CalculationCancelled &) {
            result.error = "시간축 계산을 취소했습니다.";
        } catch (const std::exception &e) {
            result.error = qErrorText(e);
        }
        return result;
    }));
}
void UsageDialog::showResult() {
    model_->setUsage(result_, static_cast<UsageHierarchy>(hierarchy_->currentIndex()));
    model_->setMetric(metric_->currentIndex());
    const auto width = tree_->viewport()->width();
    tree_->setColumnWidth(0, std::clamp(width / 4, 180, 270));
    tree_->setColumnWidth(1, std::clamp(width / 6, 160, 210));
    tree_->setColumnWidth(2, std::clamp(width / 5, 180, 230));
    for (int c = 3; c < model_->columnCount(); ++c)
        tree_->setColumnWidth(c, std::clamp(width / 7, 150, 220));
    tree_->expandToDepth(1);
    message_->setText(
        qs(result_->name) + " · rev " + QString::number(result_->revision) + " · " +
        (sourceUnsaved_ ? "미저장 메모리" : "저장된 원본") + " · 상태일 " + qs(result_->statusAt.value_or("미설정")) +
        "\n" + qs(result_->options.from) + " ~ " + qs(result_->options.to) + " · " +
        QString::number(result_->buckets.size()) + "개 기간 · 작업 " + QString::number(result_->taskOrder.size()) +
        "개 · 현재 미배정 작업 " + QString::number(result_->unassignedTasks) + "개 · 통화 " + qs(result_->currency) +
        "\n계획/기준선/잔여 산정: " + (result_->available[0] ? "가능" : "미정") + " / " +
        (result_->available[1] ? "가능" : "미정") + " / " + (result_->available[2] ? "가능" : "미정") +
        "\n기준선: " + qs(result_->baselineDescription.empty() ? "미선택" : result_->baselineDescription));
    QStringList issues;
    for (const auto &d : result_->diagnostics)
        if (issues.size() < 5)
            issues << qs(diagnosticMessage(d));
    diagnostics_->setText(issues.join('\n'));
}
void UsageDialog::exportFile(bool pdf) {
    if (exporting_ || !current())
        return;
    const auto snapshot = result_;
    auto path = QFileDialog::getSaveFileName(this, "Usage 내보내기 · 새 파일명", QString{},
                                             pdf ? "PDF (*.pdf)" : "Excel (*.xlsx)", nullptr,
                                             QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += pdf ? ".pdf" : ".xlsx";
    if (!authorizeSandboxFile(this, path) || !current() || snapshot != result_)
        return;
    exporting_ = true;
    for (auto *w : std::initializer_list<QWidget *>{scope_, baseline_, period_, hierarchy_, metric_, from_, to_,
                                                    refresh_, xlsx_, pdf_})
        w->setEnabled(false);
    PdfOptions pdfOptions;
    pdfOptions.font = QApplication::font();
    const auto reportId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto generated = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
    const auto hierarchy = static_cast<UsageHierarchy>(hierarchy_->currentIndex());
    const auto unsaved = sourceUnsaved_;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        const auto error = watcher->result();
        watcher->deleteLater();
        exporting_ = false;
        for (auto *w :
             std::initializer_list<QWidget *>{scope_, baseline_, period_, hierarchy_, metric_, from_, to_, refresh_})
            w->setEnabled(true);
        xlsx_->setEnabled(current());
        pdf_->setEnabled(current());
        message_->setText(error.isEmpty() ? "Usage 내보내기 완료: " + path : error);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, path, pdf, pdfOptions, hierarchy, reportId, generated, unsaved] {
        try {
            const auto report = usageReport(*snapshot, hierarchy, reportId, generated, unsaved);
            if (pdf)
                exportPdf(report, path, ReportFamily::All, pdfOptions);
            else
                exportXlsx(report, path);
            return QString{};
        } catch (const std::exception &e) {
            return qErrorText(e);
        }
    }));
}
void UsageDialog::reject() {
    if (!exporting_) {
        if (control_)
            control_->cancel();
        QDialog::reject();
    }
}
void UsageDialog::closeEvent(QCloseEvent *e) {
    if (exporting_)
        e->ignore();
    else {
        if (control_)
            control_->cancel();
        QDialog::closeEvent(e);
    }
}
} // namespace ppm
