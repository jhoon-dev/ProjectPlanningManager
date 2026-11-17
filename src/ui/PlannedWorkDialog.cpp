#include "ui/PlannedWorkDialog.h"
#include "ui/EditorUi.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
namespace jobs {
namespace {
struct Outcome {
    std::optional<PlannedWorkResult> value;
    QString error;
};
} // namespace
PlannedWorkDialog::PlannedWorkDialog(Document &doc, std::shared_ptr<const Project> source, std::string taskId,
                                     std::string resourceId, QWidget *parent)
    : QDialog(parent), doc_(doc), source_(std::move(source)), taskId_(std::move(taskId)),
      resourceId_(std::move(resourceId)), session_(doc.sessionGeneration()) {
    setObjectName("plannedWorkDialog");
    setWindowTitle("계획 공수로 기간별 배정률 생성");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("고정 기간 작업 · 현재 논리 계획의 작업·자원 달력, 휴일과 중단을 반영합니다.\n"
                            "목표 공수는 시간(h), 소수 4자리까지 입력합니다. 1/100 자원분 단위로 반올림합니다. "
                            "빈칸은 오류이며 0은 수요 없음입니다.\n"
                            "변환 결과는 배정률 초안입니다. 작업 날짜·달력이 바뀌면 공수도 달라질 수 있습니다. "
                            "단위 변경 시 입력은 초기화됩니다.",
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    period_ = new QComboBox(this);
    period_->setObjectName("plannedWorkPeriod");
    period_->addItems({"일", "주 (월~일)", "월"});
    layout->addWidget(period_);
    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName("plannedWorkTable");
    table_->setHorizontalHeaderLabels(
        {"계획 시작", "계획 끝 (제외)", "자원 가능 근무분", "현재 공수 (h)", "목표 공수 (h)"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    message_ = new QLabel(this);
    message_->setObjectName("plannedWorkMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    reload_ = buttons->addButton("현재 공수 불러오기", QDialogButtonBox::ActionRole);
    reload_->setObjectName("reloadPlannedWork");
    convert_ = buttons->addButton("배정률 변환·검산", QDialogButtonBox::ActionRole);
    convert_->setObjectName("convertPlannedWork");
    use_ = buttons->addButton("배정률 초안에 반영", QDialogButtonBox::ActionRole);
    use_->setObjectName("usePlannedWork");
    cancel_ = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
    cancel_->setObjectName("cancelPlannedWork");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &PlannedWorkDialog::reject);
    connect(period_, &QComboBox::currentIndexChanged, this, [this] { run(false); });
    connect(table_, &QTableWidget::itemChanged, this, [this] {
        ++generation_;
        result_.reset();
        message_->setText("목표 공수를 입력한 뒤 변환·검산하세요. 아직 문서에는 적용되지 않았습니다.");
        controls();
    });
    connect(convert_, &QPushButton::clicked, this, [this] { run(true); });
    connect(reload_, &QPushButton::clicked, this, [this] { run(false); });
    connect(use_, &QPushButton::clicked, this, [this] {
        if (current() && !busy_ && result_)
            QDialog::accept();
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    listener_ = doc_.subscribe([this](Document::Event) {
        if (current())
            return;
        ++generation_;
        if (control_)
            control_->cancel();
        result_.reset();
        message_->setText("문서·권한이 변경되었습니다. 창을 다시 열어 최신 값으로 편집하세요.");
        controls();
    });
    resize(1050, 630);
    prepareEditorDialog(*this);
    run(false);
}
PlannedWorkDialog::~PlannedWorkDialog() {
    if (control_)
        control_->cancel();
    doc_.unsubscribe(listener_);
}
void PlannedWorkDialog::reject() {
    ++generation_;
    if (control_)
        control_->cancel();
    result_.reset();
    QDialog::reject();
}
bool PlannedWorkDialog::current() const {
    return source_ && doc_.project() && !doc_.readOnly() && doc_.sessionGeneration() == session_ &&
           doc_.project()->documentId == source_->documentId && doc_.project()->revision == source_->revision;
}
void PlannedWorkDialog::controls() {
    const bool editable = current() && !busy_;
    period_->setEnabled(editable);
    reload_->setEnabled(editable);
    convert_->setEnabled(editable && basis_.has_value());
    use_->setEnabled(editable && result_.has_value());
    cancel_->setEnabled(busy_);
    table_->setEditTriggers(editable && basis_ ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                               : QAbstractItemView::NoEditTriggers);
}
void PlannedWorkDialog::run(bool convert) {
    if (!current() || busy_) {
        controls();
        return;
    }
    std::vector<PlannedWorkAmount> amounts;
    try {
        if (convert) {
            if (!basis_)
                return;
            for (int i = 0; i < table_->rowCount(); ++i) {
                auto a = basis_->buckets.at(std::size_t(i)).amount;
                try {
                    a.workUnitMinutes = parsePlannedWorkHours(table_->item(i, 4)->text().trimmed().toStdString());
                } catch (const std::exception &) {
                    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                                   {"행 " + std::to_string(i + 1) + ": 시간(h)을 소수 4자리 이하로 입력하세요."});
                }
                amounts.push_back(a);
            }
        }
    } catch (const std::exception &e) {
        result_.reset();
        showEditorError(*message_, e);
        controls();
        return;
    }
    const auto generation = ++generation_;
    result_.reset();
    if (!convert)
        basis_.reset();
    busy_ = true;
    message_->setText("달력 반영 계획 공수 계산 중…");
    controls();
    control_ = std::make_shared<CalculationControl>();
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, generation, convert] {
        auto out = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        if (generation != generation_ || !current()) {
            controls();
            return;
        }
        if (control_->cancelRequested.load(std::memory_order_relaxed)) {
            message_->setText("계산을 취소했습니다. 현재 공수를 다시 불러오거나 변환하세요.");
            controls();
            return;
        }
        if (!out.value) {
            message_->setText(out.error);
            controls();
            return;
        }
        if (convert) {
            result_ = std::move(out.value);
            std::int64_t total = 0;
            for (const auto &b : result_->buckets)
                total += b.amount.workUnitMinutes;
            message_->setText(
                QString("검산 일치 · 목표 공수 합계 %1 h · %2개 배정률 구간\n"
                        "최대 가용률을 초과할 수 있습니다. 초안 반영 후 부하를 확인하고 ‘문서에 적용’하세요.")
                    .arg(QString::fromStdString(plannedWorkHours(total)))
                    .arg(result_->periods.size()));
        } else {
            basis_ = std::move(out.value);
            const QSignalBlocker block(table_);
            table_->setRowCount(int(basis_->buckets.size()));
            for (int i = 0; i < table_->rowCount(); ++i) {
                const auto &b = basis_->buckets[std::size_t(i)];
                const auto h = QString::fromStdString(plannedWorkHours(b.amount.workUnitMinutes));
                const QStringList cells{QString::fromStdString(b.start), QString::fromStdString(b.finish),
                                        QString::number(b.availableMinutes), h, h};
                for (int c = 0; c < cells.size(); ++c) {
                    auto *item = new QTableWidgetItem(cells[c]);
                    if (c != 4)
                        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                    table_->setItem(i, c, item);
                }
            }
            message_->setText("목표 공수는 현재 공수로 초기화했습니다. 변경 없는 구간은 기존 곡선을 보존합니다.");
        }
        controls();
    });
    watcher->setFuture(
        QtConcurrent::run([source = source_, taskId = taskId_, resourceId = resourceId_,
                           period = PlannedWorkPeriod(period_->currentIndex()), amounts, convert, control = control_] {
            Outcome out;
            try {
                out.value = convert ? convertPlannedWork(*source, taskId, resourceId, amounts, control)
                                    : describePlannedWork(*source, taskId, resourceId, period, control);
            } catch (const CalculationCancelled &) {
                out.error = "계산을 취소했습니다. 입력을 확인하고 다시 변환하세요.";
            } catch (const std::exception &e) {
                out.error = qErrorText(e);
            }
            return out;
        }));
}
} // namespace jobs
