#include "ui/SharedAvailabilityDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/EditorUi.h"
#include "ui/SandboxAccess.h"
#include "reporting/SharedAvailabilityExport.h"
#include "reporting/ReportExport.h"
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace jobs {
void showSharedAvailability(DesktopWorkspace &workspace, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("sharedAvailabilityDialog");
    dialog.setWindowTitle("시각별 공유 잔여 가용량");
    dialog.resize(1200, 740);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel("공유 풀의 가용량에서 연결된 모든 프로젝트의 동시 수요를 차감합니다. 전체 수요가 미정이면 "
                            "잔여/배정 가능량도 미정입니다. 확인된 수요는 하한이며 전체 수요를 대신하지 않습니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    std::string initial;
    std::set<std::string> referenced;
    for (const auto &ref : workspace.configuration().projects)
        referenced.insert(ref.documentId);
    const auto opened = workspace.snapshots();
    for (const auto &p : opened)
        if (referenced.contains(p->documentId) && p->statusAt && (initial.empty() || *p->statusAt > initial))
            initial = *p->statusAt;
    if (initial.empty())
        for (const auto &p : opened)
            if (referenced.contains(p->documentId) && (initial.empty() || p->startDate + " 09:00" < initial))
                initial = p->startDate + " 09:00";
    auto *date = new QLineEdit(QString::fromStdString(initial));
    date->setObjectName("sharedAvailabilityAt");
    date->setPlaceholderText("YYYY-MM-DD HH:mm");
    form->addRow("조회 시각 · 분 단위", date);
    auto *mode = new QComboBox;
    mode->setObjectName("sharedAvailabilityMode");
    mode->addItems({"현재 일정 · 파일별 상태일/채택 평준화", "계획 일정 · 채택 계획 평준화"});
    form->addRow("수요 기준", mode);
    auto *run = new QPushButton("공유 잔여 가용량 계산");
    run->setObjectName("calculateSharedAvailability");
    layout->addWidget(run);
    auto *status = new QLabel;
    status->setObjectName("sharedAvailabilityStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    const auto table = [&](const QString &id, const QString &label, const QStringList &headers) {
        auto *view = new QTableWidget(0, headers.size());
        view->setObjectName(id);
        view->setHorizontalHeaderLabels(headers);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tabs->addTab(view, label);
        return view;
    };
    auto *points =
        table("sharedAvailabilityPoints", "공유 잔여량",
              {"공유 자원", "공유 ID", "유효 가용", "전체 수요", "확인 수요", "잔여", "배정 가능", "부족", "상태"});
    auto *contributions = table("sharedAvailabilityContributions", "프로젝트별 기여",
                                {"공유 ID", "프로젝트 ID", "revision", "전체 기여", "확인 기여", "상태"});
    auto *sources = table("sharedAvailabilitySources", "입력과 상태일",
                          {"프로젝트", "프로젝트 ID", "revision", "계산 기준", "상태일", "일정 상태"});
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText("닫기");
    layout->addWidget(buttons);
    auto *cancel = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
    cancel->setObjectName("cancelSharedAvailability");
    cancel->setEnabled(false);
    std::vector<QPushButton *> exports;
    for (const auto &name : {"JSON", "XLSX", "PDF"}) {
        auto *b = buttons->addButton(QString(name) + " 저장…", QDialogButtonBox::ActionRole);
        b->setObjectName(QString("exportSharedAvailability") + name);
        b->setEnabled(false);
        exports.push_back(b);
    }
    std::shared_ptr<CalculationControl> control;
    std::shared_ptr<const SharedAvailabilityResult> result;
    std::uint64_t ticket = 0, observed = workspace.generation(), resultGeneration = 0;
    const auto invalidate = [&] {
        ++ticket;
        if (control)
            control->cancel();
        result.reset();
        points->setRowCount(0);
        contributions->setRowCount(0);
        sources->setRowCount(0);
        run->setEnabled(true);
        cancel->setEnabled(false);
        for (auto *b : exports)
            b->setEnabled(false);
        dialog.setProperty("sharedAvailabilityBusy", false);
        status->setText("입력·시각·계산 기준이 변경되었습니다. 다시 계산하세요.");
    };
    QObject::connect(date, &QLineEdit::textChanged, &dialog, [&] { invalidate(); });
    QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [&] { invalidate(); });
    auto *timer = new QTimer(&dialog);
    timer->setInterval(100);
    QObject::connect(timer, &QTimer::timeout, &dialog, [&] {
        if (observed != workspace.generation()) {
            observed = workspace.generation();
            invalidate();
        }
    });
    timer->start();
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        status->setText("계산을 취소했습니다.");
    });
    struct Output {
        std::shared_ptr<const SharedAvailabilityResult> result;
        QString error;
    };
    const auto fill = [](QTableWidget *table, int row, const QStringList &cells) {
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            item->setToolTip(cells[c]);
            table->setItem(row, c, item);
        }
    };
    const auto percent = [](const auto &v) { return v ? QString::number(*v) + "%" : QString("미정"); };
    QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        observed = workspace.generation();
        const auto generation = observed, currentTicket = ticket;
        auto at = date->text().trimmed().toStdString();
        if (!validStamp(at)) {
            status->setText("조회 시각을 YYYY-MM-DD HH:mm으로 입력하세요.");
            return;
        }
        auto config = workspace.configuration();
        auto snapshots = workspace.snapshots();
        const bool forecast = mode->currentIndex() == 0;
        control = std::make_shared<CalculationControl>();
        run->setEnabled(false);
        cancel->setEnabled(true);
        dialog.setProperty("sharedAvailabilityBusy", true);
        status->setText("프로젝트별 일정과 공유 수요 계산 중…");
        auto *watcher = new QFutureWatcher<Output>(&dialog);
        QObject::connect(
            watcher, &QFutureWatcher<Output>::finished, &dialog, [&, watcher, generation, currentTicket, config] {
                auto out = watcher->result();
                watcher->deleteLater();
                if (currentTicket != ticket)
                    return;
                if (generation != workspace.generation()) {
                    observed = workspace.generation();
                    invalidate();
                    return;
                }
                run->setEnabled(true);
                cancel->setEnabled(false);
                dialog.setProperty("sharedAvailabilityBusy", false);
                if (!out.error.isEmpty()) {
                    status->setText(out.error);
                    return;
                }
                result = std::move(out.result);
                resultGeneration = generation;
                std::map<std::string, std::string> names;
                for (const auto &r : config.resources)
                    names[r.id] = r.name;
                points->setRowCount(int(result->points.size()));
                int rows = 0;
                for (std::size_t i = 0; i < result->points.size(); ++i) {
                    const auto &p = result->points[i];
                    const auto &v = p.value;
                    fill(points, int(i),
                         {QString::fromStdString(names.at(v.resourceId)), QString::fromStdString(v.resourceId),
                          percent(v.capacity), percent(v.demand), QString::number(p.knownDemand) + "%",
                          percent(v.remaining), percent(v.freeUnits), percent(v.shortage),
                          QString::fromStdString(v.issue)});
                    rows += int(p.contributions.size());
                }
                contributions->setRowCount(rows);
                int index = 0;
                for (const auto &p : result->points)
                    for (const auto &c : p.contributions)
                        fill(contributions, index++,
                             {QString::fromStdString(p.value.resourceId), QString::fromStdString(c.projectId),
                              c.revision ? QString::number(*c.revision) : QString("미정"), percent(c.demand),
                              QString::number(c.knownDemand) + "%", QString::fromStdString(c.issue)});
                sources->setRowCount(int(result->sources.size()));
                for (std::size_t i = 0; i < result->sources.size(); ++i) {
                    const auto &s = result->sources[i];
                    fill(sources, int(i),
                         {QString::fromStdString(s.name), QString::fromStdString(s.projectId),
                          s.available ? QString::number(s.revision) : QString("미정"),
                          !s.available ? "미정"
                          : s.forecast ? "잔여 예측"
                                       : "계획",
                          QString::fromStdString(s.statusAt.value_or("")),
                          s.status == CalculationStatus::Valid ? "유효" : QString::fromStdString(s.issue)});
                }
                status->setText(QString::fromStdString(result->at) + " · 프로젝트 " +
                                QString::number(result->sources.size()) + " · 열린 파일의 미연결 자원 " +
                                QString::number(result->unmappedResources) +
                                " · 전체 수요/잔여가 미정인 풀은 기여와 상태일을 확인하세요.");
                for (auto *b : exports)
                    b->setEnabled(true);
            });
        watcher->setFuture(QtConcurrent::run([config, snapshots, forecast, at, control = control] {
            Output out;
            try {
                SharedAvailabilityQuery query(config, snapshots, forecast, control);
                EvaluationScope evaluation(control);
                out.result = std::make_shared<const SharedAvailabilityResult>(query.allAt(at));
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
                if (!result || resultGeneration != workspace.generation())
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                const QString extension = format == 0 ? "json" : format == 1 ? "xlsx" : "pdf";
                auto path = QFileDialog::getSaveFileName(&dialog, "공유 잔여 가용량 결과 저장", {},
                                                         "공유 잔여량 (*." + extension + ")");
                if (path.isEmpty() || !authorizeSandboxFile(&dialog, path))
                    return;
                if (!path.endsWith("." + extension, Qt::CaseInsensitive))
                    path += "." + extension;
                if (!result || resultGeneration != workspace.generation())
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                if (format == 0) {
                    const auto bytes = sharedAvailabilityJson(*result->configuration, *result, resultGeneration);
                    QSaveFile file(path);
                    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                        throw AppError(ErrorCode::WORKSPACE_INVALID, {"파일 저장 실패"});
                } else {
                    auto report =
                        sharedAvailabilityReport(*result->configuration, *result, resultGeneration,
                                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString());
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
}
} // namespace jobs
