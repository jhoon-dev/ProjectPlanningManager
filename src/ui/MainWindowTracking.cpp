#include "formatting/QtDiagnosticText.h"
#include "ui/MainWindow.h"
#include "ui/EditorUi.h"
#include "ui/TaskInput.h"
#include "ui/ComparisonDialog.h"
#include "ui/WeeklyUpdateDialog.h"
#include <QAction>
#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <cmath>
namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}
std::string utcNow() {
    return str(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}
std::optional<std::string> optional(const QString &s) {
    return s.trimmed().isEmpty() ? std::nullopt : std::optional<std::string>{str(s.trimmed())};
}
} // namespace
void MainWindow::refreshTracking() {
    const auto &p = document_.project();
    const bool writable = p.has_value() && !document_.readOnly();
    baselineSave_->setEnabled(writable && !baselinePreparing_);
    statusEdit_->setEnabled(writable);
    progressEdit_->setEnabled(writable && p->statusAt.has_value() && !selectedTask_.empty());
    comparison_->setEnabled(p.has_value());
    weeklyUpdate_->setEnabled(p.has_value());
    const QSignalBlocker block(baselineChoice_);
    baselineChoice_->clear();
    baselineChoice_->addItem("간트: 기준선 없음", QString{});
    if (p)
        for (const auto &b : p->baselines) {
            baselineChoice_->addItem("v" + QString::number(b.version) + " · " + qs(b.name), qs(b.id));
            baselineChoice_->setItemData(baselineChoice_->count() - 1,
                                         "승인자: " + qs(b.approvedBy) + "\n사유: " + qs(b.reason), Qt::ToolTipRole);
        }
    const auto i = baselineChoice_->findData(qs(selectedBaseline_));
    baselineChoice_->setCurrentIndex(std::max(0, i));
    if (i < 0)
        selectedBaseline_.clear();
    baselineChoice_->setEnabled(p.has_value());
}
void MainWindow::saveBaseline() {
    if (!document_.project() || document_.readOnly() || baselinePreparing_)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("기준선 저장");
    dialog.setObjectName("baselineDialog");
    dialog.resize(500, 270);
    auto *layout = new QFormLayout(&dialog);
    layout->addRow(new QLabel("현재 계획의 입력·달력·WBS·계산 결과를 새 버전으로 고정합니다.\n기존 기준선은 유지되며 "
                              "실적은 별도 이력으로 관리합니다.",
                              &dialog));
    auto *name = new QLineEdit(&dialog);
    name->setObjectName("baselineName");
    auto *author = new QLineEdit(&dialog);
    author->setObjectName("baselineApprover");
    auto *reason = new QLineEdit(&dialog);
    reason->setObjectName("baselineReason");
    layout->addRow("기준선 이름", name);
    layout->addRow("승인자", author);
    layout->addRow("승인/변경 사유", reason);
    auto *error = new QLabel(&dialog);
    error->setTextFormat(Qt::PlainText);
    layout->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty() || author->text().trimmed().isEmpty() ||
            reason->text().trimmed().isEmpty()) {
            error->setText("이름·승인자·사유를 모두 입력하세요.");
            return;
        }
        dialog.accept();
    });
    prepareEditorDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto n = str(name->text().trimmed()), a = str(author->text().trimmed()), r = str(reason->text().trimmed()),
               utc = utcNow();
    baselinePreparing_ = true;
    refreshTracking();
    try {
        submitEdit(BaselineEdit{n, a, r, utc}, "기준선 승인 준비·검증",
                   [this](EditState state, const QString &message) {
                       baselinePreparing_ = false;
                       if (state == EditState::Applied)
                           selectedBaseline_ = document_.project()->baselines.back().id;
                       refresh();
                       if (state == EditState::Failed)
                           QMessageBox::warning(this, "기준선 저장 실패", message);
                   });
    } catch (const std::exception &e) {
        baselinePreparing_ = false;
        refreshTracking();
        report(e);
    }
}
void MainWindow::editStatusAt() {
    if (!document_.project() || document_.readOnly())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("상태일 설정");
    dialog.setObjectName("statusDialog");
    auto *layout = new QFormLayout(&dialog);
    auto *date = new QDateTimeEdit(&dialog);
    date->setObjectName("statusAt");
    // UTC is only a neutral editor carrier: the stored value is a floating civil timestamp.
    date->setTimeZone(QTimeZone::UTC);
    date->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    date->setCalendarPopup(true);
    date->setDisplayFormat("yyyy-MM-dd HH:mm");
    const auto s = document_.project()->statusAt.value_or(document_.project()->startDate + " 18:00");
    date->setDateTime(QDateTime(QDate::fromString(qs(s).left(10), Qt::ISODate),
                                QTime::fromString(qs(s).mid(11), "HH:mm"), QTimeZone::UTC));
    layout->addRow("현지 상태일·시각", date);
    auto *clear = new QCheckBox("상태일 해제 · 현재 계획 모드 (보고 이력은 보존)", &dialog);
    clear->setObjectName("clearStatusAt");
    layout->addRow(clear);
    layout->addRow(new QLabel(
        "미완료 잔여 작업을 이 시각 이후로 재예측합니다.\n과거 상태일을 선택하면 그때까지의 보고만 사용합니다.",
        &dialog));
    auto *error = new QLabel(&dialog);
    error->setTextFormat(Qt::PlainText);
    layout->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            document_.setStatusAt(clear->isChecked()
                                      ? std::nullopt
                                      : std::optional<std::string>{str(date->dateTime().toString("yyyy-MM-dd HH:mm"))});
            dialog.accept();
        } catch (const std::exception &e) {
            error->setText(qErrorText(e));
        }
    });
    // Size the form before the shared editor moves its body into a scroll area.
    // The spanning checkbox must also fit when the application font is enlarged.
    const auto margins = layout->contentsMargins();
    dialog.resize(std::max(560, clear->sizeHint().width() + margins.left() + margins.right() + 32),
                  std::max(280, dialog.fontMetrics().height() * 12));
    prepareEditorDialog(dialog);
    if (dialog.exec() == QDialog::Accepted)
        refresh();
}
void MainWindow::reportProgress() {
    editTaskProgress(selectedTask_, this, false);
}
void MainWindow::showWeeklyUpdate() {
    if (!document_.project())
        return;
    WeeklyUpdateDialog dialog(document_, this);
    dialog.editStatus = [this] { editStatusAt(); };
    dialog.editProgress = [this](const std::string &id, QWidget *parent, bool next) {
        return editTaskProgress(id, parent, next);
    };
    dialog.exec();
    refresh();
}
bool MainWindow::editTaskProgress(const std::string &id, QWidget *parent, bool continueNext) {
    if (!document_.project() || document_.readOnly() || !document_.project()->statusAt || id.empty())
        return false;
    const auto &p = *document_.project();
    const Task *task = nullptr;
    for (const auto &t : p.tasks)
        if (t.id == id)
            task = &t;
    if (!task)
        return false;
    const bool workBased = usesWork(*task);
    const auto sourceId = p.documentId;
    const auto sourceRevision = p.revision;
    const auto sourceSession = document_.sessionGeneration();
    const auto taskId = task->id;
    const auto *previous = progressAsOf(p, taskId);
    QDialog dialog(parent);
    dialog.setObjectName("progressDialog");
    dialog.setProperty("progressTaskId", qs(taskId));
    dialog.setWindowTitle("작업 실적 입력 · 새 보고/정정");
    dialog.resize(960, 620);
    auto *layout = new QFormLayout(&dialog);
    auto *heading = new QLabel(qs(task->name) + " · 상태일 " + qs(*p.statusAt), &dialog);
    heading->setTextFormat(Qt::PlainText);
    layout->addRow(heading);
    auto *start = new QLineEdit(previous && previous->actualStart ? qs(*previous->actualStart) : "", &dialog);
    start->setObjectName("actualStart");
    auto *finish = new QLineEdit(previous && previous->actualFinish ? qs(*previous->actualFinish) : "", &dialog);
    finish->setObjectName("actualFinish");
    start->setPlaceholderText("YYYY-MM-DD HH:mm · 미착수면 빈칸");
    finish->setPlaceholderText("YYYY-MM-DD HH:mm · 미완료면 빈칸");
    const auto dayMinutes = taskDayMinutes(p, *task);
    auto *durationHint = new QLabel(taskDurationHelp(p, *task), &dialog);
    durationHint->setObjectName("progressDurationHelp");
    durationHint->setWordWrap(true);
    layout->addRow(durationHint);
    auto *actual = new QLineEdit(&dialog);
    actual->setObjectName("actualMinutes");
    auto *remaining = new QLineEdit(&dialog);
    remaining->setObjectName("remainingMinutes");
    actual->setText(taskDurationText(previous ? previous->actualDurationMinutes : 0, dayMinutes));
    remaining->setText(
        taskDurationText(previous ? previous->remainingMinutes : task->durationMinutes.value_or(0), dayMinutes));
    for (auto *field : {actual, remaining}) {
        field->setPlaceholderText("예: 2d / 16h / 960m · 숫자=일");
        field->setToolTip(taskDurationHelp(p, *task));
    }
    if (workBased) {
        remaining->setText("0d");
        remaining->setEnabled(false);
        remaining->setToolTip("공수 기준 작업은 명시 잔여 공수로 예측합니다. 잔여 기간을 공수로 역산하지 않습니다.");
    }
    auto *reporter = new QLineEdit(previous ? qs(previous->reporter) : "", &dialog);
    reporter->setObjectName("progressReporter");
    auto *reason = new QLineEdit(&dialog);
    reason->setObjectName("progressReason");
    layout->addRow("실제 시작", start);
    layout->addRow("실제 완료", finish);
    layout->addRow("실제 소요 기간", actual);
    layout->addRow("잔여 기간", remaining);
    auto *remainingWork =
        new QLineEdit(previous && previous->remainingWorkUnitMinutes
                          ? QString::number(double(*previous->remainingWorkUnitMinutes) / 6000.0, 'f', 4)
                          : QString{},
                      &dialog);
    remainingWork->setObjectName("remainingWorkHours");
    remainingWork->setPlaceholderText("공수 기준 작업은 필수 · 완료는 0 · 실제 공수와 구분");
    if (isEffort(p))
        layout->addRow("명시적 잔여 공수(h)", remainingWork);
    else
        remainingWork->hide();
    layout->addRow("보고자", reporter);
    layout->addRow("보고/정정 사유", reason);
    auto *progressNote = new QLabel(
        workBased
            ? "공수 기준: 잔여 기간은 사용하지 않습니다. 명시 잔여 공수로 예측하며 기간 진척률은 미산정입니다.\n완료는 "
              "실제 시작·완료와 잔여 공수 0을 함께 보고합니다. 기존 보고는 이력으로 보존됩니다."
            : "완료는 실제 시작·완료와 잔여 0을 함께 입력합니다.\n기간 진척은 실제 소요/(실제 "
              "소요+잔여)입니다. 기존 보고는 이력으로 보존됩니다.",
        &dialog);
    progressNote->setWordWrap(true);
    layout->addRow(progressNote);
    auto *history = new QTableWidget(0, 9, &dialog);
    history->setObjectName("progressHistory");
    history->setMaximumHeight(180);
    history->setHorizontalHeaderLabels({"상태일", "실제 시작", "실제 완료", "실제(d/h/m)", "잔여(d/h/m)", "보고자",
                                        "사유", "기록(UTC)", "명시 잔여 공수(h)"});
    history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (const auto &e : p.progress)
        if (e.taskId == taskId) {
            const auto row = history->rowCount();
            history->insertRow(row);
            QStringList values{qs(e.statusAt),
                               e.actualStart ? qs(*e.actualStart) : "—",
                               e.actualFinish ? qs(*e.actualFinish) : "—",
                               taskDurationText(e.actualDurationMinutes, dayMinutes),
                               taskDurationText(e.remainingMinutes, dayMinutes),
                               qs(e.reporter),
                               qs(e.reason),
                               qs(e.recordedAt),
                               e.remainingWorkUnitMinutes
                                   ? QString::number(double(*e.remainingWorkUnitMinutes) / 6000.0, 'f', 4)
                                   : "미보고"};
            for (int col = 0; col < values.size(); ++col) {
                auto *item = new QTableWidgetItem(values[col]);
                item->setToolTip("ID: " + qs(e.id) + "\n이전 보고: " + qs(e.previousEntryId) + "\n원본 문서: " +
                                 qs(e.sourceDocumentId) + "\n기간 표시는 현재 작업 달력 기준 · 실제 " +
                                 QString::number(e.actualDurationMinutes) + "분 / 잔여 " +
                                 QString::number(e.remainingMinutes) + "분");
                history->setItem(row, col, item);
            }
        }
    layout->addRow("보고 이력", history);
    auto *error = new QLabel(&dialog);
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    error->setObjectName("progressError");
    layout->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    if (continueNext) {
        buttons->button(QDialogButtonBox::Save)->setText("저장 후 다음 대상");
        buttons->button(QDialogButtonBox::Cancel)->setText("목록으로 (초안 취소)");
    }
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        ProgressEntry e;
        e.taskId = taskId;
        e.actualStart = optional(start->text());
        e.actualFinish = optional(finish->text());
        e.reporter = str(reporter->text().trimmed());
        e.reason = str(reason->text().trimmed());
        e.recordedAt = utcNow();
        try {
            const auto actualValue = taskDurationInput(actual->text(), dayMinutes);
            const auto remainingValue = taskDurationInput(remaining->text(), dayMinutes);
            if (!actualValue || !remainingValue)
                throw AppError(ErrorCode::TASKINPUT_001, {});
            e.actualDurationMinutes = *actualValue;
            e.remainingMinutes = workBased ? 0 : *remainingValue;
            if (!document_.project() || document_.project()->documentId != sourceId ||
                document_.project()->revision != sourceRevision || document_.sessionGeneration() != sourceSession)
                throw AppError(ErrorCode::MAINWINDOWTRACKING_001, {});
            if (!remainingWork->text().trimmed().isEmpty()) {
                bool ok = false;
                const auto n = remainingWork->text().trimmed().toDouble(&ok);
                if (!ok || !std::isfinite(n) || n < 0 || n > double(maxWork) / 6000.0)
                    throw AppError(ErrorCode::MAINWINDOWTRACKING_002, {});
                e.remainingWorkUnitMinutes = std::llround(n * 6000.0);
            }
            if (workBased && !e.actualFinish && !e.remainingWorkUnitMinutes)
                throw AppError(ErrorCode::MAINWINDOWTRACKING_003, {});
            if (workBased && e.actualFinish)
                e.remainingWorkUnitMinutes = 0;
            submitDialogEdit(
                dialog, error, buttons, ProgressEdit{std::move(e)}, "실적 계획 기간 계산", [=](bool pending) {
                    for (QWidget *field :
                         std::initializer_list<QWidget *>{start, finish, actual, reporter, reason, remainingWork})
                        field->setEnabled(!pending);
                    remaining->setEnabled(!pending && !workBased);
                    buttons->button(QDialogButtonBox::Save)->setEnabled(!pending);
                });
        } catch (const std::exception &ex) {
            showEditorError(*error, ex);
        }
    });
    prepareEditorDialog(dialog);
    if (dialog.exec() == QDialog::Accepted) {
        refresh();
        return true;
    }
    return false;
}
void MainWindow::showComparison() {
    if (!document_.project())
        return;
    ComparisonDialog dialog(document_, selectedBaseline_, this);
    prepareEditorDialog(dialog);
    dialog.exec();
}
} // namespace jobs
