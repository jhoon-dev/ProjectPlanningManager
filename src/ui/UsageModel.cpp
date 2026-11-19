#include "ui/UsageDialog.h"
#include "reporting/ReportExport.h"
#include <QStringList>
#include <algorithm>
namespace ppm {
namespace {
QString text(const std::string &s) {
    return QString::fromUtf8(s);
}
QString hours(const std::optional<std::int64_t> &v) {
    return v ? QString::number(double(*v) / 6000, 'f', 2) : "미정";
}
QString show(const UsageValue &v, bool cost, const std::string &currency) {
    if (cost)
        return displayCell(
            {v.cost ? decltype(ReportCell::value){*v.cost} : decltype(ReportCell::value){}, CellFormat::Money},
            currency);
    if (v.labor && *v.labor == 0 && v.equipment && *v.equipment == 0)
        return "0.00";
    if (v.equipment && *v.equipment == 0)
        return hours(v.labor) + " 인시";
    if (v.labor && *v.labor == 0)
        return hours(v.equipment) + " 장비시";
    return hours(v.labor) + " 인시 · " + hours(v.equipment) + " 장비시";
}
} // namespace
UsageModel::UsageModel(QObject *parent) : QAbstractItemModel(parent) {}
void UsageModel::setUsage(std::shared_ptr<const UsageResult> result, UsageHierarchy hierarchy) {
    beginResetModel();
    result_ = std::move(result);
    rows_.clear();
    children_.clear();
    position_.clear();
    if (result_) {
        rows_ = usageRows(*result_, hierarchy);
        children_.resize(rows_.size() + 1);
        position_.resize(rows_.size());
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto p = rows_[i].parent ? *rows_[i].parent + 1 : 0;
            position_[i] = int(children_[p].size());
            children_[p].push_back(i);
        }
    }
    endResetModel();
}
void UsageModel::setMetric(int metric) {
    if (metric < 0 || metric > 7 || metric == metric_)
        return;
    beginResetModel();
    metric_ = metric;
    endResetModel();
}
QModelIndex UsageModel::index(int row, int column, const QModelIndex &p) const {
    if (row < 0 || column < 0 || column >= columnCount() || (p.isValid() && p.column() != 0))
        return {};
    const auto id = p.isValid() ? p.internalId() + 1 : 0;
    if (id >= children_.size() || std::size_t(row) >= children_[id].size())
        return {};
    return createIndex(row, column, quintptr(children_[id][std::size_t(row)]));
}
QModelIndex UsageModel::parent(const QModelIndex &i) const {
    if (!i.isValid() || i.internalId() >= rows_.size())
        return {};
    const auto p = rows_[i.internalId()].parent;
    return p ? createIndex(position_[*p], 0, quintptr(*p)) : QModelIndex{};
}
int UsageModel::rowCount(const QModelIndex &p) const {
    if (p.isValid() && p.column() != 0)
        return 0;
    const auto id = p.isValid() ? p.internalId() + 1 : 0;
    return id < children_.size() ? int(children_[id].size()) : 0;
}
int UsageModel::columnCount(const QModelIndex &) const {
    return result_ ? int(result_->buckets.size()) + 3 : 0;
}
QVariant UsageModel::data(const QModelIndex &i, int role) const {
    if (!result_ || !i.isValid() || i.internalId() >= rows_.size())
        return {};
    const auto &row = rows_[i.internalId()];
    if (role == Qt::UserRole)
        return text(row.taskId);
    if (role == Qt::UserRole + 1)
        return text(row.resourceId);
    if (role == Qt::ToolTipRole) {
        std::pair<std::size_t, std::size_t> reports{};
        if (i.column() >= 3)
            reports = row.actualCounts.at(std::size_t(i.column() - 3));
        else
            for (const auto &n : row.actualCounts) {
                reports.first += n.first;
                reports.second += n.second;
            }
        const bool estimated = std::any_of(result_->actualTables.begin(), result_->actualTables.end(),
                                           [](const auto &t) { return t.id == "periodActualAllocation"; });
        return (estimated ? QString("이 조회에 비례 추정 자료 포함 · 척도/날짜별 근거는 기간 실적 표 참조\n")
                          : QString{}) +
               "작업 ID: " + text(row.taskId) + "\n자원 ID: " + text(row.resourceId) +
               "\n기간 합계는 표시 범위만 포함 · 상위/배정 행 중복 합산 금지" +
               "\n누계와 기간별 배분은 더하지 않음 · 보고 실제는 입력분 합계이며 미보고의 완전성을 보장하지 "
               "않음\n미배분 실제원가: " +
               displayCell({row.unallocatedActual ? decltype(ReportCell::value){*row.unallocatedActual}
                                                  : decltype(ReportCell::value){},
                            CellFormat::Money},
                           result_->currency) +
               "\n공수 보고 " + QString::number(reports.first) + "건 · 원가 배분 보고 " +
               QString::number(reports.second) + "건 (부분 보고 합계)";
    }
    if (role == Qt::TextAlignmentRole && i.column() > 0)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole && role != Qt::UserRole + 2 && role != Qt::UserRole + 3)
        return {};
    if (i.column() == 0)
        return text(row.name) + (row.baselineOnly ? " · 기준선 전용" : "");
    if (i.column() == 2)
        return displayCell(
            {row.cumulativeActual ? decltype(ReportCell::value){*row.cumulativeActual} : decltype(ReportCell::value){},
             CellFormat::Money},
            result_->currency);
    if (row.fixed && metric_ == 5 && role == Qt::DisplayRole)
        return "고정비 제외";
    const auto &values = metric_ >= 6 ? row.reportedActual : row.values[std::size_t(metric_ % 3)];
    const auto v = i.column() == 1 ? (metric_ >= 6 ? usageReportedTotal(values) : usageTotal(values))
                                   : values.at(std::size_t(i.column() - 3));
    if (role == Qt::UserRole + 3) {
        std::pair<std::size_t, std::size_t> count{};
        if (i.column() == 1)
            for (const auto &c : row.actualCounts) {
                count.first += c.first;
                count.second += c.second;
            }
        else
            count = row.actualCounts.at(std::size_t(i.column() - 3));
        return QVariantList{qulonglong(count.first), qulonglong(count.second)};
    }
    if (role == Qt::UserRole + 2) {
        const auto raw = [](Money n) -> QVariant { return n ? QVariant::fromValue<qlonglong>(*n) : QVariant{}; };
        return QVariantList{raw(v.labor), raw(v.equipment), raw(v.cost)};
    }
    if (metric_ >= 6) {
        if (metric_ == 7)
            return v.reportOverflow[2] ? "합계 한도 초과" : v.cost ? show(v, true, result_->currency) : "미보고";
        QStringList known;
        if (v.reportOverflow[0])
            known << "인시 합계 한도 초과";
        if (v.reportOverflow[1])
            known << "장비시 합계 한도 초과";
        if (v.labor)
            known << hours(v.labor) + " 인시";
        if (v.equipment)
            known << hours(v.equipment) + " 장비시";
        return known.isEmpty() ? "미보고" : known.join(" · ");
    }
    return show(v, metric_ >= 3, result_->currency);
}
QVariant UsageModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == 2)
        return "상태일 기준 작업별 누계 · 기간/자원 미배분 · 표시 기간 합계에 더하지 않음";
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal || !result_)
        return {};
    if (section == 0)
        return "작업 / 자원 · 배정";
    if (section == 1)
        return "표시 기간 합계";
    if (section == 2)
        return "누적 실제원가 (" + text(result_->currency) + ")";
    if (section < 3 || section >= columnCount())
        return {};
    const auto &b = result_->buckets[std::size_t(section - 3)];
    return text(b.from) + (b.from == b.to ? "" : " ~ " + text(b.to));
}
Qt::ItemFlags UsageModel::flags(const QModelIndex &i) const {
    return i.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}
} // namespace ppm
