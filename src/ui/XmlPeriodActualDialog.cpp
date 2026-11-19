#include "ui/XmlPeriodActualDialog.h"
#include "ui/ExchangeOperation.h"
#include "ui/EditorUi.h"
#include "ui/XmlActualAllocationDialog.h"
#include "domain/PeriodActuals.h"
#include "domain/Classification.h"
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QMessageBox>
#include <algorithm>
namespace ppm {
namespace {
QString work(std::optional<std::int64_t> n) {
    return n ? QString::number(*n) + "u" : "미보고";
}
QString money(std::optional<std::int64_t> n, const std::string &currency) {
    return n ? QString::fromStdString(formatFieldCost(*n, currency) + " " + currency) : "미보고";
}
QString parts(std::size_t count) {
    return count > 1 ? QString(" · %1구간").arg(count) : QString{};
}
} // namespace
bool reviewXmlPeriodActuals(QWidget *parent, Document &doc, const ProjectXmlExchange &source) {
    if (!doc.project() || doc.readOnly())
        throw AppError(ErrorCode::DOCUMENT_NOT_EDITABLE);
    if (!doc.project()->statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    const auto snapshot = doc.snapshot();
    const auto session = doc.sessionGeneration();
    const auto current = periodActualsAsOf(*snapshot, *snapshot->statusAt);
    QDialog dialog(parent);
    dialog.setObjectName("xmlPeriodActualDialog");
    dialog.setWindowTitle("XML 배정·기간별 실제 공수·원가 채택");
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(QString::fromStdString(
        "현재 상태일: " + *snapshot->statusAt + " · XML 상태일: " + source.progressStatusAt.value_or("미설정") +
        "\n일별 실제 공수(Type2)·원가(Type6)를 선택하고 native 배정에 직접 연결하세요. 100u=1자원·분입니다. "
        "수신 없는 항목은 기존 관측값을 보존합니다. XML 원가를 선택한 작업만 XML 누계 AC도 반영하고, 공수만 선택하면 "
        "기존 누계(미보고 포함)를 유지합니다. 누계와 날짜별 배분액은 더하지 않습니다. "
        "같은 날짜의 겹치지 않는 구간은 종류별로 합산하며 구간 수를 표시합니다. "
        "현재 상태일은 유지하고, 중복 출처는 다시 기록하지 않습니다. 원본 XML을 함께 보관하세요."));
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    layout->addWidget(note);
    auto *allocate = new QPushButton("다일 구간 비례 배분…");
    allocate->setObjectName("xmlPeriodAllocate");
    allocate->setEnabled(std::any_of(source.actualIntervals.begin(), source.actualIntervals.end(),
                                     [](const auto &r) { return r.multipleDays; }));
    layout->addWidget(allocate);
    QObject::connect(allocate, &QPushButton::clicked, &dialog, [&] {
        try {
            if (reviewXmlActualAllocation(&dialog, doc, source))
                dialog.accept();
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, "다일 실적 배분", QString::fromStdString(e.what()));
        }
    });
    using Pair = std::pair<std::string, std::string>;
    std::vector<Pair> targets;
    std::map<std::string, std::string> taskNames, resourceNames;
    for (const auto &t : snapshot->tasks)
        taskNames[t.id] = t.name;
    for (const auto &r : snapshot->resources)
        if (r.type == "labor" || r.type == "equipment")
            resourceNames[r.id] = r.name;
    auto *model = new QStandardItemModel(&dialog);
    auto *blank = new QStandardItem("대상 배정 선택…");
    blank->setData(-1, Qt::UserRole);
    model->appendRow(blank);
    for (const auto &a : snapshot->assignments)
        if (resourceNames.contains(a.resourceId)) {
            auto *item = new QStandardItem(QString::fromStdString(a.taskId + " / " + a.resourceId + " · " +
                                                                  taskNames.at(a.taskId) + " / " +
                                                                  resourceNames.at(a.resourceId)));
            item->setData(int(targets.size()), Qt::UserRole);
            model->appendRow(item);
            targets.emplace_back(a.taskId, a.resourceId);
        }
    auto *table = new QTableWidget(int(source.periodCandidates.size()), 8);
    table->setObjectName("xmlPeriodRows");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalHeaderLabels({"반영", "XML 배정 / 작업 / 자원 UID", "실제 날짜·범위", "native 배정",
                                      "현재 날짜별 관측", "수신 공수·원가", "작업 누계 AC", "검토"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    for (std::size_t i = 0; i < source.periodCandidates.size(); ++i) {
        const auto &r = source.periodCandidates[i];
        const bool eligible = r.issue.empty() && (r.workUnitMinutes || r.actualMinor) &&
                              (!r.actualMinor || (r.taskActualMinor && r.totalIssue.empty() &&
                                                  source.actualCurrency == snapshot->currency));
        auto *select = new QTableWidgetItem;
        select->setFlags(eligible ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        table->setItem(int(i), 0, select);
        table->setItem(int(i), 1,
                       new QTableWidgetItem(QString::fromStdString(r.assignmentUid + " / " + r.taskUid + " / " +
                                                                   r.resourceUid + "\n" + r.name)));
        table->setItem(int(i), 2,
                       new QTableWidgetItem(QString::fromStdString(r.date + (r.dateBucket ? " · 일별 버킷 포함" : "") +
                                                                   "\n" + r.start + " → " + r.finish)));
        auto *combo = new QComboBox;
        combo->setModel(model);
        combo->setMaximumWidth(340);
        table->setCellWidget(int(i), 3, combo);
        table->setItem(int(i), 4, new QTableWidgetItem("대상 미선택"));
        table->setItem(int(i), 5,
                       new QTableWidgetItem("공수 " + work(r.workUnitMinutes) + parts(r.workParts) + "\n원가 " +
                                            money(r.actualMinor, source.actualCurrency) + parts(r.costParts)));
        table->setItem(int(i), 6, new QTableWidgetItem("XML " + money(r.taskActualMinor, source.actualCurrency)));
        const auto issue = !r.issue.empty() ? r.issue
                           : r.actualMinor && source.actualCurrency != snapshot->currency
                               ? "통화 불일치 · 자동 환산하지 않음"
                           : r.actualMinor && !r.totalIssue.empty() ? r.totalIssue
                                                                    : "선택 시 정정 보고 추가";
        table->setItem(int(i), 7, new QTableWidgetItem(QString::fromStdString(issue)));
        for (int column : {1, 2, 5, 6, 7})
            table->item(int(i), column)->setToolTip(table->item(int(i), column)->text());
        QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, [&, i, combo] {
            const auto index = combo->currentData().toInt();
            if (index < 0) {
                table->item(int(i), 4)->setText("대상 미선택");
                return;
            }
            const auto &[task, resource] = targets.at(std::size_t(index));
            const auto key = PeriodActualKey{task, resource, source.periodCandidates[i].date};
            const auto *old = current.contains(key) ? current.at(key) : nullptr;
            table->item(int(i), 4)->setText(old ? "공수 " + work(old->workUnitMinutes) + "\n원가 " +
                                                      money(old->actualMinor, snapshot->currency)
                                                : "관측 보고 없음");
            const auto *total = costAsOf(*snapshot, task, *snapshot->statusAt);
            table->item(int(i), 6)->setText(
                "현재 " + money(total ? total->actualMinor : std::nullopt, snapshot->currency) + "\nXML " +
                money(source.periodCandidates[i].taskActualMinor, source.actualCurrency));
            combo->setToolTip(combo->currentText());
            for (int column : {4, 6})
                table->item(int(i), column)->setToolTip(table->item(int(i), column)->text());
            table->resizeRowToContents(int(i));
        });
    }
    table->resizeRowsToContents();
    layout->addWidget(table, 1);
    auto *form = new QFormLayout;
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlPeriodReporter");
    reporter->setMaxLength(128);
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlPeriodReason");
    reason->setMaxLength(512);
    form->addRow("작성자", reporter);
    form->addRow("채택 사유", reason);
    layout->addLayout(form);
    auto *error = new QLabel;
    error->setObjectName("xmlPeriodError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 실적·누계 대사 · 원장에 채택");
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
            std::vector<XmlPeriodMapping> mappings;
            for (std::size_t i = 0; i < source.periodCandidates.size(); ++i)
                if (table->item(int(i), 0)->checkState() == Qt::Checked) {
                    const auto index = qobject_cast<QComboBox *>(table->cellWidget(int(i), 3))->currentData().toInt();
                    const Pair target = index < 0 ? Pair{} : targets.at(std::size_t(index));
                    mappings.push_back({source.periodCandidates[i].key, target.first, target.second});
                }
            auto batches = prepareExchange<std::vector<PeriodActualSubmission>>(
                &dialog, doc, "XML 원본·날짜·배정·단위 대조 중…", [snapshot, source, options, mappings] {
                    return prepareXmlPeriodMerge(*snapshot, source, options, mappings);
                });
            if (!batches)
                return;
            auto request = doc.makeEditRequest(PeriodActualBatchEdit{std::move(*batches), *snapshot->statusAt});
            auto prepared = prepareExchange<std::shared_ptr<Document::PreparedEdit>>(
                &dialog, doc, "누계·배분 대사·마감 기간 검증 중…",
                [request = std::move(request)]() mutable { return Document::prepareEdit(std::move(request), {}); });
            if (!prepared)
                return;
            const auto state = doc.applyPreparedEdit(**prepared);
            if (state == EditState::Applied || state == EditState::NoChange)
                dialog.accept();
            else if ((*prepared)->failure())
                throw AppError(*(*prepared)->failure());
            else
                throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(1180, 700);
    prepareEditorDialog(dialog);
    return dialog.exec() == QDialog::Accepted;
}
} // namespace ppm
