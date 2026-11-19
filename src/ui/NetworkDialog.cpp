#include "ui/NetworkDialog.h"
#include "reporting/PrintFont.h"
#include <QTimer>
#include <QGraphicsSceneMouseEvent>
#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
namespace ppm {
namespace {
constexpr double cardWidth = 300, cardHeight = 150, columnStep = 440, rowStep = 216;
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QString tooltip(const QString &text) {
    return "<qt>" + text.toHtmlEscaped().replace('\n', "<br/>") + "</qt>";
}
QString time(const std::optional<std::string> &s) {
    return s ? qs(*s) : "—";
}
QString lag(const NetworkEdge &edge) {
    if (!edge.resourceId.empty())
        return "자원 순서 " + qs(edge.resourceId) + " / " + QString::number(edge.resourceUnits) + "%";
    return qs(enumToken(edge.type)) + " " + (edge.lagMinutes >= 0 ? "+" : "") + QString::number(edge.lagMinutes) + "m";
}
QString driving(NetworkDriving value) {
    return value == NetworkDriving::Yes             ? "driving"
           : value == NetworkDriving::No            ? "비 binding"
           : value == NetworkDriving::Inactive      ? "채택 진척 정책 비활성"
           : value == NetworkDriving::NotApplicable ? "완료 실적 · 해당 없음"
                                                    : "미산정";
}
QString kind(const NetworkGraph &g, const NetworkNode &node) {
    if (!node.timing)
        return "날짜/여유 미산정";
    const auto &r = g.calculation.tasks[*node.timing];
    return r.completedActual ? "실제 완료"
           : g.calculation.resourceLeveled
               ? (g.calculation.levelingFloatCalculated ? (r.critical ? "평준화 임계" : "평준화 여유")
                                                        : "평준화 · 여유/임계 미산정")
           : !hasScheduleFloat(g.calculation, r) ? "잔여 예측 · 여유/임계 미산정"
           : g.calculation.isForecast            ? (r.critical ? "예측 임계" : "잔여 예측")
                                                 : (r.critical ? "계획 임계" : "현재 계획");
}
QString floats(const NetworkGraph &g, const NetworkNode &node) {
    if (!node.timing)
        return "총/자유여유 · 임계 미산정";
    const auto &r = g.calculation.tasks[*node.timing];
    if (r.completedActual)
        return "미래 여유 해당 없음";
    if (!hasScheduleFloat(g.calculation, r))
        return "총/자유여유 · 임계 미산정";
    return "총 " + QString::number(double(r.totalFloatMinutes) / 60, 'g', 10) + "h / 자유 " +
           QString::number(double(r.freeFloatMinutes) / 60, 'g', 10) + "h";
}
QString detail(const NetworkGraph &g, std::size_t i) {
    const auto &n = g.nodes[i];
    QString text = QString("#%1  %2\nID: %3\n%4 · %5").arg(i + 1).arg(qs(n.name), qs(n.id), kind(g, n), floats(g, n));
    if (n.timing) {
        const auto &r = g.calculation.tasks[*n.timing];
        text += "\n활성 시작 " + qs(r.start) + " / 완료 " + qs(r.finish) + "\n잔여 시작 " + time(r.remainingStart);
    }
    text += "\n실제 시작 " + time(n.actualStart) + " / 실제 완료 " + time(n.actualFinish) + "\n달력 ID " +
            (n.calendarId.empty() ? QString("프로젝트 기본") : qs(n.calendarId));
    if (n.cyclic)
        text += "\n구조상 순환에 포함 · 계산 상태와 별도";
    return text;
}
class NetworkCanvas final : public QGraphicsView {
public:
    explicit NetworkCanvas(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent) {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setBackgroundBrush(QColor("#f3f6f9"));
    }
    void zoom(double value) {
        const auto scaleNow = transform().m11();
        const auto next = std::clamp(scaleNow * value, 0.08, 3.0);
        scale(next / scaleNow, next / scaleNow);
    }

protected:
    void wheelEvent(QWheelEvent *e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            zoom(e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15);
            e->accept();
        } else
            QGraphicsView::wheelEvent(e);
    }
};
class NetworkCard final : public QGraphicsItem {
    std::shared_ptr<const NetworkGraph> graph_;
    NetworkPlacedNode placed_;
    bool focus_;
    std::function<void(const std::string &)> activate_;

public:
    std::function<void(const std::string &, QPointF)> moved;

public:
    NetworkCard(std::shared_ptr<const NetworkGraph> graph, NetworkPlacedNode placed, bool focus,
                std::function<void(const std::string &)> activate)
        : graph_(std::move(graph)), placed_(placed), focus_(focus), activate_(std::move(activate)) {
        setFlags(ItemIsSelectable | ItemIsFocusable);
        setData(0, qs(graph_->nodes[placed.node].id));
        setData(1, "networkNode");
        setToolTip(tooltip(detail(*graph_, placed.node)));
        setZValue(2);
    }
    QRectF boundingRect() const override {
        return {-5, -5, cardWidth + 10, cardHeight + 10};
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        const auto &node = graph_->nodes[placed_.node];
        const auto *r = node.timing ? &graph_->calculation.tasks[*node.timing] : nullptr;
        const bool critical = r && hasScheduleFloat(graph_->calculation, *r) && r->critical;
        const auto fill = r && r->completedActual ? QColor("#e8f7ee")
                          : critical              ? QColor("#fff0ef")
                                                  : QColor("#ffffff");
        const auto border = isSelected()  ? QColor("#086da5")
                            : node.cyclic ? QColor("#8c3cad")
                            : critical    ? QColor("#c94343")
                                          : QColor("#6e8393");
        p->setPen(QPen(border, isSelected() ? 3 : focus_ ? 2 : 1, placed_.inTable ? Qt::SolidLine : Qt::DashLine));
        p->setBrush(fill);
        p->drawRoundedRect(QRectF(0, 0, cardWidth, cardHeight), 6, 6);
        auto font = printFont(QApplication::font());
        font.setPointSize(10);
        font.setBold(true);
        p->setFont(font);
        p->setPen(QColor("#162f44"));
        p->drawText(QRectF(14, 7, cardWidth - 28, 22),
                    QFontMetricsF(font).elidedText(QString::number(placed_.node + 1) + ". " + qs(node.name),
                                                   Qt::ElideRight, cardWidth - 28));
        font.setPointSize(8);
        font.setBold(false);
        p->setFont(font);
        p->drawText(QRectF(14, 32, cardWidth - 28, 18),
                    QFontMetricsF(font).elidedText(kind(*graph_, node) + (node.cyclic ? " · 구조 순환" : "") +
                                                       (!placed_.inTable ? " · 표 범위 밖" : ""),
                                                   Qt::ElideRight, cardWidth - 28));
        p->drawText(QRectF(16, 55, cardWidth - 32, 18), "시작 " + (r ? qs(r->start) : "미산정"));
        p->drawText(QRectF(16, 76, cardWidth - 32, 18), "완료 " + (r ? qs(r->finish) : "미산정"));
        p->drawText(QRectF(14, 99, cardWidth - 28, 18), floats(*graph_, node));
        p->drawText(QRectF(14, 123, cardWidth - 28, 18),
                    QString("생략 관계: 선행 %1 / 후행 %2").arg(placed_.hiddenIncoming).arg(placed_.hiddenOutgoing));
        p->setPen(QColor("#4c6374"));
        p->drawText(QRectF(2, 64, 12, 18), "S");
        p->drawText(QRectF(cardWidth - 12, 64, 12, 18), "F");
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        QGraphicsItem::mouseReleaseEvent(e);
        if (moved)
            moved(graph_->nodes[placed_.node].id, pos());
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *) override {
        activate_(graph_->nodes[placed_.node].id);
    }
};
struct PreparedNetwork {
    std::shared_ptr<const NetworkGraph> graph;
    NetworkWindow window;
    QString error;
    double graphMs = 0, windowMs = 0;
};
} // namespace
class NetworkRelationsModel final : public QAbstractTableModel {
    std::shared_ptr<const NetworkGraph> graph_;
    std::vector<std::pair<std::size_t, bool>> edges_;

public:
    explicit NetworkRelationsModel(QObject *parent) : QAbstractTableModel(parent) {}
    void set(std::shared_ptr<const NetworkGraph> graph, std::size_t node = 0) {
        beginResetModel();
        graph_ = std::move(graph);
        edges_.clear();
        if (graph_ && node < graph_->nodes.size()) {
            for (auto e : graph_->nodes[node].incoming)
                edges_.emplace_back(e, true);
            for (auto e : graph_->nodes[node].outgoing)
                edges_.emplace_back(e, false);
        }
        endResetModel();
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(edges_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 6;
    }
    QVariant headerData(int section, Qt::Orientation direction, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (direction == Qt::Vertical)
            return section + 1;
        return QStringList{"방향", "원본 번호", "작업", "관계", "지연(분)", "driving"}.value(section);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!graph_ || !index.isValid() || index.row() < 0 || index.row() >= int(edges_.size()))
            return {};
        const auto &[id, up] = edges_[index.row()];
        const auto &edge = graph_->edges[id];
        const auto other = up ? edge.from : edge.to;
        const auto &node = graph_->nodes[other];
        if (role == Qt::UserRole)
            return qs(node.id);
        if (role == Qt::ToolTipRole)
            return tooltip(detail(*graph_, other) + "\n" + lag(edge) + " / " + driving(edge.driving) +
                           (edge.releaseAt.empty() ? QString{}
                                                   : "\n실행 구간 " + qs(edge.releaseAt) + " → " + qs(edge.acquireAt)));
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return up ? "선행" : "후행";
        case 1:
            return qulonglong(other + 1);
        case 2:
            return qs(node.name);
        case 3:
            if (!edge.resourceId.empty())
                return "자원 순서 " + qs(edge.resourceId) + " / " + QString::number(edge.resourceUnits) + "%";
            return qs(enumToken(edge.type));
        case 4:
            return qlonglong(edge.lagMinutes);
        case 5:
            return driving(edge.driving);
        default:
            return {};
        }
    }
};
void syncNetworkSelection(QWidget *root, const std::string &id) {
    for (auto *dialog : root->findChildren<QDialog *>())
        if (auto *network = dynamic_cast<NetworkDialog *>(dialog))
            network->setSelection(id);
}
NetworkDialog::NetworkDialog(Document &doc, std::string initialFocus, ScopeProvider provider, QWidget *parent)
    : QDialog(parent), document_(doc), scopeProvider_(std::move(provider)), focus_(std::move(initialFocus)) {
    setObjectName("networkDialog");
    setWindowTitle("네트워크 다이어그램 · 선행/후행·driving 탐색");
    resize(1320, 900);
    auto *body = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    body->addLayout(controls);
    mode_ = new QComboBox(this);
    mode_->setObjectName("networkMode");
    mode_->addItems({"주변 관계", "전체 선행", "전체 후행", "driving 선행", "driving 후행", "전체 작업 묶음"});
    controls->addWidget(mode_);
    radius_ = new QSpinBox(this);
    radius_->setObjectName("networkRadius");
    radius_->setRange(1, 10);
    radius_->setValue(1);
    radius_->setPrefix("주변 깊이 ");
    controls->addWidget(radius_);
    limit_ = new QSpinBox(this);
    limit_->setObjectName("networkLimit");
    limit_->setRange(2, 200);
    limit_->setValue(80);
    limit_->setPrefix("카드 한도 ");
    controls->addWidget(limit_);
    visibleOnly_ = new QCheckBox("표 범위만(기준 작업 유지)", this);
    visibleOnly_->setObjectName("networkVisibleOnly");
    controls->addWidget(visibleOnly_);
    labels_ = new QCheckBox("관계 라벨", this);
    labels_->setChecked(true);
    controls->addWidget(labels_);
    controls->addStretch();
    refresh_ = new QPushButton("최신 입력 재계산", this);
    refresh_->setObjectName("refreshNetwork");
    controls->addWidget(refresh_);
    cancel_ = new QPushButton("취소", this);
    cancel_->setObjectName("cancelNetwork");
    controls->addWidget(cancel_);
    auto *navigation = new QHBoxLayout;
    body->addLayout(navigation);
    find_ = new QLineEdit(this);
    find_->setObjectName("networkFind");
    find_->setPlaceholderText("원본 번호 / 작업 ID / 이름 검색 (최대 50 후보)");
    navigation->addWidget(find_, 1);
    auto *search = new QPushButton("찾기", this);
    search->setObjectName("findNetworkTask");
    navigation->addWidget(search);
    found_ = new QComboBox(this);
    found_->setObjectName("networkFound");
    found_->setMinimumContentsLength(16);
    found_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    navigation->addWidget(found_, 1);
    auto *focus = new QPushButton("후보를 중심으로", this);
    focus->setObjectName("focusNetworkFound");
    navigation->addWidget(focus);
    auto *scope = new QPushButton("표 범위 갱신", this);
    scope->setObjectName("refreshNetworkScope");
    navigation->addWidget(scope);
    previous_ = new QPushButton("이전 묶음", this);
    previous_->setObjectName("previousNetwork");
    next_ = new QPushButton("다음 묶음", this);
    next_->setObjectName("nextNetwork");
    navigation->addWidget(previous_);
    navigation->addWidget(next_);
    summary_ = new QLabel(this);
    summary_->setObjectName("networkSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    body->addWidget(summary_);
    auto *split = new QSplitter(this);
    body->addWidget(split, 1);
    scene_ = new QGraphicsScene(this);
    view_ = new NetworkCanvas(scene_, this);
    view_->setObjectName("networkView");
    split->addWidget(view_);
    auto *side = new QWidget(this);
    auto *detailBody = new QVBoxLayout(side);
    details_ = new QPlainTextEdit(side);
    details_->setObjectName("networkDetails");
    details_->setReadOnly(true);
    details_->setMaximumHeight(245);
    detailBody->addWidget(details_);
    auto *actions = new QHBoxLayout;
    detailBody->addLayout(actions);
    center_ = new QPushButton("선택 작업을 중심으로", side);
    center_->setObjectName("centerNetworkTask");
    jump_ = new QPushButton("표·간트에서 찾기", side);
    jump_->setObjectName("jumpNetworkTask");
    jump_->setToolTip("같은 계산 결과로 표/간트를 표시합니다. 숨긴 작업은 WBS/검색/분류 필터를 해제해 선택합니다.");
    actions->addWidget(center_);
    actions->addWidget(jump_);
    relations_ = new QTableView(side);
    relations_->setObjectName("networkRelations");
    relationsModel_ = new NetworkRelationsModel(relations_);
    relations_->setModel(relationsModel_);
    relations_->setSelectionBehavior(QAbstractItemView::SelectRows);
    relations_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    relations_->setWordWrap(false);
    relations_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    relations_->setColumnWidth(0, 45);
    relations_->setColumnWidth(1, 65);
    relations_->setColumnWidth(2, 180);
    relations_->setColumnWidth(3, 42);
    relations_->setColumnWidth(4, 70);
    relations_->setColumnWidth(5, 100);
    detailBody->addWidget(relations_, 1);
    split->addWidget(side);
    split->setSizes({850, 460});
    auto *footer = new QHBoxLayout;
    body->addLayout(footer);
    for (const auto &[text, factor] : std::vector<std::pair<QString, double>>{{"확대", 1.2}, {"축소", 1 / 1.2}}) {
        auto *b = new QPushButton(text, this);
        footer->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, factor] { static_cast<NetworkCanvas *>(view_)->zoom(factor); });
    }
    auto *fit = new QPushButton("그림 맞춤", this);
    footer->addWidget(fit);
    connect(fit, &QPushButton::clicked, this, [this] { view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio); });
    auto *reset = new QPushButton("100%", this);
    footer->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        view_->resetTransform();
        setSelection(focus_);
    });
    auto *editing = new QHBoxLayout;
    body->insertLayout(body->count() - 1, editing);
    auto *edit = new QPushButton("선택 작업 관계 편집", this);
    edit->setObjectName("editNetworkRelations");
    editing->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this] { editRelations(); });
    edit->setEnabled(!document_.readOnly());
    auto *save = new QPushButton("배치 문서에 저장", this);
    save->setObjectName("saveNetworkLayout");
    editing->addWidget(save);
    connect(save, &QPushButton::clicked, this, [this] { saveLayout(); });
    save->setEnabled(!document_.readOnly());
    auto *autoLayout = new QPushButton("자동 배치", this);
    autoLayout->setObjectName("autoNetworkLayout");
    editing->addWidget(autoLayout);
    connect(autoLayout, &QPushButton::clicked, this, [this] {
        if (!current())
            return;
        for (const auto &v : window_.nodes)
            draftPositions_[graph_->nodes[v.node].id] = QPointF(v.column * columnStep, v.row * rowStep);
        render();
    });
    auto *clearLayout = new QPushButton("저장 배치 초기화", this);
    clearLayout->setObjectName("resetNetworkLayout");
    editing->addWidget(clearLayout);
    clearLayout->setEnabled(!document_.readOnly());
    connect(clearLayout, &QPushButton::clicked, this, [this] {
        if (!current() || document_.readOnly())
            return;
        try {
            savingLayout_ = true;
            document_.setNetworkPositions({});
            savingLayout_ = false;
            draftPositions_.clear();
            refresh();
        } catch (const std::exception &e) {
            savingLayout_ = false;
            summary_->setText(qErrorText(e));
        }
    });
    auto *print = new QPushButton("네트워크 미리보기·인쇄", this);
    print->setObjectName("printNetwork");
    editing->addWidget(print);
    connect(print, &QPushButton::clicked, this, [this] { printNetwork(); });
    auto *note = new QLabel("빨강 카드=엔진 임계 · 주황 선=driving(binding) · 보라=구조 순환 · 점선 카드=마지막 표 "
                            "범위 밖 · S/F 포트 · 지연 m=근무분 · Ctrl+휠 확대 · 빈 곳 드래그 이동",
                            this);
    note->setWordWrap(true);
    footer->addWidget(note, 1);
    auto *close = new QPushButton("닫기", this);
    footer->addWidget(close);
    connect(close, &QPushButton::clicked, this, &NetworkDialog::reject);
    connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancel();
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { requestWindow(); });
    connect(radius_, &QSpinBox::valueChanged, this, [this] { requestWindow(); });
    connect(limit_, &QSpinBox::valueChanged, this, [this] { requestWindow(); });
    connect(visibleOnly_, &QCheckBox::toggled, this, [this] { requestWindow(); });
    connect(labels_, &QCheckBox::toggled, this, [this] {
        for (auto *item : scene_->items())
            if (item->data(1) == "networkEdgeLabel")
                item->setVisible(labels_->isChecked());
    });
    connect(scope, &QPushButton::clicked, this, [this] {
        if (scopeProvider_)
            visible_ = scopeProvider_();
        requestWindow();
    });
    connect(previous_, &QPushButton::clicked, this, [this] {
        const auto step = std::size_t(limit_->value() - 1);
        requestWindow(window_.offset > step ? window_.offset - step : 0);
    });
    connect(next_, &QPushButton::clicked, this, [this] { requestWindow(window_.nextOffset); });
    connect(search, &QPushButton::clicked, this, [this] { find(); });
    connect(find_, &QLineEdit::returnPressed, this, [this] { find(); });
    connect(focus, &QPushButton::clicked, this,
            [this] { setSelection(found_->currentData().toString().toStdString()); });
    connect(center_, &QPushButton::clicked, this, [this] { setSelection(selected_); });
    connect(jump_, &QPushButton::clicked, this, [this] { jump(); });
    connect(relations_, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) { setSelection(index.data(Qt::UserRole).toString().toStdString()); });
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        for (auto *item : scene_->selectedItems())
            if (item->data(1) == "networkNode") {
                select(item->data(0).toString().toStdString());
                break;
            }
    });
    listener_ = document_.subscribe([this](Document::Event event) {
        if (savingLayout_)
            return;
        if (event == Document::Event::Saved) {
            if (current() && windowReady_)
                summary();
        } else
            invalidate("입력/문서가 바뀌었습니다. 최신 입력을 다시 계산하세요.");
    });
    refresh();
}
NetworkDialog::~NetworkDialog() {
    // QWidget deletes scene items after our members have been destroyed;
    // deleting a selected item can synchronously emit selectionChanged.
    disconnect(scene_, nullptr, this, nullptr);
    if (control_)
        control_->cancel();
    document_.unsubscribe(listener_);
}
void NetworkDialog::reject() {
    if (control_)
        control_->cancel();
    ++generation_;
    QDialog::reject();
}
bool NetworkDialog::current() const {
    return graph_ && document_.project() && document_.sessionGeneration() == session_ &&
           document_.project()->documentId == graph_->documentId && document_.project()->revision == graph_->revision;
}
void NetworkDialog::invalidate(const QString &message) {
    for (const auto *name : {"editNetworkRelations", "saveNetworkLayout", "resetNetworkLayout", "printNetwork"})
        if (auto *b = findChild<QPushButton *>(name))
            b->setEnabled(false);
    windowReady_ = false;
    draftPositions_.clear();
    setProperty("networkNodeCount", 0);
    setProperty("networkEdgeCount", 0);
    if (control_)
        control_->cancel();
    ++generation_;
    graph_.reset();
    relationsModel_->set({});
    scene_->clear();
    details_->clear();
    jump_->setEnabled(false);
    center_->setEnabled(false);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    cancel_->setEnabled(false);
    summary_->setText(message);
}
void NetworkDialog::refresh() {
    invalidate("전체 입력 계산과 네트워크 인덱스 준비 중…");
    if (!document_.project()) {
        summary_->setText("열린 프로젝트가 없습니다.");
        return;
    }
    if (scopeProvider_)
        visible_ = scopeProvider_();
    const auto source = document_.snapshot();
    session_ = document_.sessionGeneration();
    const auto session = session_, generation = generation_;
    control_ = std::make_shared<CalculationControl>();
    cancel_->setEnabled(true);
    auto *watcher = new QFutureWatcher<PreparedNetwork>(this);
    connect(watcher, &QFutureWatcher<PreparedNetwork>::finished, this, [this, watcher, session, generation, source] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        cancel_->setEnabled(false);
        if (control_->cancelRequested.load()) {
            summary_->setText("네트워크 준비를 취소했습니다.");
            return;
        }
        if (!document_.project() || session != document_.sessionGeneration() ||
            document_.project()->documentId != source->documentId ||
            document_.project()->revision != source->revision) {
            summary_->setText("문서가 바뀌어 이전 결과를 적용하지 않았습니다.");
            return;
        }
        if (!result.error.isEmpty()) {
            summary_->setText(result.error);
            return;
        }
        graph_ = std::move(result.graph);
        setProperty("networkBuildMs", result.graphMs);
        if (!graph_->indices.contains(focus_)) {
            const auto first = std::find_if(graph_->nodes.begin(), graph_->nodes.end(),
                                            [this](const auto &node) { return visible_.contains(node.id); });
            focus_ = first != graph_->nodes.end() ? first->id
                     : graph_->nodes.empty()      ? std::string{}
                                                  : graph_->nodes.front().id;
        }
        requestWindow();
    });
    watcher->setFuture(QtConcurrent::run([source, control = control_] {
        PreparedNetwork result;
        try {
            auto begin = std::chrono::steady_clock::now();
            result.graph = std::make_shared<const NetworkGraph>(buildNetworkGraph(*source, control));
            result.graphMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        } catch (const CalculationCancelled &) {
            result.error = "네트워크 준비 취소";
        } catch (const std::exception &e) {
            result.error = qErrorText(e);
        }
        return result;
    }));
}
void NetworkDialog::requestWindow(std::size_t offset) {
    if (!current())
        return;
    windowReady_ = false;
    setProperty("networkNodeCount", 0);
    setProperty("networkEdgeCount", 0);
    if (control_)
        control_->cancel();
    ++generation_;
    const auto generation = generation_;
    control_ = std::make_shared<CalculationControl>();
    cancel_->setEnabled(true);
    jump_->setEnabled(false);
    center_->setEnabled(false);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    relationsModel_->set({});
    scene_->clear();
    details_->clear();
    summary_->setText("표시 관계/배치 준비 중…");
    NetworkQuery query;
    query.focus = focus_;
    query.mode = static_cast<NetworkMode>(mode_->currentIndex());
    query.radius = std::size_t(radius_->value());
    query.maxNodes = std::size_t(limit_->value());
    query.offset = offset;
    query.visibleOnly = visibleOnly_->isChecked();
    query.visible = visible_;
    radius_->setEnabled(query.mode == NetworkMode::Neighbors);
    auto *watcher = new QFutureWatcher<PreparedNetwork>(this);
    connect(watcher, &QFutureWatcher<PreparedNetwork>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != generation_)
            return;
        cancel_->setEnabled(false);
        if (control_->cancelRequested.load()) {
            summary_->setText("표시 준비를 취소했습니다.");
            return;
        }
        if (!current())
            return;
        if (!result.error.isEmpty()) {
            summary_->setText(result.error);
            return;
        }
        window_ = std::move(result.window);
        windowReady_ = true;
        setProperty("networkQueryMs", result.windowMs);
        render();
    });
    watcher->setFuture(QtConcurrent::run([graph = graph_, query, control = control_] {
        PreparedNetwork result;
        try {
            auto begin = std::chrono::steady_clock::now();
            result.window = networkWindow(*graph, query, control);
            result.windowMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        } catch (const CalculationCancelled &) {
            result.error = "표시 준비 취소";
        } catch (const std::exception &e) {
            result.error = qErrorText(e);
        }
        return result;
    }));
}
void NetworkDialog::render() {
    if (!current() || !windowReady_)
        return;
    QElapsedTimer timer;
    timer.start();
    QSignalBlocker block(scene_);
    scene_->clear();
    std::unordered_map<std::size_t, QPointF> points;
    for (const auto &placed : window_.nodes) {
        auto *item =
            new NetworkCard(graph_, placed, graph_->nodes[placed.node].id == focus_, [this](const std::string &id) {
                select(id);
                jump();
            });
        QPointF point(placed.column * columnStep, placed.row * rowStep);
        const auto id = graph_->nodes[placed.node].id;
        for (const auto &v : document_.project()->networkPositions)
            if (v.taskId == id) {
                point = QPointF(v.x, v.y);
                break;
            }
        if (draftPositions_.contains(id))
            point = draftPositions_.at(id);
        item->setFlag(QGraphicsItem::ItemIsMovable, !document_.readOnly());
        item->moved = [this](const std::string &id, QPointF point) {
            if (!current() || document_.readOnly())
                return;
            draftPositions_[id] = point;
            QTimer::singleShot(0, this, [this] { render(); });
        };
        item->setPos(point);
        points.emplace(placed.node, point);
        scene_->addItem(item);
    }
    for (std::size_t e = 0; e < window_.edges.size(); ++e) {
        const auto &edge = graph_->edges[window_.edges[e]];
        const auto from = points.at(edge.from), to = points.at(edge.to);
        const bool sourceFinish = edge.type == DependencyType::FS || edge.type == DependencyType::FF,
                   targetStart = edge.type == DependencyType::FS || edge.type == DependencyType::SS;
        QPointF a = from + QPointF(sourceFinish ? cardWidth : 0, cardHeight / 2),
                b = to + QPointF(targetStart ? 0 : cardWidth, cardHeight / 2);
        const auto c = edge.cyclic                           ? QColor("#8c3cad")
                       : edge.driving == NetworkDriving::Yes ? QColor("#c87000")
                                                             : QColor("#7b8d9b");
        QPainterPath path(a);
        QPointF label;
        if (edge.type == DependencyType::FS && to.x() - from.x() == columnStep && !edge.cyclic) {
            const double middle = (a.x() + b.x()) / 2;
            path.lineTo(middle, a.y());
            path.lineTo(middle, b.y());
            path.lineTo(b);
            label = QPointF(middle, (a.y() + b.y()) / 2 - 18);
        } else {
            const auto roof = std::min(from.y(), to.y()) - 24 - double(e % 4) * 11;
            const QPointF outerA(a.x() + (sourceFinish ? 16 : -16), a.y()),
                outerB(b.x() + (targetStart ? -16 : 16), b.y());
            path.lineTo(outerA);
            path.lineTo(outerA.x(), roof);
            path.lineTo(outerB.x(), roof);
            path.lineTo(outerB);
            path.lineTo(b);
            label = QPointF((outerA.x() + outerB.x()) / 2, roof - 16);
        }
        auto *line = scene_->addPath(
            path, QPen(c, edge.driving == NetworkDriving::Yes ? 2 : 1,
                       (edge.cyclic || edge.driving == NetworkDriving::Inactive || !edge.resourceId.empty())
                           ? Qt::DashLine
                           : Qt::SolidLine));
        line->setData(1, "networkEdge");
        line->setData(0, qulonglong(window_.edges[e]));
        const auto tip =
            qs(graph_->nodes[edge.from].name) + " → " + qs(graph_->nodes[edge.to].name) + "\n" + lag(edge) + " / " +
            driving(edge.driving) +
            (edge.releaseAt.empty() ? QString{} : "\n실행 구간 " + qs(edge.releaseAt) + " → " + qs(edge.acquireAt));
        line->setToolTip(tooltip(tip));
        const double sign = targetStart ? -1 : 1;
        auto *arrow =
            scene_->addPolygon(QPolygonF{b, b + QPointF(sign * 9, -4), b + QPointF(sign * 9, 4)}, QPen(c), QBrush(c));
        arrow->setToolTip(tooltip(tip));
        if (window_.edges.size() <= 120 || edge.driving == NetworkDriving::Yes ||
            graph_->nodes[edge.from].id == focus_ || graph_->nodes[edge.to].id == focus_) {
            auto font = printFont(QApplication::font());
            font.setPointSize(8);
            auto *text = scene_->addSimpleText(lag(edge), font);
            text->setData(1, "networkEdgeLabel");
            text->setVisible(labels_->isChecked());
            text->setBrush(c);
            text->setPos(label.x() - text->boundingRect().width() / 2, label.y());
            text->setToolTip(tooltip(tip));
        }
    }
    if (window_.nodes.empty())
        scene_->addText("표시할 작업이 없습니다.");
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-35, -35, 35, 35));
    view_->setSceneRect(scene_->sceneRect());
    for (auto *item : scene_->items())
        if (item->data(1) == "networkNode" && item->data(0).toString() == qs(focus_)) {
            item->setSelected(true);
            view_->centerOn(item);
            break;
        }
    select(focus_);
    previous_->setEnabled(mode_->currentIndex() == int(NetworkMode::All) && window_.offset > 0);
    next_->setEnabled(mode_->currentIndex() == int(NetworkMode::All) && window_.nextOffset + 1 < window_.candidates);
    setProperty("networkRenderMs", double(timer.nsecsElapsed()) / 1000000);
    setProperty("networkNodeCount", int(window_.nodes.size()));
    setProperty("networkEdgeCount", int(window_.edges.size()));
    for (const auto *name : {"editNetworkRelations", "saveNetworkLayout", "resetNetworkLayout"})
        if (auto *b = findChild<QPushButton *>(name))
            b->setEnabled(!document_.readOnly());
    if (auto *b = findChild<QPushButton *>("printNetwork"))
        b->setEnabled(true);
    summary();
}
void NetworkDialog::summary() {
    if (!current())
        return;
    const auto &c = graph_->calculation;
    QString text = QString("전체 %1 작업 / %2 관계 · 표시 %3 카드 / %4 선 · 한도/묶음 생략 %5 작업 / %6 선 · 표 범위 "
                           "제외 %7 · rev %8 · %9")
                       .arg(graph_->nodes.size())
                       .arg(graph_->edges.size())
                       .arg(window_.nodes.size())
                       .arg(window_.edges.size())
                       .arg(window_.omittedNodes)
                       .arg(window_.omittedEdges)
                       .arg(window_.scopeExcluded)
                       .arg(graph_->revision)
                       .arg(document_.revisionSaved() ? "저장된 입력" : "미저장 입력");
    text += "\n" + qs(c.engineVersion) + " · " + qs(c.calculationPolicy) + " · " +
            (c.status == CalculationStatus::Valid
                 ? c.resourceLeveled ? (c.levelingFloatCalculated ? "평준화 · 자원 순서 여유/임계/driving"
                                                                  : "평준화 · 여유/임계/driving 미산정")
                   : c.isForecast    ? "잔여 예측 · 완료 실적의 미래 여유 제외"
                                     : "현재 계획"
                 : "계산 불완전/충돌 · 날짜/여유/driving 미산정");
    text += QString(" · 구조 순환 %1묶음 · 미확인 선행 %2").arg(graph_->cycleComponents).arg(graph_->unknownRelations);
    if (window_.drivingUnavailable)
        text += "\n현재 결과는 driving 미산정입니다. 관계가 없다는 뜻이 아닙니다.";
    text += QString("\n탐색 후보 %1 · 그림 밖 %2 작업 / %3 관계 · 부분 그림에서 여유/임계를 다시 계산하지 않습니다.")
                .arg(window_.candidates)
                .arg(graph_->nodes.size() - window_.nodes.size())
                .arg(graph_->edges.size() - window_.edges.size());
    summary_->setText(text);
    QStringList notes;
    for (const auto &d : c.diagnostics)
        notes << qs(d.code) + ": " + qs(diagnosticMessage(d));
    for (const auto &w : graph_->warnings)
        notes << qs(w);
    summary_->setToolTip(tooltip(notes.join('\n')));
}
void NetworkDialog::select(const std::string &id) {
    if (!current() || !windowReady_ || !graph_->indices.contains(id))
        return;
    selected_ = id;
    const auto i = graph_->indices.at(id);
    details_->setPlainText(detail(*graph_, i) +
                           QString("\n전체 직접 관계: 선행 %1 / 후행 %2 · 더블클릭으로 이웃을 중심 이동")
                               .arg(graph_->nodes[i].incoming.size())
                               .arg(graph_->nodes[i].outgoing.size()));
    relationsModel_->set(graph_, i);
    jump_->setEnabled(true);
    center_->setEnabled(true);
}
void NetworkDialog::setSelection(const std::string &id) {
    if (id.empty())
        return;
    if (id == focus_) {
        for (auto *item : scene_->items())
            if (item->data(1) == "networkNode" && item->data(0).toString() == qs(id)) {
                view_->centerOn(item);
                break;
            }
        return;
    }
    focus_ = id;
    if (current() && graph_->indices.contains(id))
        requestWindow();
}
void NetworkDialog::jump() {
    if (windowReady_ && current() && graph_->indices.contains(selected_) && taskActivated)
        taskActivated(selected_, graph_->calculation);
}
void NetworkDialog::find() {
    if (!current())
        return;
    const auto text = find_->text().trimmed();
    found_->clear();
    if (text.isEmpty())
        return;
    bool number = false;
    const auto index = text.toULongLong(&number);
    std::size_t matches = 0;
    for (std::size_t i = 0; i < graph_->nodes.size(); ++i) {
        const auto &node = graph_->nodes[i];
        if ((number && index == i + 1) || qs(node.id) == text ||
            (!number && qs(node.name).contains(text, Qt::CaseInsensitive))) {
            if (matches++ < 50)
                found_->addItem(QString::number(i + 1) + ". " + qs(node.name).left(80), qs(node.id));
        }
    }
    found_->setToolTip(QString("검색 일치 %1개 · 최대 50개 후보 · 정확한 원본 번호/ID로 이동 가능").arg(matches));
}
} // namespace ppm
