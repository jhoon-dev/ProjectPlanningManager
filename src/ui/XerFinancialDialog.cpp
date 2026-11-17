#include "ui/XerFinancialDialog.h"
#include "persistence/XerFinancialJson.h"
#include "formatting/Money.h"
#include "ui/EditorUi.h"
#include "ui/XerConversionEditor.h"
#include <QAbstractTableModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <map>
#include <memory>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QString hours(std::optional<std::int64_t> n) {
    return n ? QString::number(double(*n) / 6000.0, 'f', 6) : QString("—");
}
class FinancialModel final : public QAbstractTableModel {
    const XerFinancialSnapshot &s_;
    int kind_;
    std::vector<std::size_t> rows_;
    std::map<std::string, QString> tasks_, resources_;
    std::map<std::pair<std::string, std::string>, std::pair<std::int64_t, std::int64_t>> allocated_;

public:
    FinancialModel(const XerFinancialSnapshot &s, int kind, QObject *parent)
        : QAbstractTableModel(parent), s_(s), kind_(kind) {
        for (const auto &t : s.tasks)
            tasks_[t.id] = q(t.name);
        for (const auto &r : s.resources)
            resources_[r.id] = q(r.name);
        for (std::size_t i = 0; i < s.periods.size(); ++i) {
            const auto &e = s.periods[i];
            if ((kind == 1 && !e.resourceId.empty()) || (kind == 2 && e.resourceId.empty()))
                rows_.push_back(i);
            if (!e.resourceId.empty()) {
                auto &a = allocated_[{e.taskId, e.resourceId}];
                a.first += e.work.value_or(0);
                a.second += e.actualMinor.value_or(0);
            }
        }
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : kind_ == 0 ? int(s_.assignments.size()) : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : kind_ == 0 ? 10 : kind_ == 1 ? 6 : 10;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Vertical)
            return section + 1;
        const QStringList a{"작업",           "자원",      "정규 공수(h)", "초과근무(h)", "기간 공수(h)",
                            "기간 미지정(h)", "누계 원가", "기간 원가",    "미배분 원가", "구분"};
        const QStringList b{"작업", "자원", "시작", "종료", "공수(h)", "실제원가"};
        const QStringList c{"작업", "시작", "종료", "인건비", "장비비", "자재비", "경비", "외부 EV", "외부 PV", "설명"};
        return (kind_ == 0 ? a : kind_ == 1 ? b : c).value(section);
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || role != Qt::DisplayRole)
            return {};
        if (kind_ == 0) {
            const auto &a = s_.assignments.at(std::size_t(i.row()));
            auto found = allocated_.find({a.taskId, a.resourceId});
            const auto used = found == allocated_.end() ? std::pair<std::int64_t, std::int64_t>{0, 0} : found->second;
            std::optional<std::int64_t> work, cost;
            if (a.regularWork || a.overtimeWork)
                work = a.regularWork.value_or(0) + a.overtimeWork.value_or(0);
            if (a.regularMinor || a.overtimeMinor)
                cost = a.regularMinor.value_or(0) + a.overtimeMinor.value_or(0);
            const QStringList row{
                tasks_.at(a.taskId),
                resources_.at(a.resourceId),
                hours(a.regularWork),
                hours(a.overtimeWork),
                hours(used.first),
                hours(work ? std::optional<std::int64_t>(*work - used.first) : std::nullopt),
                moneyui::format(cost, s_.currency),
                moneyui::format(used.second, s_.currency),
                moneyui::format(cost ? std::optional<std::int64_t>(*cost - used.second) : std::nullopt, s_.currency),
                "원본 집계 · native 원장과 별도"};
            return row.value(i.column());
        }
        const auto &e = s_.periods.at(rows_.at(std::size_t(i.row())));
        if (kind_ == 1) {
            const QStringList row{tasks_.at(e.taskId), resources_.at(e.resourceId),
                                  q(e.start),          q(e.finish),
                                  hours(e.work),       moneyui::format(e.actualMinor, s_.currency)};
            return row.value(i.column());
        }
        const QStringList row{tasks_.at(e.taskId),
                              q(e.start),
                              q(e.finish),
                              moneyui::format(e.laborMinor, s_.currency),
                              moneyui::format(e.equipmentMinor, s_.currency),
                              moneyui::format(e.materialMinor, s_.currency),
                              moneyui::format(e.expenseMinor, s_.currency),
                              moneyui::format(e.bcwp, s_.currency),
                              moneyui::format(e.bcws, s_.currency),
                              "외부 요약 · native EVM에 합산 안 함"};
        return row.value(i.column());
    }
};
} // namespace
void showXerFinancials(QWidget *parent, const Project &p) {
    if (!p.xerFinancials)
        return;
    const auto snapshot = *activeXerFinancials(p);
    const bool current = xerFinancialsCurrent(p);
    QDialog dialog(parent);
    dialog.setObjectName("xerFinancialDialog");
    dialog.setWindowTitle("XER 보존 재무기간·누계 조회");
    dialog.resize(1000, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(&dialog);
    label->setTextFormat(Qt::PlainText);
    label->setText(QString("원본 %1 · 상태일 %2 · 통화 %3\n%4\n기간 집계와 날짜별 실적은 같은 금액의 다른 표현입니다. "
                           "합산하지 않습니다. 기간 미지정 공수는 날짜를 추정하지 않습니다.")
                       .arg(q(snapshot.sourceName), q(snapshot.statusAt), q(snapshot.currency),
                            current ? "현재 문서와 일치 · 원본 집계 재내보내기 가능"
                                    : "문서 변경 후의 원본 스냅샷 · 현재 원장과 별도 보존 / 자동 재내보내기 제외"));
    label->setObjectName("xerFinancialStatus");
    label->setWordWrap(true);
    layout->addWidget(label);
    if (!p.xerFinancialRevisions.empty()) {
        auto *original = new QPushButton("가져온 원본 집계 별도 조회", &dialog);
        original->setObjectName("xerViewOriginal");
        const auto reference = std::make_shared<const Project>(p);
        QObject::connect(original, &QPushButton::clicked, &dialog, [reference, &dialog] {
            auto source = *reference;
            source.xerFinancialRevisions.clear();
            showXerFinancials(&dialog, source);
        });
        layout->addWidget(original);
        label->setText("정정 이력 " + QString::number(p.xerFinancialRevisions.size()) + "건 · 최신 정정 집계\n" +
                       label->text());
    }
    auto *search = new QLineEdit(&dialog);
    search->setObjectName("xerFinancialSearch");
    search->setPlaceholderText("작업·자원·날짜 검색");
    layout->addWidget(search);
    auto *tabs = new QTabWidget(&dialog);
    tabs->setObjectName("xerFinancialTabs");
    layout->addWidget(tabs, 1);
    const QStringList names{"배정 누계·기간 미지정", "자원 재무기간", "작업 요약·외부 EV/PV"};
    for (int kind = 0; kind < 3; ++kind) {
        auto *view = new QTableView(tabs);
        view->setObjectName(QString("xerFinancialTable%1").arg(kind));
        auto *proxy = new QSortFilterProxyModel(view);
        proxy->setSourceModel(new FinancialModel(snapshot, kind, proxy));
        proxy->setFilterKeyColumn(-1);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        view->setModel(proxy);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setDefaultSectionSize(140);
        view->setWordWrap(false);
        QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        tabs->addTab(view, names[kind]);
    }
    if (!p.xerFinancialRevisions.empty()) {
        auto *history = new QTableWidget(int(p.xerFinancialRevisions.size()), 5, tabs);
        history->setObjectName("xerFinancialRevisionHistory");
        history->setHorizontalHeaderLabels({"기록 UTC", "작성자", "사유", "문서 revision", "이력 ID"});
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        for (std::size_t i = 0; i < p.xerFinancialRevisions.size(); ++i) {
            const auto &r = p.xerFinancialRevisions[i];
            const QStringList values{q(r.recordedAt), q(r.reporter), q(r.reason), QString::number(r.recordedRevision),
                                     q(r.id)};
            for (int c = 0; c < 5; ++c)
                history->setItem(int(i), c, new QTableWidgetItem(values[c]));
        }
        tabs->addTab(history, "집계 정정·배분 이력");
        auto *page = new QWidget(tabs);
        auto *conversionLayout = new QVBoxLayout(page);
        auto *conversions = new QTableWidget(0, 8, page);
        conversions->setObjectName("xerSourceConversionHistory");
        conversions->setHorizontalHeaderLabels(
            {"기록 UTC", "기준", "변환 대상", "원본 → 대상", "환산비", "기준일", "근거", "이력 ID"});
        conversions->setEditTriggers(QAbstractItemView::NoEditTriggers);
        conversions->setSelectionBehavior(QAbstractItemView::SelectRows);
        conversions->setSelectionMode(QAbstractItemView::SingleSelection);
        conversions->horizontalHeader()->setDefaultSectionSize(160);
        for (std::size_t i = 0; i < p.xerFinancialRevisions.size(); ++i) {
            const auto &r = p.xerFinancialRevisions[i];
            if (!r.relink)
                continue;
            for (bool incoming : {true, false}) {
                const auto &c = incoming ? r.relink->incomingConversion : r.relink->previousConversion;
                if (!c)
                    continue;
                const auto &source =
                    incoming ? r.relink->incoming : (i ? p.xerFinancialRevisions[i - 1].financials : *p.xerFinancials);
                auto append = [&](const QString &metric, const QString &from, const QString &to, auto n, auto d) {
                    const int row = conversions->rowCount();
                    conversions->insertRow(row);
                    const QStringList values{q(r.recordedAt),
                                             incoming ? "새 원본" : "이전 집계",
                                             metric,
                                             from + " → " + to,
                                             QString::number(n) + "/" + QString::number(d),
                                             q(c->effectiveDate),
                                             q(c->reference),
                                             q(r.id)};
                    for (int col = 0; col < values.size(); ++col) {
                        auto *item = new QTableWidgetItem(values[col]);
                        item->setData(Qt::UserRole, int(i));
                        conversions->setItem(row, col, item);
                    }
                };
                append("통화", q(source.currency), q(c->targetCurrency), c->numerator, c->denominator);
                for (const auto &rule : c->resources)
                    append("자원 공수", rule.fromType == "labor" ? "인력" : "장비",
                           rule.toType == "labor" ? "인력" : "장비", rule.workNumerator, rule.workDenominator);
            }
        }
        conversionLayout->addWidget(conversions);
        auto *inspect = new QPushButton("선택 이력의 원본·전환값 조회…", page);
        inspect->setObjectName("xerInspectSourceConversion");
        conversionLayout->addWidget(inspect);
        inspect->setEnabled(conversions->rowCount() > 0);
        if (conversions->rowCount()) {
            conversions->selectRow(0);
            QObject::connect(inspect, &QPushButton::clicked, &dialog, [&p, &dialog, conversions] {
                if (conversions->currentRow() < 0)
                    return;
                const auto i = std::size_t(conversions->item(conversions->currentRow(), 0)->data(Qt::UserRole).toInt());
                const auto &audit = *p.xerFinancialRevisions[i].relink;
                const auto &previous = i ? p.xerFinancialRevisions[i - 1].financials : *p.xerFinancials;
                previewXerConversions(&dialog, audit.incoming, previous, audit);
            });
            tabs->addTab(page, "통화·자원 전환 이력");
        } else
            delete page;
    }
    auto *source = new QLabel("출처 SHA256: " + q(snapshot.sourceSha256), &dialog);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(source);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    prepareEditorDialog(dialog);
    dialog.exec();
}
} // namespace jobs
