#include "ui/ResourceAvailabilityDialog.h"
#include "ui/EditorUi.h"
#include "application/Document.h"
#include "calculation/ResourceAvailabilityQuery.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace ppm {
void showResourceAvailability(Document &doc, QWidget *parent) {
    if (!doc.project())
        return;
    QDialog dialog(parent);
    dialog.setObjectName("remainingAvailabilityDialog");
    dialog.setWindowTitle("현재 부하·잔여 가용량");
    dialog.resize(1150, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note =
        new QLabel("유효 가용률(자원 근무 달력 반영) − 전체 프로젝트의 동시간대 배정 수요입니다. 잔여가 음수면 "
                   "부족합니다. 배정 가능량은 음수를0으로 제한합니다. 선택 작업/WBS로 수요를 줄이지 않습니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *date =
        new QLineEdit(QString::fromStdString(doc.project()->statusAt.value_or(doc.project()->startDate + " 09:00")));
    date->setObjectName("availabilityQueryAt");
    date->setPlaceholderText("YYYY-MM-DD HH:mm");
    form->addRow("조회 시각 · civil 분", date);
    auto *mode = new QComboBox;
    mode->setObjectName("availabilityQueryMode");
    mode->addItem("현재 일정 · 상태일 있으면 잔여 예측 · 채택 평준화 포함", false);
    mode->addItem("계획 일정 · 채택 계획 평준화 포함", true);
    form->addRow("수요 기준", mode);
    auto *run = new QPushButton("잔여 가용량 계산");
    run->setObjectName("calculateRemainingAvailability");
    layout->addWidget(run);
    auto *status = new QLabel;
    status->setObjectName("remainingAvailabilityStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *table = new QTableWidget(0, 9);
    table->setObjectName("remainingAvailabilityTable");
    table->setHorizontalHeaderLabels(
        {"자원", "자원 ID", "유효 가용률", "배정 수요", "잔여", "배정 가능", "부족", "자원 근무", "상태"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText("닫기");
    auto *cancel = buttons->addButton("계산 취소", QDialogButtonBox::ActionRole);
    cancel->setObjectName("cancelRemainingAvailability");
    cancel->setEnabled(false);
    layout->addWidget(buttons);
    std::shared_ptr<CalculationControl> control;
    std::uint64_t generation = 0;
    auto invalidate = [&] {
        ++generation;
        if (control)
            control->cancel();
        table->setRowCount(0);
        cancel->setEnabled(false);
        run->setEnabled(true);
        status->setText("조회 조건/문서가 변경되었습니다. 다시 계산하세요.");
        dialog.setProperty("availabilityBusy", false);
    };
    auto observedSession = doc.sessionGeneration();
    auto observedRevision = doc.project()->revision;
    auto observedId = doc.project()->documentId;
    auto listener = doc.subscribe([&](auto) {
        if (!doc.project() || doc.sessionGeneration() != observedSession ||
            doc.project()->revision != observedRevision || doc.project()->documentId != observedId) {
            invalidate();
            observedSession = doc.sessionGeneration();
            observedRevision = doc.project() ? doc.project()->revision : 0;
            observedId = doc.project() ? doc.project()->documentId : "";
        }
    });
    QObject::connect(date, &QLineEdit::textChanged, &dialog, [&] { invalidate(); });
    QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [&] { invalidate(); });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        status->setText("계산을 취소했습니다.");
    });
    struct Output {
        std::vector<ResourceAvailabilityPoint> rows;
        QString error, caption;
    };
    QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        const auto at = date->text().trimmed().toStdString();
        if (!validStamp(at)) {
            status->setText("조회 시각을 YYYY-MM-DD HH:mm 형식으로 입력하세요.");
            return;
        }
        auto source = doc.snapshot();
        if (!source)
            return;
        const auto session = doc.sessionGeneration(), ticket = generation;
        const bool plan = mode->currentData().toBool();
        control = std::make_shared<CalculationControl>();
        run->setEnabled(false);
        cancel->setEnabled(true);
        status->setText("일정·전체 자원 수요 계산 중…");
        dialog.setProperty("availabilityBusy", true);
        auto *watcher = new QFutureWatcher<Output>(&dialog);
        QObject::connect(
            watcher, &QFutureWatcher<Output>::finished, &dialog, [&, watcher, source, session, ticket, at] {
                auto out = watcher->result();
                watcher->deleteLater();
                if (ticket != generation)
                    return;
                dialog.setProperty("availabilityBusy", false);
                run->setEnabled(true);
                cancel->setEnabled(false);
                if (doc.sessionGeneration() != session || !doc.project() ||
                    doc.project()->documentId != source->documentId || doc.project()->revision != source->revision) {
                    invalidate();
                    return;
                }
                if (!out.error.isEmpty()) {
                    status->setText(out.error);
                    return;
                }
                status->setText(out.caption + " · " + QString::fromStdString(at) + " · rev " +
                                QString::number(source->revision) + " · 읽기 전용 조회");
                table->setRowCount(int(out.rows.size()));
                const auto percent = [](const auto &value) {
                    return value ? QString::number(*value) + "%" : QString("미정");
                };
                for (int i = 0; i < int(out.rows.size()); ++i) {
                    const auto &v = out.rows[std::size_t(i)];
                    const auto &r = source->resources[std::size_t(i)];
                    QStringList cells{QString::fromStdString(r.name),
                                      QString::fromStdString(v.resourceId),
                                      percent(v.capacity),
                                      percent(v.demand),
                                      percent(v.remaining),
                                      percent(v.freeUnits),
                                      percent(v.shortage),
                                      v.working ? "근무 시간" : "근무 시간 외",
                                      QString::fromStdString(v.issue)};
                    for (int c = 0; c < cells.size(); ++c) {
                        auto *item = new QTableWidgetItem(cells[c]);
                        item->setToolTip(cells[c]);
                        table->setItem(i, c, item);
                    }
                }
            });
        watcher->setFuture(QtConcurrent::run([source, at, plan, control = control] {
            Output out;
            try {
                EvaluationScope evaluation(control);
                auto schedule = plan ? calculatePlan(*source) : calculate(*source);
                if (schedule.status == CalculationStatus::Cancelled)
                    throw CalculationCancelled{};
                ResourceAvailabilityQuery query(*source, schedule);
                for (const auto &r : source->resources)
                    out.rows.push_back(query.at(r.id, at));
                out.caption = schedule.isForecast ? "현재 잔여 예측" : "현재 계획";
                if (schedule.resourceLeveled)
                    out.caption += " · 채택 평준화";
            } catch (const CalculationCancelled &) {
                out.error = "계산 취소";
            } catch (const std::exception &e) {
                out.error = QString::fromUtf8(e.what());
            }
            return out;
        }));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QTimer::singleShot(0, run, &QPushButton::click);
    prepareEditorDialog(dialog);
    dialog.exec();
    ++generation;
    if (control)
        control->cancel();
    doc.unsubscribe(listener);
}
} // namespace ppm
