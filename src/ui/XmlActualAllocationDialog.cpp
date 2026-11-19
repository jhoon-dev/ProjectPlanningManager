#include "ui/XmlActualAllocationDialog.h"
#include "ui/ExchangeOperation.h"
#include "ui/EditorUi.h"
#include "persistence/XmlPeriodAllocation.h"
#include "domain/Classification.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QScopeGuard>
#include <QTableWidget>
#include <QVBoxLayout>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromStdString(s);
}
QString work(const QJsonValue &v) {
    return v.isNull() ? QString("미보고") : QString::number(qint64(v.toDouble())) + "u";
}
QString money(const QJsonValue &v, const std::string &currency) {
    return v.isNull() ? QString("미보고") : qs(formatFieldCost(qint64(v.toDouble()), currency) + " " + currency);
}
} // namespace
bool reviewXmlActualAllocation(QWidget *parent, Document &doc, const ProjectXmlExchange &input) {
    if (!doc.project() || doc.readOnly())
        throw AppError(ErrorCode::DOCUMENT_NOT_EDITABLE);
    if (!doc.project()->statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    const auto snapshot = doc.snapshot();
    const auto session = doc.sessionGeneration();
    QDialog dialog(parent);
    dialog.setObjectName("xmlAllocationDialog");
    dialog.setWindowTitle("다일 실적 · 비례 배분 검토");
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        "원본 Value를 구간 전체 총량으로 확인한 자료만 일별 비례 추정합니다. 현재 상태일을 유지하며 날짜별 총량·기존 "
        "값·누계 AC를 비교하세요. 근무분 기준은 현재 작업·자원 달력의 교집합(휴일/중단 포함)이며 배정률/단가는 "
        "가중치에 쓰지 않습니다. 가중분0인 날은 생략합니다. 전체 총량의 의미와 추정임을 확인해야 반영할 수 있습니다.");
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    layout->addWidget(note);
    std::vector<const XmlActualInterval *> ranges;
    for (const auto &r : input.actualIntervals)
        if (r.multipleDays)
            ranges.push_back(&r);
    using Pair = std::pair<std::string, std::string>;
    std::vector<Pair> targets;
    std::map<std::string, std::string> taskNames, resourceNames;
    for (const auto &t : snapshot->tasks)
        taskNames[t.id] = t.name;
    for (const auto &r : snapshot->resources)
        if (r.type == "labor" || r.type == "equipment")
            resourceNames[r.id] = r.name;
    auto *model = new QStandardItemModel(&dialog);
    auto *blank = new QStandardItem("native 배정 선택…");
    blank->setData(-1, Qt::UserRole);
    model->appendRow(blank);
    for (const auto &a : snapshot->assignments)
        if (resourceNames.contains(a.resourceId)) {
            auto *v = new QStandardItem(qs(a.taskId + " / " + a.resourceId + " · " + taskNames.at(a.taskId) + " / " +
                                           resourceNames.at(a.resourceId)));
            v->setData(int(targets.size()), Qt::UserRole);
            model->appendRow(v);
            targets.emplace_back(a.taskId, a.resourceId);
        }
    auto *table = new QTableWidget(int(ranges.size()), 7);
    table->setObjectName("xmlAllocationRanges");
    table->setHorizontalHeaderLabels({"선택", "XML 배정/구간", "원본 범위 [시작,끝)", "종류/Unit",
                                      "원본 Value · 총량 확인 필요", "native 배정", "검토"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setMinimumHeight(110);
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto &r = *ranges[i];
        auto *select = new QTableWidgetItem;
        select->setFlags(r.issue.empty() ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        select->setCheckState(Qt::Unchecked);
        table->setItem(int(i), 0, select);
        const QStringList values{
            qs(r.assignmentUid + " / " + std::to_string(r.sourceIndex) + " · " + r.name),
            qs(r.start + " → " + r.finish + (r.wholeDays ? " · 전체일 버킷" : "")),
            QString(r.type == 2 ? "공수 Type2 / " : "원가 Type6 / ") + QString::number(r.unit),
            r.type == 2
                ? work(r.workUnitMinutes ? QJsonValue(double(*r.workUnitMinutes)) : QJsonValue(QJsonValue::Null))
                : money(r.actualMinor ? QJsonValue(double(*r.actualMinor)) : QJsonValue(QJsonValue::Null),
                        input.actualCurrency)};
        for (int j = 0; j < values.size(); ++j) {
            table->setItem(int(i), j + 1, new QTableWidgetItem(values[j]));
            table->item(int(i), j + 1)->setToolTip(values[j]);
        }
        auto *target = new QComboBox;
        target->setModel(model);
        target->setMaximumWidth(330);
        table->setCellWidget(int(i), 5, target);
        table->setItem(int(i), 6, new QTableWidgetItem(qs(r.issue.empty() ? "미선택 · 총량 의미 확인" : r.issue)));
    }
    table->resizeRowsToContents();
    layout->addWidget(table, 1);
    auto *basis = new QComboBox;
    basis->setObjectName("xmlAllocationBasis");
    basis->addItem("배분 기준 선택…", -1);
    basis->addItem("날짜별 경과분 · 주말 포함", int(XmlAllocationBasis::ElapsedMinutes));
    basis->addItem("현재 작업·자원 달력 교집합 근무분", int(XmlAllocationBasis::WorkingIntersectionMinutes));
    layout->addWidget(basis);
    auto *confirmed = new QCheckBox("원본 Value는 구간 전체 총량 · 일별 배분은 비례 추정임을 확인");
    confirmed->setObjectName("xmlAllocationConfirm");
    layout->addWidget(confirmed);
    auto *form = new QFormLayout;
    auto *reporter = new QLineEdit;
    reporter->setObjectName("xmlAllocationReporter");
    reporter->setMaxLength(128);
    auto *reason = new QLineEdit;
    reason->setObjectName("xmlAllocationReason");
    reason->setMaxLength(512);
    form->addRow("작성자", reporter);
    form->addRow("배분 사유", reason);
    layout->addLayout(form);
    auto *previewButton = new QPushButton("선택 구간 · 일별 배분 미리보기");
    previewButton->setObjectName("xmlAllocationPreview");
    layout->addWidget(previewButton);
    auto *days = new QTableWidget(0, 8);
    days->setObjectName("xmlAllocationDays");
    days->setHorizontalHeaderLabels({"native 배정", "날짜", "원본 가중분/합계:배분", "배분 공수", "배분 원가(원본)",
                                     "현재 공수/원가", "반영 공수/원가", "처리"});
    days->setEditTriggers(QAbstractItemView::NoEditTriggers);
    days->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    days->setMinimumHeight(120);
    layout->addWidget(days, 1);
    auto *summary = new QLabel("배분 미리보기를 먼저 확인하세요. 원본 XML을 함께 보관하세요.");
    summary->setObjectName("xmlAllocationSummary");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *destination =
        new QLineEdit(QDir(doc.path().empty() ? QDir::homePath() : QFileInfo(qs(doc.path())).absolutePath())
                          .filePath("xml-proportional-review"));
    destination->setObjectName("xmlAllocationDestination");
    destination->setAccessibleName("새 배분 검토 기록 폴더 전체 경로");
    layout->addWidget(destination);
    auto *exportButton = new QPushButton("검토 기록을 새 폴더에 저장 (원본 XML/native·배분 JSON)");
    exportButton->setObjectName("xmlAllocationExport");
    exportButton->setEnabled(false);
    layout->addWidget(exportButton);
    auto *error = new QLabel;
    error->setObjectName("xmlAllocationError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("미리보기 재검증 · 비례 추정 채택");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);
    std::optional<XmlActualAllocation> preview;
    XmlProgressOptions preparedOptions;
    XmlAllocationPolicy preparedPolicy;
    std::vector<XmlPeriodMapping> preparedMappings;
    const auto invalidate = [&] {
        preview.reset();
        days->setRowCount(0);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        exportButton->setEnabled(false);
        for (std::size_t i = 0; i < ranges.size(); ++i)
            if (ranges[i]->issue.empty())
                table->item(int(i), 6)->setText(table->item(int(i), 0)->checkState() == Qt::Checked
                                                    ? "선택 · 미리보기 필요"
                                                    : "미선택 · 총량 의미 확인");
        summary->setText("선택/기준/총량 확인/작성자/사유 변경 · 다시 미리보기하세요.");
    };
    QObject::connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem *v) {
        if (v->column() == 0)
            invalidate();
    });
    for (int i = 0; i < table->rowCount(); ++i)
        QObject::connect(qobject_cast<QComboBox *>(table->cellWidget(i, 5)), &QComboBox::currentIndexChanged, &dialog,
                         [&] { invalidate(); });
    QObject::connect(basis, &QComboBox::currentIndexChanged, &dialog, [&] { invalidate(); });
    QObject::connect(confirmed, &QCheckBox::toggled, &dialog, [&] { invalidate(); });
    QObject::connect(reporter, &QLineEdit::textChanged, &dialog, [&] { invalidate(); });
    QObject::connect(reason, &QLineEdit::textChanged, &dialog, [&] { invalidate(); });
    const auto unchanged = [&] {
        if (doc.sessionGeneration() != session || !doc.project() || doc.project()->revision != snapshot->revision)
            throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
    };
    QObject::connect(previewButton, &QPushButton::clicked, &dialog, [&] {
        try {
            unchanged();
            if (basis->currentData().toInt() < 0)
                throw AppError(ErrorCode::XML_PROGRESS_INVALID, {"배분 기준을 선택하세요."});
            preparedOptions = {*snapshot->statusAt,
                               reporter->text().trimmed().toStdString(),
                               reason->text().trimmed().toStdString(),
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString(),
                               {},
                               true};
            preparedPolicy = {XmlAllocationBasis(basis->currentData().toInt()), confirmed->isChecked()};
            preparedMappings.clear();
            for (std::size_t i = 0; i < ranges.size(); ++i)
                if (table->item(int(i), 0)->checkState() == Qt::Checked) {
                    const auto index = qobject_cast<QComboBox *>(table->cellWidget(int(i), 5))->currentData().toInt();
                    const auto target = index < 0 ? Pair{} : targets.at(std::size_t(index));
                    preparedMappings.push_back({ranges[i]->key, target.first, target.second});
                }
            const auto o = preparedOptions;
            const auto policy = preparedPolicy;
            const auto m = preparedMappings;
            auto out = prepareExchange<XmlActualAllocation>(
                &dialog, doc, "원본·가중분·정수 배분·누계·마감 검증 중…",
                [snapshot, input, o, m, policy] { return prepareXmlActualAllocation(*snapshot, input, o, m, policy); });
            if (!out)
                return;
            preview = std::move(*out);
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i, 0)->checkState() == Qt::Checked)
                    table->item(i, 6)->setText("검증 완료 · 비례 추정");
            const auto values = preview->report["days"].toArray();
            days->setRowCount(values.size());
            int n = 0;
            for (const auto &v : values) {
                const auto r = v.toObject();
                const QStringList cells{r["taskId"].toString() + " / " + r["resourceId"].toString(),
                                        r["date"].toString(),
                                        r["parts"].toString(),
                                        work(r["allocatedWork"]),
                                        money(r["allocatedCostSourceMinor"], input.actualCurrency),
                                        work(r["oldWork"]) + " / " + money(r["oldCostNativeMinor"], snapshot->currency),
                                        work(r["adoptedWork"]) + " / " +
                                            money(r["adoptedCostNativeMinor"], snapshot->currency),
                                        r["alreadyApplied"].toBool() ? "기반영 · 현재 정정 유지" : "비례 추정 · 신규"};
                for (int c = 0; c < cells.size(); ++c) {
                    days->setItem(n, c, new QTableWidgetItem(cells[c]));
                    days->item(n, c)->setToolTip(cells[c]);
                }
                ++n;
            }
            days->resizeRowsToContents();
            QString text = QString("구간 %1개 · 일별 %2행 · 신규 보고 %3건 · 비례 추정, 측정값 아님\n")
                               .arg(preparedMappings.size())
                               .arg(values.size())
                               .arg(preview->batches.size());
            for (const auto &v : preview->report["costReconciliation"].toArray()) {
                const auto a = v.toObject();
                text += a["taskId"].toString() + " 누계 " + money(a["cumulative"], snapshot->currency) + " / 배분 " +
                        money(a["afterAllocated"], snapshot->currency) + " / 미배분 " +
                        money(a["unallocated"], snapshot->currency) + "\n";
            }
            summary->setText(text);
            error->clear();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            exportButton->setEnabled(true);
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        try {
            unchanged();
            if (!preview)
                return;
            const auto out = *preview;
            const auto o = preparedOptions;
            const auto policy = preparedPolicy;
            const auto m = preparedMappings;
            const auto path = destination->text().trimmed();
            QString folder;
            if (runExchangeOperation(
                    &dialog, doc, "배분 검토 기록 게시 중…",
                    [&] { folder = publishXmlActualAllocation(*snapshot, input, o, m, policy, out, path); }, true))
                summary->setText(summary->text() + "검토 기록: " + folder + "\nsource.ppm은 채택 전 원본 사본입니다.");
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            unchanged();
            if (!preview)
                return;
            const auto out = *preview;
            const auto o = preparedOptions;
            const auto policy = preparedPolicy;
            const auto m = preparedMappings;
            auto rows = prepareExchange<std::vector<PeriodActualSubmission>>(
                &dialog, doc, "미리보기·원본·가중분 재검증 중…", [snapshot, input, o, m, policy, out] {
                    return confirmXmlActualAllocation(*snapshot, input, o, m, policy, out);
                });
            if (!rows)
                return;
            auto request = doc.makeEditRequest(PeriodActualBatchEdit{std::move(*rows), *snapshot->statusAt});
            auto prepared = prepareExchange<std::shared_ptr<Document::PreparedEdit>>(
                &dialog, doc, "정정 원장·마감 기간 검증 중…",
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
    const auto disconnect = qScopeGuard([&] {
        for (auto *child : dialog.findChildren<QObject *>())
            QObject::disconnect(child, nullptr, &dialog, nullptr);
    });
    dialog.resize(1100, 800);
    prepareEditorDialog(dialog);
    return dialog.exec() == QDialog::Accepted;
}
} // namespace ppm
