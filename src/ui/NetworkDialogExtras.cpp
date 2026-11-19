#include "ui/NetworkDialog.h"
#include "ui/NetworkPrintDialog.h"
#include "ui/EditorUi.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <cmath>
#include <QDateTime>
#include <QUuid>
#include "formatting/PlanTokens.h"
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace ppm {
void NetworkDialog::saveLayout() {
    if (!current() || !windowReady_ || document_.readOnly())
        return;
    try {
        std::map<std::string, NetworkPosition> all;
        for (const auto &v : document_.project()->networkPositions)
            all[v.taskId] = v;
        for (auto *item : scene_->items())
            if (item->data(1) == "networkNode") {
                const auto id = item->data(0).toString().toStdString();
                const auto pos = item->pos();
                if (!std::isfinite(pos.x()) || !std::isfinite(pos.y()) || pos.x() < -1000000 || pos.x() > 1000000 ||
                    pos.y() < -1000000 || pos.y() > 1000000)
                    throw std::runtime_error("배치 좌표는 ±1,000,000 이내여야 합니다.");
                all[id] = {id, qRound(pos.x()), qRound(pos.y())};
            }
        std::vector<NetworkPosition> values;
        for (const auto &[id, v] : all) {
            (void)id;
            values.push_back(v);
        }
        savingLayout_ = true;
        document_.setNetworkPositions(std::move(values));
        savingLayout_ = false;
        draftPositions_.clear();
        refresh();
    } catch (const std::exception &e) {
        savingLayout_ = false;
        summary_->setText(qErrorText(e));
    }
}
void NetworkDialog::printNetwork() {
    if (!current() || !windowReady_ || scene_->items().isEmpty())
        return;
    // Record bounded vector items on the GUI thread. Printing owns this frozen picture.
    QPicture picture;
    QPainter painter(&picture);
    const auto bounds = scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20);
    scene_->render(&painter, bounds, bounds);
    painter.end();
    const auto provenance = summary_->text() + " · " + QString::fromStdString(graph_->calculation.engineVersion) +
                            " · " + QString::fromStdString(graph_->calculation.calculationPolicy) + " · 문서 " +
                            QString::fromStdString(graph_->documentId) + " · UTC " +
                            QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " · 인쇄 ID " +
                            QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList facts{QString::fromStdString(graph_->name) + " · " + provenance,
                      "표시망 출처 · 카드의 축약 전 전체 작업명/ID와 표시 관계입니다. 생략 관계는 전체 "
                      "프로젝트에서 삭제된 관계가 아닙니다."};
    for (const auto &v : window_.nodes) {
        const auto &n = graph_->nodes[v.node];
        facts << QString::number(v.node + 1) + " · " + QString::fromStdString(n.id) + " · " +
                     QString::fromStdString(n.name);
    }
    for (const auto index : window_.edges) {
        const auto &e = graph_->edges[index];
        facts << QString::fromStdString(graph_->nodes[e.from].id) + " → " +
                     QString::fromStdString(graph_->nodes[e.to].id) + " · " +
                     QString::fromStdString(enumToken(e.type)) + " " + QString::number(e.lagMinutes) + "m · 자원 " +
                     QString::fromStdString(e.resourceId) + " · " + QString::number(e.resourceUnits) + "%";
    }
    showNetworkPrint(document_, std::move(picture), bounds, provenance, this, facts);
}
void NetworkDialog::editRelations() {
    if (!current() || !windowReady_ || document_.readOnly() || selected_.empty())
        return;
    const auto source = document_.snapshot();
    const auto session = document_.sessionGeneration();
    const auto target = selected_;
    const auto t =
        std::find_if(source->tasks.begin(), source->tasks.end(), [&](const auto &v) { return v.id == target; });
    if (t == source->tasks.end())
        return;
    QDialog dialog(this);
    dialog.setObjectName("networkRelationEditor");
    dialog.setWindowTitle("선택 작업의 수신 관계 편집 · " + QString::fromStdString(t->name));
    dialog.resize(850, 550);
    auto *body = new QVBoxLayout(&dialog);
    auto *note = new QLabel("원본 선행 관계만 편집합니다. 자원 인계선은 계산 결과입니다. 선행 원본 번호 또는 안정 ID, "
                            "FS/SS/FF/SF, 부호 있는 근무분 지연을 입력하세요.",
                            &dialog);
    note->setWordWrap(true);
    body->addWidget(note);
    auto *table = new QTableWidget(0, 3, &dialog);
    table->setObjectName("networkDependencyTable");
    table->setHorizontalHeaderLabels({"선행 원본 번호 / ID", "관계", "지연(근무분)"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    body->addWidget(table);
    auto add = [&](const Dependency *d) {
        int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(d ? QString::fromStdString(d->predecessorId) : QString{}));
        auto *type = new QComboBox(table);
        type->addItems({"FS", "SS", "FF", "SF"});
        type->setCurrentIndex(d ? int(d->type) : 0);
        table->setCellWidget(row, 1, type);
        auto *lag = new QSpinBox(table);
        lag->setRange(-5000000, 5000000);
        lag->setValue(d ? int(d->lagMinutes) : 0);
        table->setCellWidget(row, 2, lag);
    };
    for (const auto &d : dependencies(*t))
        add(&d);
    auto *rows = new QHBoxLayout;
    body->addLayout(rows);
    auto *plus = new QPushButton("관계 추가", &dialog);
    plus->setObjectName("addNetworkDependency");
    auto *minus = new QPushButton("선택 관계 삭제", &dialog);
    minus->setObjectName("removeNetworkDependency");
    rows->addWidget(plus);
    rows->addWidget(minus);
    connect(plus, &QPushButton::clicked, &dialog, [&] { add(nullptr); });
    connect(minus, &QPushButton::clicked, &dialog, [&] {
        const auto row = table->currentRow();
        if (row >= 0)
            table->removeRow(row);
    });
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    body->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    body->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    std::shared_ptr<CalculationControl> control;
    bool busy = false;
    connect(&dialog, &QDialog::finished, &dialog, [&] {
        if (control)
            control->cancel();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (busy || document_.readOnly() || !document_.project() || document_.sessionGeneration() != session ||
                document_.project()->documentId != source->documentId ||
                document_.project()->revision != source->revision)
                throw std::runtime_error("문서가 변경되었습니다. 관계 편집을 다시 여세요.");
            std::vector<Dependency> edges;
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto text = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : QString{};
                auto id = text.toStdString();
                if (std::none_of(source->tasks.begin(), source->tasks.end(),
                                 [&](const auto &v) { return v.id == id; })) {
                    bool ok = false;
                    const auto n = text.toInt(&ok);
                    if (!ok || n < 1 || n > int(source->tasks.size()))
                        throw std::runtime_error("선행 작업 번호/ID를 확인하세요.");
                    id = source->tasks[n - 1].id;
                }
                edges.push_back({id,
                                 DependencyType(qobject_cast<QComboBox *>(table->cellWidget(row, 1))->currentIndex()),
                                 qobject_cast<QSpinBox *>(table->cellWidget(row, 2))->value()});
            }
            TaskEdit change;
            change.taskId = target;
            change.edges = edges;
            auto request = document_.makeEditRequest(BatchTaskEdit{{change}});
            control = std::make_shared<CalculationControl>();
            busy = true;
            table->setEnabled(false);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            auto *watcher = new QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>(&dialog);
            connect(watcher, &QFutureWatcher<std::shared_ptr<Document::PreparedEdit>>::finished, &dialog, [&, watcher] {
                auto result = watcher->result();
                watcher->deleteLater();
                const auto state = document_.applyPreparedEdit(*result);
                busy = false;
                table->setEnabled(true);
                buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
                if (state == EditState::Applied || state == EditState::NoChange) {
                    dialog.accept();
                    refresh();
                } else
                    error->setText(result->failure() ? QString::fromStdString(diagnosticMessage(*result->failure()))
                                                     : "취소되었거나 문서가 변경되어 적용하지 않았습니다.");
            });
            watcher->setFuture(QtConcurrent::run([request = std::move(request), control]() mutable {
                return Document::prepareEdit(std::move(request), control);
            }));
        } catch (const std::exception &e) {
            error->setText(qErrorText(e));
        }
    });
    prepareEditorDialog(dialog);
    dialog.exec();
}
} // namespace ppm
