#include "ui/ResourcePlanDialog.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/ResourcePlan.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>
#include <array>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string s(const QString &v) {
    return v.toUtf8().toStdString();
}
QString id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString value(QTableWidget *t, int r, int c) {
    const auto *i = t->item(r, c);
    return i ? i->text().trimmed() : QString{};
}
std::string ref(QTableWidget *t, int r, int c) {
    return s(qobject_cast<QComboBox *>(t->cellWidget(r, c))->currentData().toString());
}
std::optional<std::int64_t> integer(const QString &text) {
    if (text.isEmpty() || text == "?")
        return {};
    bool ok = false;
    const auto v = text.toLongLong(&ok);
    if (!ok || v < 0 || v > maxMoney)
        throw AppError(Diagnostic{
            "RESOURCE_PLAN_INVALID", {"수량은 소수 3자리, 금액·배정률은 허용 범위의 정수로 입력하세요."}, {}});
    return v;
}
std::optional<std::int64_t> quantity(const QString &text) {
    if (text.isEmpty() || text == "?")
        return {};
    const auto v = parseFieldNumber(s(text));
    if (v < 0 || v % 1000 || v / 1000 > 1000000000000LL)
        throw AppError(Diagnostic{
            "RESOURCE_PLAN_INVALID", {"수량은 소수 3자리, 금액·배정률은 허용 범위의 정수로 입력하세요."}, {}});
    return v / 1000;
}
QString qty(std::optional<std::int64_t> v) {
    return v ? q(formatFieldNumber(*v * 1000)) : QString{};
}
QString money(std::optional<std::int64_t> v) {
    return v ? QString::number(*v) : QString{};
}
using Choices = std::vector<std::pair<QString, QString>>;
QComboBox *choice(QTableWidget *t, int r, int c, const Choices &items, const QString &selected) {
    auto *box = new QComboBox(t);
    for (const auto &[label, key] : items)
        box->addItem(label, key);
    const auto index = box->findData(selected);
    if (index >= 0)
        box->setCurrentIndex(index);
    else if (!selected.isEmpty()) {
        box->addItem("참조 없음 [" + selected + "]", selected);
        box->setCurrentIndex(box->count() - 1);
    }
    t->setCellWidget(r, c, box);
    return box;
}
void cell(QTableWidget *t, int r, int c, const QString &text) {
    if (auto *item = t->item(r, c))
        item->setText(text);
    else
        t->setItem(r, c, new QTableWidgetItem(text));
}
QString identity(QTableWidget *t, int r) {
    return t->item(r, 0)->data(Qt::UserRole).toString();
}
} // namespace
ResourcePlanDialog::ResourcePlanDialog(Document &document, QWidget *parent) : QDialog(parent) {
    setObjectName("resourcePlanDialog");
    setWindowTitle("자재·비용계정·역할 계획");
    resize(1040, 720);
    const auto snapshot = document.project();
    if (!snapshot)
        return;
    const auto session = document.sessionGeneration(), revision = snapshot->revision;
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "수량: 소수 3자리 · 금액: " + q(snapshot->currency) +
            " 최소 단위 정수 · 빈 단가·재고·금액은 미정입니다. 개시 시각이 비면 프로젝트 시작일09:00입니다.\n"
            "비용은 작업 고정비·자원비에 추가 합산합니다. 고정비가 없다면 원가 메뉴에서 명시적으로 0을 입력하세요.\n"
            "역할 수요는 공수 계산 프로필을 사용합니다. 역할 연결은 실제 배정을 관리합니다. 기존 수동 배정과 중복 "
            "연결할 수 없습니다. 모든 변경은 함께 "
            "저장됩니다.",
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *automatic = new QCheckBox("평준화 시 자재 입고 대기에 맞춰 작업 날짜 자동 지연", this);
    automatic->setObjectName("autoDelayForReceipts");
    automatic->setChecked(snapshot->resourcePlan.autoDelayForReceipts);
    automatic->setEnabled(!document.readOnly());
    automatic->setToolTip("계획 일정의 평준화 비교·채택에 적용합니다. 작업 우선순위대로 재고를 예약하며 실제 소모·잔여 "
                          "예측은 추정하지 않습니다.");
    layout->addWidget(automatic);
    auto *remainingAutomatic = new QCheckBox("잔여 평준화에 확인된 실제 자재 재고·잔여 소모 적용", this);
    remainingAutomatic->setObjectName("autoDelayRemainingForReceipts");
    remainingAutomatic->setChecked(snapshot->resourcePlan.autoDelayRemainingForReceipts);
    remainingAutomatic->setEnabled(!document.readOnly());
    remainingAutomatic->setToolTip("실제 자재 원장에서 현재 상태일 재고·잔여 소모를 확인해 제출한 뒤 비교·채택합니다.");
    layout->addWidget(remainingAutomatic);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);
    auto sharedTables = std::make_shared<std::array<QTableWidget *, 6>>();
    auto &tables = *sharedTables;
    const std::array<QString, 6> names = {"자재", "작업별 자재 소모", "비용·계정",
                                          "역할", "역할 수요·연결",   "계획 입고"};
    const std::array<QStringList, 6> headers = {
        QStringList{"이름", "단위", "단가", "개시 재고", "개시 시각"}, QStringList{"작업", "자재", "수량", "발생 방식"},
        QStringList{"항목", "작업", "비용계정", "금액", "발생 방식"},  QStringList{"역할 이름", "종류"},
        QStringList{"작업", "역할", "구체 자원", "배정률(%)"},         QStringList{"자재", "입고 시각", "입고 수량"}};
    const Choices accruals = {{"시작", "start"}, {"완료", "finish"}, {"균등", "uniform"}};
    Choices tasks, resources = {{"미연결 (일정 미완성)", ""}};
    for (const auto &t : snapshot->tasks)
        tasks.emplace_back(q(t.name) + " [" + q(t.id) + "]", q(t.id));
    for (const auto &r : snapshot->resources)
        resources.emplace_back(q(r.name) + " [" + q(r.id) + "]", q(r.id));
    const auto catalog = [sharedTables](int n) {
        auto *t = (*sharedTables)[n];
        Choices result;
        for (int r = 0; r < t->rowCount(); ++r)
            result.emplace_back(value(t, r, 0), identity(t, r));
        return result;
    };
    const auto append = [sharedTables](int n, const QString &key) {
        auto *t = (*sharedTables)[n];
        const int r = t->rowCount();
        t->insertRow(r);
        for (int c = 0; c < t->columnCount(); ++c)
            cell(t, r, c, {});
        t->item(r, 0)->setData(Qt::UserRole, key);
        return r;
    };
    const auto row = [sharedTables, append, catalog, tasks, resources, accruals,
                      start = q(snapshot->startDate) + " 09:00"](int n) {
        const int r = append(n, id());
        auto *t = (*sharedTables)[n];
        if (n == 0) {
            cell(t, r, 0, "새 자재");
            cell(t, r, 1, "개");
        }
        if (n == 1) {
            choice(t, r, 0, tasks, {});
            choice(t, r, 1, catalog(0), {});
            cell(t, r, 2, "1");
            choice(t, r, 3, accruals, "start");
        }
        if (n == 2) {
            cell(t, r, 0, "새 비용");
            choice(t, r, 1, tasks, {});
            cell(t, r, 2, "직접비");
            choice(t, r, 4, accruals, "uniform");
        }
        if (n == 3) {
            cell(t, r, 0, "새 역할");
            choice(t, r, 1, {{"인력", "labor"}, {"장비", "equipment"}}, "labor");
        }
        if (n == 4) {
            choice(t, r, 0, tasks, {});
            choice(t, r, 1, catalog(3), {});
            choice(t, r, 2, resources, "");
            cell(t, r, 3, "100");
        }
        if (n == 5) {
            choice(t, r, 0, catalog(0), {});
            cell(t, r, 1, start);
            cell(t, r, 2, "1");
        }
        t->selectRow(r);
    };
    for (int n = 0; n < 6; ++n) {
        auto *page = new QWidget(tabs);
        auto *box = new QVBoxLayout(page);
        auto *t = new QTableWidget(0, headers[n].size(), page);
        tables[n] = t;
        t->setObjectName("resourcePlanTable" + QString::number(n));
        t->setHorizontalHeaderLabels(headers[n]);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        box->addWidget(t, 1);
        auto *buttons = new QHBoxLayout;
        box->addLayout(buttons);
        auto *add = new QPushButton("추가", page);
        add->setObjectName("resourcePlanAdd" + QString::number(n));
        buttons->addWidget(add);
        auto *remove = new QPushButton("선택 행 삭제", page);
        remove->setObjectName("resourcePlanRemove" + QString::number(n));
        buttons->addWidget(remove);
        connect(add, &QPushButton::clicked, this, [=] { row(n); });
        connect(remove, &QPushButton::clicked, this, [t] {
            if (t->currentRow() >= 0)
                t->removeRow(t->currentRow());
        });
        add->setEnabled(!document.readOnly());
        remove->setEnabled(!document.readOnly());
        t->setEnabled(!document.readOnly());
        tabs->addTab(page, names[n]);
    }
    for (const auto &m : snapshot->resourcePlan.materials) {
        auto r = append(0, q(m.id));
        cell(tables[0], r, 0, q(m.name));
        cell(tables[0], r, 1, q(m.unit));
        cell(tables[0], r, 2, money(m.unitPriceMinor));
        cell(tables[0], r, 3, qty(m.stockMilli));
        cell(tables[0], r, 4, q(m.stockAt.value_or("")));
    }
    for (const auto &u : snapshot->resourcePlan.uses) {
        auto r = append(1, q(u.id));
        choice(tables[1], r, 0, tasks, q(u.taskId));
        choice(tables[1], r, 1, catalog(0), q(u.materialId));
        cell(tables[1], r, 2, qty(u.quantityMilli));
        choice(tables[1], r, 3, accruals, q(u.accrual));
    }
    for (const auto &e : snapshot->resourcePlan.expenses) {
        auto r = append(2, q(e.id));
        cell(tables[2], r, 0, q(e.name));
        choice(tables[2], r, 1, tasks, q(e.taskId));
        cell(tables[2], r, 2, q(e.account));
        cell(tables[2], r, 3, money(e.amountMinor));
        choice(tables[2], r, 4, accruals, q(e.accrual));
    }
    for (const auto &role : snapshot->resourcePlan.roles) {
        auto r = append(3, q(role.id));
        cell(tables[3], r, 0, q(role.name));
        choice(tables[3], r, 1, {{"인력", "labor"}, {"장비", "equipment"}}, q(role.type));
    }
    for (const auto &d : snapshot->resourcePlan.demands) {
        auto r = append(4, q(d.id));
        choice(tables[4], r, 0, tasks, q(d.taskId));
        choice(tables[4], r, 1, catalog(3), q(d.roleId));
        choice(tables[4], r, 2, resources, q(d.resourceId));
        cell(tables[4], r, 3, QString::number(d.units));
    }
    for (const auto &receipt : snapshot->resourcePlan.receipts) {
        const auto r = append(5, q(receipt.id));
        choice(tables[5], r, 0, catalog(0), q(receipt.materialId));
        cell(tables[5], r, 1, q(receipt.at));
        cell(tables[5], r, 2, qty(receipt.quantityMilli));
    }
    connect(tabs, &QTabWidget::currentChanged, this, [=](int n) {
        int c = -1, source = -1;
        if (n == 1) {
            c = 1;
            source = 0;
        }
        if (n == 4) {
            c = 1;
            source = 3;
        }
        if (n == 5) {
            c = 0;
            source = 0;
        }
        if (c >= 0)
            for (int r = 0; r < tables[n]->rowCount(); ++r) {
                const auto old = q(ref(tables[n], r, c));
                choice(tables[n], r, c, catalog(source), old);
            }
    });
    auto *error = new QLabel(this);
    error->setObjectName("resourcePlanError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveResourcePlan");
    buttons->button(QDialogButtonBox::Save)->setEnabled(!document.readOnly());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(
        buttons, &QDialogButtonBox::accepted, this,
        [&, tables, error, session, revision, automatic, remainingAutomatic] {
            try {
                if (document.sessionGeneration() != session || !document.project() ||
                    document.project()->revision != revision)
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                ResourcePlan plan;
                plan.autoDelayForReceipts = automatic->isChecked();
                plan.autoDelayRemainingForReceipts = remainingAutomatic->isChecked();
                for (int r = 0; r < tables[0]->rowCount(); ++r) {
                    plan.materials.push_back({s(identity(tables[0], r)), s(value(tables[0], r, 0)),
                                              s(value(tables[0], r, 1)), integer(value(tables[0], r, 2)),
                                              quantity(value(tables[0], r, 3))});
                    if (!value(tables[0], r, 4).isEmpty())
                        plan.materials.back().stockAt = s(value(tables[0], r, 4));
                }
                for (int r = 0; r < tables[1]->rowCount(); ++r) {
                    const auto v = quantity(value(tables[1], r, 2));
                    if (!v)
                        throw AppError(Diagnostic{"RESOURCE_PLAN_INVALID",
                                                  {"수량은 소수 3자리, 금액·배정률은 허용 범위의 정수로 입력하세요."},
                                                  {}});
                    plan.uses.push_back({s(identity(tables[1], r)), ref(tables[1], r, 0), ref(tables[1], r, 1), *v,
                                         ref(tables[1], r, 3)});
                }
                for (int r = 0; r < tables[2]->rowCount(); ++r)
                    plan.expenses.push_back({s(identity(tables[2], r)), ref(tables[2], r, 1), s(value(tables[2], r, 0)),
                                             s(value(tables[2], r, 2)), integer(value(tables[2], r, 3)),
                                             ref(tables[2], r, 4)});
                for (int r = 0; r < tables[3]->rowCount(); ++r)
                    plan.roles.push_back({s(identity(tables[3], r)), s(value(tables[3], r, 0)), ref(tables[3], r, 1)});
                for (int r = 0; r < tables[4]->rowCount(); ++r) {
                    const auto units = integer(value(tables[4], r, 3));
                    if (!units || *units > 100000)
                        throw AppError(Diagnostic{"RESOURCE_PLAN_INVALID",
                                                  {"수량은 소수 3자리, 금액·배정률은 허용 범위의 정수로 입력하세요."},
                                                  {}});
                    plan.demands.push_back({s(identity(tables[4], r)), ref(tables[4], r, 0), ref(tables[4], r, 1),
                                            ref(tables[4], r, 2), int(*units)});
                }
                for (int r = 0; r < tables[5]->rowCount(); ++r) {
                    const auto amount = quantity(value(tables[5], r, 2));
                    if (!amount)
                        throw AppError(Diagnostic{"RESOURCE_PLAN_INVALID", {"입고 수량을 입력하세요."}, {}});
                    plan.receipts.push_back(
                        {s(identity(tables[5], r)), ref(tables[5], r, 0), s(value(tables[5], r, 1)), *amount});
                }
                document.setResourcePlan(std::move(plan));
                accept();
            } catch (const std::exception &e) {
                showEditorError(*error, e);
            }
        });
    prepareEditorDialog(*this);
}
} // namespace jobs
