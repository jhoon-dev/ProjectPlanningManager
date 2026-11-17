#include "ui/MaterialInventoryDialog.h"
#include "ui/EditorUi.h"
#include "ui/SandboxAccess.h"
#include "application/Document.h"
#include "calculation/MaterialInventory.h"
#include "domain/ResourcePlan.h"
#include "reporting/ReportExport.h"
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace jobs {
void showMaterialInventory(Document &document, QWidget *parent) {
    if (!document.project())
        return;
    QDialog dialog(parent);
    dialog.setObjectName("materialInventoryDialog");
    dialog.setWindowTitle("계획 입고·잔여 자재 재고");
    dialog.resize(1200, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        "개시 재고에 누적 계획 입고를 더하고 기준 시각 이후 계획 소모를 차감합니다. 같은 분의 입고·소모를 함께 "
        "반영합니다. 계획 일정과 채택 계획 평준화를 사용하며 실제 입고·소모를 추정하지 않습니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *date = new QLineEdit(QString::fromStdString(document.project()->startDate) + " 09:00");
    date->setObjectName("materialInventoryAt");
    date->setPlaceholderText("YYYY-MM-DD HH:mm");
    layout->addWidget(date);
    auto *run = new QPushButton("계획 재고 계산");
    run->setObjectName("calculateMaterialInventory");
    layout->addWidget(run);
    auto *status = new QLabel;
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setObjectName("materialInventoryStatus");
    layout->addWidget(status);
    auto *table = new QTableWidget(0, 10);
    table->setObjectName("materialInventoryTable");
    table->setHorizontalHeaderLabels({"자재", "단위", "개시 시각", "개시 재고", "누적 입고", "누적 소모", "잔여",
                                      "사용 가능", "부족", "최초 부족/상태"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText("닫기");
    layout->addWidget(buttons);
    auto *cancel = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
    cancel->setEnabled(false);
    cancel->setObjectName("cancelMaterialInventory");
    std::vector<QPushButton *> exports;
    for (const auto &format : {"JSON", "XLSX", "PDF"}) {
        auto *b = buttons->addButton(QString(format) + " 저장…", QDialogButtonBox::ActionRole);
        b->setObjectName(QString("exportMaterialInventory") + format);
        b->setEnabled(false);
        exports.push_back(b);
    }
    struct Output {
        std::shared_ptr<const Project> source;
        std::vector<MaterialInventoryPoint> points;
        std::map<std::string, std::string> shortage;
        QString error;
        std::string at;
    };
    std::shared_ptr<const Output> result;
    std::shared_ptr<CalculationControl> control;
    std::uint64_t ticket = 0;
    const auto invalidate = [&] {
        ++ticket;
        if (control)
            control->cancel();
        result.reset();
        table->setRowCount(0);
        for (auto *b : exports)
            b->setEnabled(false);
        cancel->setEnabled(false);
        run->setEnabled(true);
        dialog.setProperty("materialInventoryBusy", false);
        status->setText("입력이나 조회 시각이 바뀌었습니다. 다시 계산하세요.");
    };
    auto listener = document.subscribe([&](auto) { invalidate(); });
    QObject::connect(date, &QLineEdit::textChanged, &dialog, [&] { invalidate(); });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        status->setText("계산을 취소했습니다.");
    });
    QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        const auto at = date->text().trimmed().toStdString();
        if (!validStamp(at)) {
            status->setText("조회 시각을 YYYY-MM-DD HH:mm으로 입력하세요.");
            return;
        }
        auto source = document.snapshot();
        if (!source)
            return;
        const auto session = document.sessionGeneration(), currentTicket = ticket;
        control = std::make_shared<CalculationControl>();
        run->setEnabled(false);
        cancel->setEnabled(true);
        dialog.setProperty("materialInventoryBusy", true);
        status->setText("계획 일정과 입고·소모 계산 중…");
        auto *watcher = new QFutureWatcher<Output>(&dialog);
        QObject::connect(watcher, &QFutureWatcher<Output>::finished, &dialog, [&, watcher, currentTicket, session] {
            auto out = watcher->result();
            watcher->deleteLater();
            if (currentTicket != ticket)
                return;
            if (!document.project() || document.sessionGeneration() != session ||
                document.project()->documentId != out.source->documentId ||
                document.project()->revision != out.source->revision) {
                invalidate();
                return;
            }
            run->setEnabled(true);
            cancel->setEnabled(false);
            dialog.setProperty("materialInventoryBusy", false);
            if (!out.error.isEmpty()) {
                status->setText(out.error);
                return;
            }
            result = std::make_shared<const Output>(std::move(out));
            table->setRowCount(int(result->points.size()));
            const auto quantity = [](auto v) {
                return v ? QString::fromStdString(formatMaterialQuantity(*v)) : QString("미정");
            };
            for (int i = 0; i < table->rowCount(); ++i) {
                const auto &p = result->points[std::size_t(i)];
                auto signedQuantity = [&](auto v) {
                    return !v       ? QString("미정")
                           : *v < 0 ? "-" + QString::fromStdString(formatMaterialQuantity(-*v))
                                    : quantity(v);
                };
                QStringList cells{
                    QString::fromStdString(p.name),
                    QString::fromStdString(p.unit),
                    QString::fromStdString(p.stockAt),
                    quantity(p.opening),
                    quantity(p.received),
                    quantity(p.consumed),
                    signedQuantity(p.remaining),
                    quantity(p.available),
                    quantity(p.shortage),
                    QString::fromStdString(p.issue.empty() ? result->shortage.at(p.materialId) : p.issue)};
                for (int c = 0; c < cells.size(); ++c) {
                    auto *item = new QTableWidgetItem(cells[c]);
                    item->setToolTip(cells[c]);
                    table->setItem(i, c, item);
                }
            }
            status->setText(QString::fromStdString(result->at) + " · 계획 재고 · 입력 revision " +
                            QString::number(result->source->revision));
            for (auto *b : exports)
                b->setEnabled(true);
        });
        watcher->setFuture(QtConcurrent::run([source, at, control = control] {
            Output out;
            out.source = source;
            out.at = at;
            try {
                EvaluationScope evaluation(control);
                auto schedule = calculatePlan(*source);
                MaterialInventoryQuery query(*source, schedule, control);
                out.points = query.allAt(at);
                for (const auto &p : out.points)
                    out.shortage[p.materialId] = query.firstShortage(p.materialId).value_or("");
            } catch (const CalculationCancelled &) {
                out.error = "계산 취소";
            } catch (const std::exception &e) {
                out.error = QString::fromUtf8(e.what());
            }
            return out;
        }));
    });
    for (int format = 0; format < 3; ++format)
        QObject::connect(exports[std::size_t(format)], &QPushButton::clicked, &dialog, [&, format] {
            try {
                auto current = [&] {
                    return result && document.project() &&
                           document.project()->documentId == result->source->documentId &&
                           document.project()->revision == result->source->revision;
                };
                if (!current())
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                const QString extension = format == 0 ? "json" : format == 1 ? "xlsx" : "pdf";
                auto path =
                    QFileDialog::getSaveFileName(&dialog, "계획 재고 결과 저장", {}, "계획 재고 (*." + extension + ")");
                if (path.isEmpty() || !authorizeSandboxFile(&dialog, path))
                    return;
                if (!path.endsWith("." + extension, Qt::CaseInsensitive))
                    path += "." + extension;
                if (!current())
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                if (format == 0) {
                    QJsonArray points;
                    auto number = [](auto v) { return v ? QJsonValue(qint64(*v)) : QJsonValue(QJsonValue::Null); };
                    for (const auto &p : result->points)
                        points.append(
                            QJsonObject{{"materialId", QString::fromStdString(p.materialId)},
                                        {"name", QString::fromStdString(p.name)},
                                        {"unit", QString::fromStdString(p.unit)},
                                        {"stockAt", QString::fromStdString(p.stockAt)},
                                        {"openingMilli", number(p.opening)},
                                        {"receivedMilli", number(p.received)},
                                        {"consumedMilli", number(p.consumed)},
                                        {"remainingMilli", number(p.remaining)},
                                        {"availableMilli", number(p.available)},
                                        {"shortageMilli", number(p.shortage)},
                                        {"firstShortage", QString::fromStdString(result->shortage.at(p.materialId))},
                                        {"issue", QString::fromStdString(p.issue)}});
                    auto bytes =
                        QJsonDocument(QJsonObject{{"contract", "ppm-material-inventory-v1"},
                                                  {"documentId", QString::fromStdString(result->source->documentId)},
                                                  {"revision", double(result->source->revision)},
                                                  {"at", QString::fromStdString(result->at)},
                                                  {"mode", "plan"},
                                                  {"quantityScale", 1000},
                                                  {"points", points}})
                            .toJson();
                    QSaveFile file(path);
                    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                        throw AppError(ErrorCode::FILE_OPERATION_FAILED);
                } else {
                    ProjectReport report;
                    report.documentId = result->source->documentId;
                    report.projectName = result->source->name;
                    report.revision = result->source->revision;
                    report.currency = result->source->currency;
                    report.scheduleStatus = CalculationStatus::Valid;
                    report.scopeName = "전체 프로젝트 계획 재고";
                    report.options.reportId = "material-inventory";
                    report.options.generatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                    ReportTable output{"materialInventory",
                                       "계획 입고·잔여 자재 재고",
                                       "조회 시각 " + result->at +
                                           " · 개시 재고 직전 값 + 입고 - 계획 소모 · 미정과0을 구분",
                                       ReportFamily::Resources,
                                       {},
                                       {}};
                    for (int c = 0; c < table->columnCount(); ++c)
                        output.columns.push_back(
                            {table->horizontalHeaderItem(c)->text().toStdString(), c == 2 || c == 9 ? 28 : 16});
                    for (int r = 0; r < table->rowCount(); ++r) {
                        std::vector<ReportCell> row;
                        for (int c = 0; c < table->columnCount(); ++c)
                            row.push_back({table->item(r, c)->text().toStdString(), CellFormat::Text});
                        output.rows.push_back(std::move(row));
                    }
                    report.tables.push_back(std::move(output));
                    if (format == 1)
                        exportXlsx(report, path);
                    else
                        exportPdf(report, path);
                }
                status->setText("저장했습니다: " + path);
            } catch (const std::exception &e) {
                showEditorError(*status, e);
            }
        });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QTimer::singleShot(0, run, &QPushButton::click);
    prepareEditorDialog(dialog);
    dialog.exec();
    ++ticket;
    if (control)
        control->cancel();
    document.unsubscribe(listener);
}
} // namespace jobs
