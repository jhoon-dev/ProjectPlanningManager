#include "ui/XerConversionEditor.h"
#include "ui/EditorUi.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
namespace jobs {
XerConversionEditor::XerConversionEditor(const QString &title, const QString &prefix, const Project &p, QWidget *parent)
    : QGroupBox(title, parent), targetCurrency_(p.currency) {
    auto *grid = new QGridLayout(this);
    enabled_ = new QCheckBox("이 원본의 통화·자원 단위 전환 사용", this);
    enabled_->setObjectName(prefix + "Enabled");
    grid->addWidget(enabled_, 0, 0, 1, 5);
    auto *fields = new QWidget(this);
    auto *rows = new QGridLayout(fields);
    rows->setContentsMargins(0, 0, 0, 0);
    auto spin = [&](const QString &name) {
        auto *s = new QSpinBox(fields);
        s->setRange(1, 1000000000);
        s->setValue(1);
        s->setObjectName(prefix + name);
        QObject::connect(s, &QSpinBox::valueChanged, this, [this] {
            if (changed)
                changed();
        });
        return s;
    };
    numerator_ = spin("Numerator");
    denominator_ = spin("Denominator");
    rows->addWidget(new QLabel("원본 1통화 = 현재 통화 ×", fields), 0, 0);
    rows->addWidget(numerator_, 0, 1);
    rows->addWidget(new QLabel("/", fields), 0, 2);
    rows->addWidget(denominator_, 0, 3);
    for (int i = 0; i < 2; ++i) {
        const auto key = i == 0 ? "Labor" : "Equipment";
        types_[i] = new QComboBox(fields);
        types_[i]->setObjectName(prefix + key + "Type");
        types_[i]->addItem("인력", "labor");
        types_[i]->addItem("장비", "equipment");
        types_[i]->setCurrentIndex(i);
        rows->addWidget(new QLabel(i == 0 ? "원본 인력 전체 →" : "원본 장비 전체 →", fields), i + 1, 0);
        rows->addWidget(types_[i], i + 1, 1);
        rows->addWidget(new QLabel("공수 ×", fields), i + 1, 2);
        workNumerator_[i] = spin(QString(key) + "Numerator");
        workDenominator_[i] = spin(QString(key) + "Denominator");
        auto *ratioRow = new QHBoxLayout;
        ratioRow->addWidget(workNumerator_[i]);
        ratioRow->addWidget(new QLabel("/", fields));
        ratioRow->addWidget(workDenominator_[i]);
        rows->addLayout(ratioRow, i + 1, 3);
        QObject::connect(types_[i], &QComboBox::currentIndexChanged, this, [this] {
            if (changed)
                changed();
        });
    }
    date_ = new QLineEdit(QString::fromStdString(p.statusAt.value_or(p.startDate).substr(0, 10)), fields);
    date_->setObjectName(prefix + "Date");
    date_->setPlaceholderText("YYYY-MM-DD");
    reference_ = new QLineEdit(fields);
    reference_->setObjectName(prefix + "Reference");
    reference_->setMaxLength(4096);
    reference_->setPlaceholderText("환율 출처·단위/생산성 환산 근거");
    rows->addWidget(new QLabel("전환 기준일 / 근거", fields), 3, 0);
    rows->addWidget(date_, 3, 1);
    rows->addWidget(reference_, 3, 2, 1, 2);
    grid->addWidget(fields, 1, 0, 1, 5);
    fields->setEnabled(false);
    QObject::connect(enabled_, &QCheckBox::toggled, fields, &QWidget::setEnabled);
    QObject::connect(enabled_, &QCheckBox::toggled, this, [this] {
        if (changed)
            changed();
    });
    for (auto *edit : {date_, reference_})
        QObject::connect(edit, &QLineEdit::textChanged, this, [this] {
            if (changed)
                changed();
        });
}
void XerConversionEditor::source(const XerFinancialSnapshot &s) {
    setTitle(title().section(" · ", 0, 0) + " · " + QString::fromStdString(s.currency + " → " + targetCurrency_));
    enabled_->setChecked(false);
    numerator_->setValue(1);
    denominator_->setValue(1);
    reference_->clear();
    for (int i = 0; i < 2; ++i) {
        types_[i]->setCurrentIndex(i);
        workNumerator_[i]->setValue(1);
        workDenominator_[i]->setValue(1);
        const auto type = i == 0 ? "labor" : "equipment";
        const bool present =
            std::any_of(s.resources.begin(), s.resources.end(), [&](const auto &r) { return r.type == type; }) ||
            std::any_of(s.periods.begin(), s.periods.end(), [&](const auto &e) {
                return i == 0 ? bool(e.laborWork || e.laborMinor) : bool(e.equipmentWork || e.equipmentMinor);
            });
        types_[i]->setEnabled(present);
        workNumerator_[i]->setEnabled(present);
        workDenominator_[i]->setEnabled(present);
    }
}
std::optional<XerSourceConversion> XerConversionEditor::conversion() const {
    if (!enabled_->isChecked())
        return {};
    XerSourceConversion c;
    c.targetCurrency = targetCurrency_;
    c.effectiveDate = date_->text().trimmed().toStdString();
    c.reference = reference_->text().trimmed().toStdString();
    c.numerator = numerator_->value();
    c.denominator = denominator_->value();
    for (int i = 0; i < 2; ++i) {
        const auto from = i == 0 ? "labor" : "equipment";
        const auto to = types_[i]->currentData().toString().toStdString();
        if (from != to || workNumerator_[i]->value() != workDenominator_[i]->value())
            c.resources.push_back({from, to, workNumerator_[i]->value(), workDenominator_[i]->value()});
    }
    return c;
}
namespace {
class Preview final : public QAbstractTableModel {
    std::vector<QStringList> rows_;

public:
    using QAbstractTableModel::QAbstractTableModel;
    void add(const QString &scope, const XerFinancialSnapshot &a, const std::optional<XerSourceConversion> &c) {
        if (!c)
            return;
        const auto b = convertXerSource(a, *c);
        auto row = [&](const std::string &id, const QString &metric, const QString &before, const QString &after) {
            rows_.push_back({scope, QString::fromStdString(id), metric, before, after});
        };
        row("", "통화", QString::fromStdString(a.currency), QString::fromStdString(b.currency));
        auto value = [](auto n) { return n ? QString::number(*n) : QString("미정"); };
        auto amount = [&](const auto &id, const QString &metric, auto from, auto to) {
            row(id, metric, value(from), value(to));
        };
        for (std::size_t i = 0; i < a.resources.size(); ++i)
            row(a.resources[i].id, "자원 종류", QString::fromStdString(a.resources[i].type),
                QString::fromStdString(b.resources[i].type));
        for (std::size_t i = 0; i < a.tasks.size(); ++i)
            amount(a.tasks[i].id, "누적 AC · 통화 최소 단위", a.tasks[i].actualMinor, b.tasks[i].actualMinor);
        for (std::size_t i = 0; i < a.assignments.size(); ++i) {
            const auto &x = a.assignments[i], &y = b.assignments[i];
            const auto id = x.taskId + "/" + x.resourceId;
            amount(id, "정규 공수 · 1/100분", x.regularWork, y.regularWork);
            amount(id, "초과 공수 · 1/100분", x.overtimeWork, y.overtimeWork);
            amount(id, "정규 원가 · 최소 단위", x.regularMinor, y.regularMinor);
            amount(id, "초과 원가 · 최소 단위", x.overtimeMinor, y.overtimeMinor);
        }
        for (std::size_t i = 0; i < a.periods.size(); ++i) {
            const auto &x = a.periods[i], &y = b.periods[i];
            const auto id = x.taskId + "/" + x.resourceId + " " + x.start + "~" + x.finish;
            for (auto [name, member] :
                 std::initializer_list<std::pair<const char *, std::optional<std::int64_t> XerFinancialPeriod::*>>{
                     {"공수", &XerFinancialPeriod::work},
                     {"원가", &XerFinancialPeriod::actualMinor},
                     {"인력 공수", &XerFinancialPeriod::laborWork},
                     {"장비 공수", &XerFinancialPeriod::equipmentWork},
                     {"인력 원가", &XerFinancialPeriod::laborMinor},
                     {"장비 원가", &XerFinancialPeriod::equipmentMinor},
                     {"자재 원가", &XerFinancialPeriod::materialMinor},
                     {"직접비", &XerFinancialPeriod::expenseMinor},
                     {"EV", &XerFinancialPeriod::bcwp},
                     {"PV", &XerFinancialPeriod::bcws}})
                amount(id, QString::fromUtf8(name), x.*member, y.*member);
        }
    }
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : 5;
    }
    QVariant data(const QModelIndex &i, int role) const override {
        return i.isValid() && (role == Qt::DisplayRole || role == Qt::ToolTipRole)
                   ? QVariant(rows_.at(i.row()).at(i.column()))
                   : QVariant{};
    }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return role == Qt::DisplayRole && o == Qt::Horizontal
                   ? QVariant(QStringList{"기준", "대상·기간", "지표", "원본", "전환 결과"}.at(c))
                   : QVariant{};
    }
};
} // namespace
void previewXerConversions(QWidget *parent, const XerFinancialSnapshot &incoming, const XerFinancialSnapshot &previous,
                           const XerRelinkAudit &audit) {
    QDialog d(parent);
    d.setObjectName("xerConversionPreview");
    d.setWindowTitle("통화·자원 종류 전환 미리보기");
    auto *layout = new QVBoxLayout(&d);
    auto *note =
        new QLabel("금액: 각 통화 최소 단위 정수, half-up 반올림 · 공수: 1/100 근무분, 정확한 정수만 허용.\n원본은 "
                   "보존하며 현재 원장·계획·단가·생산성은 바뀌지 않습니다. 병합 검토에서 최종 결과를 확인하세요.",
                   &d);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableView(&d);
    table->setObjectName("xerConversionValues");
    auto *model = new Preview(table);
    model->add("새 원본", incoming, audit.incomingConversion);
    model->add("이전 집계", previous, audit.previousConversion);
    table->setModel(model);
    table->horizontalHeader()->setDefaultSectionSize(155);
    table->setColumnWidth(1, 300);
    layout->addWidget(table, 1);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    QObject::connect(close, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(close);
    d.resize(1050, 650);
    prepareEditorDialog(d);
    d.exec();
}
} // namespace jobs
