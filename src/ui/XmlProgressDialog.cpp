#include "ui/XmlProgressDialog.h"
#include "ui/ExchangeOperation.h"
#include "ui/EditorUi.h"
#include <QDateTime>
#include <QComboBox>
#include <QStandardItemModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace ppm {
std::optional<ProjectXmlExchange> reviewXmlProgress(QWidget *parent, Document &doc, const ProjectXmlExchange &input) {
    const auto session = doc.sessionGeneration();
    const auto revision = doc.project() ? doc.project()->revision : 0;
    QDialog dialog(parent);
    dialog.setObjectName("xmlProgressDialog");
    dialog.setWindowTitle("XML 작업 진척 · 새 사본 채택 검토");
    auto *layout = new QVBoxLayout(&dialog);
    auto *note =
        new QLabel("선택한 작업의 실제 시작·완료·실제/잔여 기간을 새 사본 원장에 기록합니다. 현재 문서의 실적과 "
                   "병합하지 않습니다. 실제 공수·원가·진척률은 채택하지 않으며 불완전한 값은 추정하지 않습니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableWidget(int(input.progressCandidates.size()), 8);
    table->setObjectName("xmlProgressRows");
    table->setHorizontalHeaderLabels(
        {"선택", "원본 UID → 새 작업 ID", "작업", "실제 시작", "실제 완료", "실제 분", "잔여 분", "검토"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumHeight(180);
    for (std::size_t i = 0; i < input.progressCandidates.size(); ++i) {
        const auto &r = input.progressCandidates[i];
        auto *select = new QTableWidgetItem;
        select->setFlags(r.present && r.issue.empty() ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        table->setItem(int(i), 0, select);
        const QStringList cells = {QString::fromStdString(r.xmlUid + " → " + r.taskId),
                                   QString::fromStdString(r.name),
                                   r.actualStart ? QString::fromStdString(*r.actualStart) : "미설정",
                                   r.actualFinish ? QString::fromStdString(*r.actualFinish) : "미설정",
                                   r.actualMinutes ? QString::number(*r.actualMinutes) : "미설정",
                                   r.remainingMinutes ? QString::number(*r.remainingMinutes) : "미설정",
                                   r.issue.empty() ? "상태일 기준 검증 필요" : QString::fromStdString(r.issue)};
        for (int j = 0; j < cells.size(); ++j) {
            auto *cell = new QTableWidgetItem(cells[j]);
            cell->setToolTip(cells[j]);
            table->setItem(int(i), j + 1, cell);
        }
    }
    layout->addWidget(table, 1);
    auto *form = new QFormLayout;
    auto *status = new QLineEdit(input.progressStatusAt ? QString::fromStdString(*input.progressStatusAt) : QString{});
    status->setObjectName("xmlProgressStatus");
    status->setPlaceholderText("YYYY-MM-DD HH:mm");
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlProgressReporter");
    reporter->setMaxLength(128);
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlProgressReason");
    reason->setMaxLength(512);
    form->addRow("채택 상태일", status);
    form->addRow("작성자", reporter);
    form->addRow("채택 사유", reason);
    layout->addLayout(form);
    if (!input.progressStatusIssue.empty()) {
        auto *issue = new QLabel(QString::fromStdString(input.progressStatusIssue));
        issue->setWordWrap(true);
        layout->addWidget(issue);
    }
    auto *error = new QLabel;
    error->setObjectName("xmlProgressError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 진척 검증 · 새 사본에 채택");
    layout->addWidget(buttons);
    std::optional<ProjectXmlExchange> output;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc.sessionGeneration() != session || (doc.project() ? doc.project()->revision : 0) != revision)
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
            XmlProgressOptions options;
            options.statusAt = status->text().trimmed().toStdString();
            options.reporter = reporter->text().trimmed().toStdString();
            options.reason = reason->text().trimmed().toStdString();
            options.recordedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
            options.confirmed = true;
            for (std::size_t i = 0; i < input.progressCandidates.size(); ++i)
                if (table->item(int(i), 0)->checkState() == Qt::Checked)
                    options.taskIds.push_back(input.progressCandidates[i].taskId);
            auto prepared =
                prepareExchange<ProjectXmlExchange>(&dialog, doc, "선택 진척·원본 대조·원장 검증 중…",
                                                    [input, options] { return adoptXmlProgress(input, options); });
            if (!prepared)
                return;
            output = std::move(*prepared);
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(950, 640);
    prepareEditorDialog(dialog);
    dialog.exec();
    return output;
}
bool reviewXmlProgressMerge(QWidget *parent, Document &doc, const ProjectXmlExchange &input) {
    if (!doc.project() || doc.readOnly())
        throw AppError(ErrorCode::DOCUMENT_NOT_EDITABLE, {});
    if (!doc.project()->statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED, {});
    const auto snapshot = doc.snapshot();
    const auto session = doc.sessionGeneration();
    QDialog dialog(parent);
    dialog.setObjectName("xmlProgressMergeDialog");
    dialog.setWindowTitle("XML 작업 진척 · 현재 문서 병합");
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(QString::fromStdString(
        "선택하지 않은 행은 현재 보고를 유지합니다. 대상 작업을 직접 연결하고 현재/수신 값을 비교하세요. "
        "선택한 행은 기존 원장을 보존하는 정정 보고로 추가됩니다. 같은 출처·UID·상태일은 다시 추가하지 않습니다.\n"
        "현재 상태일: " +
        *snapshot->statusAt + " · XML 상태일: " + input.progressStatusAt.value_or("미설정") +
        "\n현재 상태일에 기록합니다. 공수 기반 작업은 명시 RemainingWork도 필요합니다(100u=1자원·분). "
        "예측 잔여 기간은 현재 배정·달력·곡선과 잔여 공수로 다시 계산하며 수신 잔여 분과 다를 수 있습니다. "
        "원래 계획과 실제 공수·원가는 변경하지 않습니다. 원본 XML 파일을 함께 보관하세요."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableWidget(int(input.progressCandidates.size()), 6);
    table->setObjectName("xmlProgressMergeRows");
    table->setHorizontalHeaderLabels(
        {"반영", "XML UID · 작업", "대상 작업 ID · 이름", "현재 보고", "수신 보고", "검토"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    auto observation = [](const auto &r) {
        return QString::fromStdString(r.actualStart.value_or("미설정") + " → " + r.actualFinish.value_or("미설정")) +
               QString("\n실제 %1분 · 잔여 %2분").arg(r.actualDurationMinutes).arg(r.remainingMinutes);
    };
    auto *targets = new QStandardItemModel(&dialog);
    auto *blank = new QStandardItem("대상 선택…");
    blank->setData(QString{}, Qt::UserRole);
    targets->appendRow(blank);
    for (const auto &t : snapshot->tasks) {
        auto *item = new QStandardItem(QString::fromStdString(t.id + " · " + t.name));
        item->setData(QString::fromStdString(t.id), Qt::UserRole);
        targets->appendRow(item);
    }
    for (std::size_t i = 0; i < input.progressCandidates.size(); ++i) {
        const auto &r = input.progressCandidates[i];
        auto *select = new QTableWidgetItem;
        select->setFlags(r.present && r.issue.empty() ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        table->setItem(int(i), 0, select);
        table->setItem(int(i), 1, new QTableWidgetItem(QString::fromStdString(r.xmlUid + " · " + r.name)));
        auto *target = new QComboBox;
        target->setModel(targets);
        target->setMaximumWidth(360);
        table->setCellWidget(int(i), 2, target);
        table->setItem(int(i), 3, new QTableWidgetItem("대상 미선택"));
        table->setItem(
            int(i), 4,
            new QTableWidgetItem(
                QString::fromStdString(r.actualStart.value_or("미설정") + " → " + r.actualFinish.value_or("미설정")) +
                "\n실제 " + (r.actualMinutes ? QString::number(*r.actualMinutes) : "미설정") + "분 · 잔여 " +
                (r.remainingMinutes ? QString::number(*r.remainingMinutes) : "미설정") + "분\n잔여 공수 " +
                (r.remainingWorkUnitMinutes ? QString::number(*r.remainingWorkUnitMinutes) + "u" : "미보고") +
                (r.remainingWorkIssue.empty() ? QString{} : QString::fromStdString(" · " + r.remainingWorkIssue))));
        table->setItem(int(i), 5,
                       new QTableWidgetItem(QString::fromStdString(r.issue.empty() ? "선택 시 보고 추가" : r.issue)));
        for (const auto column : {1, 4, 5})
            table->item(int(i), column)->setToolTip(table->item(int(i), column)->text());
        QObject::connect(target, &QComboBox::currentIndexChanged, &dialog, [&, i, target, observation] {
            target->setToolTip(target->currentText());
            const auto id = target->currentData().toString().toStdString();
            const auto *old = progressAsOf(*snapshot, id);
            auto text = old ? observation(*old) + QString::fromStdString("\n" + old->statusAt + " · " + old->reporter)
                            : QString("보고 없음");
            if (old)
                text +=
                    "\n잔여 공수 " +
                    (old->remainingWorkUnitMinutes ? QString::number(*old->remainingWorkUnitMinutes) + "u" : "미보고");
            const auto task =
                std::find_if(snapshot->tasks.begin(), snapshot->tasks.end(), [&](const auto &t) { return t.id == id; });
            const bool work = task != snapshot->tasks.end() && isEffort(*snapshot) && usesWork(*task);
            if (work)
                text += "\n공수 기준 · 현재 계획 " + QString::number(task->workUnitMinutes.value_or(0)) + "u";
            const auto &source = input.progressCandidates[i];
            QString review = QString::fromStdString(source.issue.empty() ? "선택 시 보고 추가" : source.issue);
            if (work && source.issue.empty()) {
                const bool zero = !source.actualStart && source.actualMinutes == 0 && source.remainingMinutes == 0 &&
                                  task->workUnitMinutes == 0;
                if (!source.remainingWorkIssue.empty())
                    review = QString::fromStdString(source.remainingWorkIssue);
                else if (!source.actualFinish &&
                         (!source.remainingWorkUnitMinutes || (*source.remainingWorkUnitMinutes == 0 && !zero)))
                    review = "병합 불가 · 명시 잔여 공수 필요";
                else if (source.actualFinish && source.remainingWorkUnitMinutes.value_or(0) != 0)
                    review = "병합 불가 · 완료와 양수 잔여 공수 모순";
                else
                    review = "공수 기준 · 기간 보고 보존/잔여 공수로 예측";
            }
            table->item(int(i), 5)->setText(review);
            table->item(int(i), 5)->setToolTip(review);
            for (auto it = snapshot->progress.rbegin(); it != snapshot->progress.rend(); ++it)
                if (it->taskId == id) {
                    if (it->statusAt > *snapshot->statusAt)
                        text += QString::fromStdString("\n미래 보고 보존: " + it->statusAt);
                    break;
                }
            table->item(int(i), 3)->setText(text);
            table->item(int(i), 3)->setToolTip(text);
            table->resizeRowToContents(int(i));
        });
    }
    table->resizeRowsToContents();
    layout->addWidget(table, 1);
    auto *form = new QFormLayout;
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlMergeReporter");
    reporter->setMaxLength(128);
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlMergeReason");
    reason->setMaxLength(512);
    form->addRow("작성자", reporter);
    form->addRow("병합 사유", reason);
    layout->addLayout(form);
    auto *error = new QLabel;
    error->setObjectName("xmlMergeError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 행 검증 · 정정 보고 추가");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc.sessionGeneration() != session || doc.project()->revision != snapshot->revision)
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
            XmlProgressOptions options;
            options.statusAt = *snapshot->statusAt;
            options.reporter = reporter->text().trimmed().toStdString();
            options.reason = reason->text().trimmed().toStdString();
            options.recordedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
            options.confirmed = true;
            std::vector<XmlProgressMapping> mappings;
            for (std::size_t i = 0; i < input.progressCandidates.size(); ++i)
                if (table->item(int(i), 0)->checkState() == Qt::Checked)
                    mappings.push_back(
                        {input.progressCandidates[i].xmlUid, qobject_cast<QComboBox *>(table->cellWidget(int(i), 2))
                                                                 ->currentData()
                                                                 .toString()
                                                                 .toStdString()});
            auto rows = prepareExchange<std::vector<ProgressEntry>>(
                &dialog, doc, "XML 출처·연결·진척 대조 중…", [snapshot, input, options, mappings] {
                    return prepareXmlProgressMerge(*snapshot, input, options, mappings);
                });
            if (!rows)
                return;
            auto request = doc.makeEditRequest(ProgressMergeEdit{std::move(*rows)});
            auto prepared = prepareExchange<std::shared_ptr<Document::PreparedEdit>>(
                &dialog, doc, "원장·마감 기간 검증 중…",
                [request = std::move(request)]() mutable { return Document::prepareEdit(std::move(request), {}); });
            if (!prepared)
                return;
            const auto result = doc.applyPreparedEdit(**prepared);
            if (result == EditState::Applied || result == EditState::NoChange)
                dialog.accept();
            else if ((*prepared)->failure())
                throw AppError(*(*prepared)->failure());
            else
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(1100, 680);
    prepareEditorDialog(dialog);
    return dialog.exec() == QDialog::Accepted;
}
} // namespace ppm
