#include "ui/MaterialActualDialog.h"
#include "application/Document.h"
#include "domain/MaterialActuals.h"
#include "domain/ResourcePlan.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <set>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QString quantity(std::int64_t n) {
    return (n < 0 ? "-" : "") + q(formatMaterialQuantity(n < 0 ? -n : n));
}
std::int64_t parseQuantity(const QString &s) {
    const auto text = s.trimmed();
    if (!QRegularExpression("^[0-9]+(?:\\.[0-9]{1,3})?$").match(text).hasMatch())
        throw AppError(Diagnostic{"MATERIAL_ACTUAL_INVALID",
                                  {"수량은 소수 3자리까지 입력하세요. 빈칸은 미확인입니다."},
                                  {},
                                  "MATERIAL_ACTUAL_INVALID"});
    auto parts = text.split('.');
    bool ok = false;
    auto whole = parts[0].toLongLong(&ok);
    if (!ok || whole > 1000000000LL)
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    auto n = whole * 1000 + (parts.size() == 2 ? parts[1].leftJustified(3, '0').toLongLong() : 0);
    if (n > 1000000000000LL)
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    return n;
}
QString kind(const std::string &s) {
    return s == "receipt" ? "입고" : s == "consume" ? "소모" : s == "remaining" ? "잔여 소모 확인" : "재고 확인";
}
QString cell(QTableWidget *t, int r, int c) {
    return t->item(r, c) ? t->item(r, c)->text() : QString{};
}
void put(QTableWidget *t, int r, int c, const QString &value, bool editable = false) {
    auto *i = new QTableWidgetItem(value);
    if (!editable)
        i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    t->setItem(r, c, i);
}
class HistoryModel final : public QAbstractTableModel {
public:
    std::shared_ptr<const Project> source;
    using QAbstractTableModel::QAbstractTableModel;
    void reset(std::shared_ptr<const Project> p) {
        beginResetModel();
        source = std::move(p);
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() || !source ? 0 : int(source->materialActuals.size());
    }
    int columnCount(const QModelIndex & = {}) const override {
        return 12;
    }
    const MaterialActualEntry &entry(int row) const {
        return source->materialActuals.at(source->materialActuals.size() - 1 - std::size_t(row));
    }
    QVariant headerData(int n, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        return QStringList{"기록 ID", "종류", "자재",   "작업/소모", "계획 입고", "실제/확인 시각",
                           "수량",    "단위", "상태일", "작성자",    "사유",      "이전 기록"}
            .at(n);
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        const auto &e = entry(i.row());
        return QStringList{q(e.id),
                           kind(e.kind),
                           q(e.materialId),
                           q(e.useId.empty() ? e.taskId : e.useId),
                           q(e.receiptId),
                           q(e.at),
                           quantity(e.quantityMilli),
                           q(e.unit),
                           q(e.statusAt),
                           q(e.reporter),
                           q(e.reason),
                           q(e.previousEntryId)}
            .at(i.column());
    }
};
} // namespace
struct MaterialActualDialog::Impl {
    Document &document;
    MaterialActualDialog &dialog;
    std::shared_ptr<const Project> source;
    std::uint64_t session = 0;
    std::uint64_t listener = 0;
    bool stale = false;
    QComboBox *material, *type, *task, *receipt;
    QLineEdit *at, *amount, *reporter, *reason;
    QPushButton *submit, *confirm, *fresh;
    QLabel *error, *basis;
    QTableView *history;
    HistoryModel *model;
    QTableWidget *stocks, *remaining;
    std::optional<MaterialActualEntry> correction;
    Impl(Document &d, MaterialActualDialog &owner) : document(d), dialog(owner) {
        auto *layout = new QVBoxLayout(&dialog);
        basis = new QLabel;
        basis->setWordWrap(true);
        basis->setObjectName("materialActualBasis");
        layout->addWidget(basis);
        auto *tabs = new QTabWidget;
        layout->addWidget(tabs, 1);
        auto *movements = new QWidget;
        auto *movementsLayout = new QVBoxLayout(movements);
        history = new QTableView;
        history->setObjectName("materialActualHistory");
        model = new HistoryModel(history);
        history->setModel(model);
        history->setSelectionBehavior(QAbstractItemView::SelectRows);
        history->setSelectionMode(QAbstractItemView::SingleSelection);
        history->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        movementsLayout->addWidget(history, 1);
        auto *form = new QFormLayout;
        movementsLayout->addLayout(form);
        material = new QComboBox;
        material->setObjectName("materialActualMaterial");
        form->addRow("자재", material);
        type = new QComboBox;
        type->setObjectName("materialActualKind");
        type->addItem("실제 입고", "receipt");
        type->addItem("실제 소모", "consume");
        form->addRow("종류", type);
        task = new QComboBox;
        task->setObjectName("materialActualTask");
        form->addRow("소모 작업 (선택)", task);
        receipt = new QComboBox;
        receipt->setObjectName("materialActualReceipt");
        form->addRow("연결할 계획 입고 (선택)", receipt);
        at = new QLineEdit;
        at->setObjectName("materialActualAt");
        at->setPlaceholderText("YYYY-MM-DD HH:mm");
        form->addRow("실제 시각", at);
        amount = new QLineEdit;
        amount->setObjectName("materialActualQuantity");
        amount->setPlaceholderText("자재 단위 · 소수 3자리 · 0은 정정 취소");
        form->addRow("수량", amount);
        auto *actions = new QHBoxLayout;
        movementsLayout->addLayout(actions);
        fresh = new QPushButton("새 거래");
        fresh->setObjectName("newMaterialActual");
        actions->addWidget(fresh);
        submit = new QPushButton("입고·소모 제출 / 선택 거래 정정");
        submit->setObjectName("submitMaterialActual");
        actions->addWidget(submit);
        tabs->addTab(movements, "실제 입고·소모 원장");
        auto *reconcile = new QWidget;
        auto *reconcileLayout = new QVBoxLayout(reconcile);
        auto *note =
            new QLabel("원장 반영 후 현재 상태일의 재고와 앞으로 필요한 소모량을 직접 확인해 제출합니다. 계획 소모가 "
                       "있는 모든 자재/소모행이 필요합니다. 0은 명시적인 잔여 없음이며 빈칸은 미확인입니다. 계획 입고 "
                       "시각이 상태일 이하인 미입고 물량은 미래 공급으로 추정하지 않습니다.");
        note->setWordWrap(true);
        reconcileLayout->addWidget(note);
        stocks = new QTableWidget(0, 7);
        stocks->setObjectName("materialActualStocks");
        stocks->setHorizontalHeaderLabels(
            {"자재 ID", "단위", "개시 재고", "실제 입고", "실제 소모", "계산 재고", "확인 재고 (입력)"});
        stocks->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        reconcileLayout->addWidget(stocks, 1);
        remaining = new QTableWidget(0, 5);
        remaining->setObjectName("materialRemainingUses");
        remaining->setHorizontalHeaderLabels({"소모 ID", "작업", "자재", "총 계획 소모", "잔여 소모 (입력)"});
        remaining->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        reconcileLayout->addWidget(remaining, 1);
        confirm = new QPushButton("상태일 재고·잔여 소모 확인 제출");
        confirm->setObjectName("confirmMaterialStock");
        reconcileLayout->addWidget(confirm);
        tabs->addTab(reconcile, "상태일 재고·잔여 소모 확인");
        auto *audit = new QFormLayout;
        layout->addLayout(audit);
        reporter = new QLineEdit;
        reporter->setObjectName("materialActualReporter");
        audit->addRow("작성자 (필수)", reporter);
        reason = new QLineEdit;
        reason->setObjectName("materialActualReason");
        audit->addRow("사유 (필수)", reason);
        error = new QLabel;
        error->setWordWrap(true);
        error->setTextFormat(Qt::PlainText);
        error->setObjectName("materialActualError");
        layout->addWidget(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        buttons->button(QDialogButtonBox::Close)->setText("닫기");
        layout->addWidget(buttons);
        auto *reload = buttons->addButton("현재 문서 다시 불러오기", QDialogButtonBox::ActionRole);
        reload->setObjectName("reloadMaterialActual");
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(reload, &QPushButton::clicked, &dialog, [this] { load(); });
        QObject::connect(material, &QComboBox::currentIndexChanged, &dialog, [this] { fillReceipts(); });
        QObject::connect(type, &QComboBox::currentIndexChanged, &dialog, [this] {
            task->setEnabled(!document.readOnly() && type->currentData() == "consume");
            receipt->setEnabled(!document.readOnly() && type->currentData() == "receipt");
        });
        QObject::connect(fresh, &QPushButton::clicked, &dialog, [this] {
            correction.reset();
            history->clearSelection();
            amount->clear();
        });
        QObject::connect(history->selectionModel(), &QItemSelectionModel::currentRowChanged, &dialog,
                         [this](const QModelIndex &i) {
                             if (!i.isValid())
                                 return;
                             const auto &e = model->entry(i.row());
                             if (e.kind != "receipt" && e.kind != "consume") {
                                 error->setText("재고·잔여 소모는 확인 탭에서 새로 제출하세요.");
                                 return;
                             }
                             correction = e;
                             material->setCurrentIndex(material->findData(q(e.materialId)));
                             type->setCurrentIndex(type->findData(q(e.kind)));
                             task->setCurrentIndex(task->findData(q(e.taskId)));
                             receipt->setCurrentIndex(receipt->findData(q(e.receiptId)));
                             at->setText(q(e.at));
                             amount->setText(quantity(e.quantityMilli));
                         });
        QObject::connect(submit, &QPushButton::clicked, &dialog, [this] {
            try {
                checkSource();
                auto s = submission();
                MaterialActualEntry e = correction.value_or(MaterialActualEntry{});
                e.kind = type->currentData().toString().toStdString();
                e.materialId = material->currentData().toString().toStdString();
                e.at = at->text().trimmed().toStdString();
                e.quantityMilli = parseQuantity(amount->text());
                e.taskId = e.kind == "consume" ? task->currentData().toString().toStdString() : "";
                e.receiptId = e.kind == "receipt" ? receipt->currentData().toString().toStdString() : "";
                if (correction)
                    e.previousEntryId = correction->id;
                s.entries.push_back(e);
                document.recordMaterialActuals(std::move(s));
                load();
                error->setText("입고·소모 원장을 제출했습니다. 정정은 이전 기록을 보존합니다.");
            } catch (const std::exception &e) {
                showEditorError(*error, e);
            }
        });
        QObject::connect(confirm, &QPushButton::clicked, &dialog, [this] {
            try {
                checkSource();
                auto s = submission();
                const auto values = actualMaterialStock(*source, *source->statusAt);
                for (int r = 0; r < stocks->rowCount(); ++r) {
                    MaterialActualEntry e;
                    e.materialId = cell(stocks, r, 0).toStdString();
                    e.kind = "confirm";
                    e.at = *source->statusAt;
                    e.quantityMilli = parseQuantity(cell(stocks, r, 6));
                    const auto &stock = *std::find_if(values.begin(), values.end(),
                                                      [&](const auto &v) { return v.materialId == e.materialId; });
                    if (!stock.remaining || *stock.remaining != e.quantityMilli)
                        throw AppError(Diagnostic{
                            "MATERIAL_ACTUAL_INVALID",
                            {"확인 재고가 원장 계산 재고와 다릅니다. 개시 재고/실제 거래를 먼저 정정하세요."},
                            {},
                            "MATERIAL_ACTUAL_INVALID"});
                    s.entries.push_back(e);
                }
                for (int r = 0; r < remaining->rowCount(); ++r) {
                    MaterialActualEntry e;
                    e.useId = cell(remaining, r, 0).toStdString();
                    e.taskId = cell(remaining, r, 1).toStdString();
                    e.materialId = cell(remaining, r, 2).toStdString();
                    e.kind = "remaining";
                    e.at = *source->statusAt;
                    e.quantityMilli = parseQuantity(cell(remaining, r, 4));
                    s.entries.push_back(e);
                }
                document.recordMaterialActuals(std::move(s));
                load();
                error->setText("현재 상태일의 재고·잔여 소모를 확인했습니다. 잔여 자동 대기 옵션을 켜고 평준화 비교 후 "
                               "채택하세요.");
            } catch (const std::exception &e) {
                showEditorError(*error, e);
            }
        });
        listener = document.subscribe([this](auto) {
            if (source && document.project() && document.sessionGeneration() == session &&
                document.project()->revision == source->revision)
                return;
            stale = true;
            submit->setEnabled(false);
            confirm->setEnabled(false);
            error->setText("문서가 변경되었습니다. 현재 문서를 다시 불러온 뒤 검토하세요.");
        });
        load();
    }
    ~Impl() {
        document.unsubscribe(listener);
    }
    void checkSource() {
        if (stale || !source || document.sessionGeneration() != session || !document.project() ||
            document.project()->revision != source->revision)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        if (!source->statusAt)
            throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    }
    MaterialActualSubmission submission() const {
        MaterialActualSubmission s;
        s.reporter = reporter->text().trimmed().toStdString();
        s.reason = reason->text().trimmed().toStdString();
        s.recordedAt = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'").toStdString();
        return s;
    }
    void fillReceipts() {
        receipt->clear();
        receipt->addItem("연결 없음 (계획 입고 잔량은 줄이지 않음)", QString{});
        if (source)
            for (const auto &r : source->resourcePlan.receipts)
                if (q(r.materialId) == material->currentData().toString())
                    receipt->addItem(q(r.id + " · " + r.at), q(r.id));
    }
    void load() {
        source = document.snapshot();
        session = document.sessionGeneration();
        stale = false;
        correction.reset();
        error->clear();
        if (!source)
            return;
        basis->setText("상태일: " + q(source->statusAt.value_or("미설정")) + " · revision " +
                       QString::number(source->revision) +
                       "\n실제 거래와 확인은 추가 전용입니다. 수량 정정은 0으로 취소할 수 있습니다. 단위·개시 "
                       "재고·기준 시각은 거래 기록 후 고정되며 실제 원가를 자동 생성하지 않습니다.");
        material->clear();
        for (const auto &m : source->resourcePlan.materials)
            material->addItem(q(m.name + " · " + m.unit + " · " + m.id), q(m.id));
        task->clear();
        task->addItem("작업 연결 없음", QString{});
        for (const auto &t : source->tasks)
            task->addItem(q(t.name + " · " + t.id), q(t.id));
        fillReceipts();
        at->setText(q(source->statusAt.value_or(source->startDate + " 09:00")));
        amount->clear();
        model->reset(source);
        stocks->setRowCount(0);
        remaining->setRowCount(int(std::min<std::size_t>(source->resourcePlan.uses.size(), 10000)));
        const auto stamp = source->statusAt.value_or(source->startDate + " 09:00");
        std::map<std::string, const MaterialActualEntry *> latest;
        for (const auto &e : source->materialActuals)
            if (e.statusAt == stamp && (e.kind == "remaining" || e.kind == "confirm"))
                latest[e.kind == "remaining" ? "u:" + e.useId : "m:" + e.materialId] = &e;
        std::set<std::string> used;
        for (int r = 0; r < remaining->rowCount(); ++r) {
            const auto &u = source->resourcePlan.uses[std::size_t(r)];
            used.insert(u.materialId);
            put(remaining, r, 0, q(u.id));
            put(remaining, r, 1, q(u.taskId));
            put(remaining, r, 2, q(u.materialId));
            put(remaining, r, 3, quantity(u.quantityMilli));
            put(remaining, r, 4,
                latest.contains("u:" + u.id) ? quantity(latest.at("u:" + u.id)->quantityMilli) : QString{},
                !document.readOnly());
        }
        for (const auto &s : actualMaterialStock(*source, stamp))
            if (used.contains(s.materialId)) {
                int r = stocks->rowCount();
                stocks->insertRow(r);
                put(stocks, r, 0, q(s.materialId));
                put(stocks, r, 1, q(s.unit));
                put(stocks, r, 2, s.opening ? quantity(*s.opening) : "미정");
                put(stocks, r, 3, quantity(s.received));
                put(stocks, r, 4, quantity(s.consumed));
                put(stocks, r, 5, s.remaining ? quantity(*s.remaining) : "미정");
                put(stocks, r, 6, s.confirmed ? quantity(*s.remaining) : QString{}, !document.readOnly());
            }
        const bool writable = !document.readOnly() && source->statusAt.has_value();
        submit->setEnabled(writable);
        confirm->setEnabled(writable && !source->resourcePlan.uses.empty() &&
                            source->resourcePlan.uses.size() <= 10000);
        if (source->resourcePlan.uses.size() > 10000)
            error->setText("잔여 소모 확인 편집은 10,000행까지 지원합니다. 부분 확인은 제출하지 않습니다.");
        fresh->setEnabled(writable);
        for (auto *w : {material, type, task, receipt})
            w->setEnabled(writable);
        for (auto *w : {at, amount, reporter, reason})
            w->setReadOnly(!writable);
        task->setEnabled(writable && type->currentData() == "consume");
        receipt->setEnabled(writable && type->currentData() == "receipt");
    }
};
MaterialActualDialog::MaterialActualDialog(Document &d, QWidget *parent) : QDialog(parent) {
    setObjectName("materialActualDialog");
    setWindowTitle("실제 자재 입고·소모 원장");
    resize(1120, 820);
    impl_ = std::make_unique<Impl>(d, *this);
    prepareEditorDialog(*this);
}
MaterialActualDialog::~MaterialActualDialog() = default;
} // namespace jobs
