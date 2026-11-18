#pragma once
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include <QAbstractTableModel>
#include <QStringList>
#include <unordered_map>

namespace jobs {
struct XmlScheduleValues;
class TaskTableModel final : public QAbstractTableModel {
public:
    explicit TaskTableModel(Document &document, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    bool setData(const QModelIndex &, const QVariant &, int role = Qt::EditRole) override;
    std::string columnKey(int) const;
    int columnForKey(const std::string &) const;
    void setWeeklyColumns(bool enabled, const std::string &baselineId);
    bool isPresentationColumn(int column) const;
    void synchronize(const std::vector<std::size_t> &rows);
    bool synchronizeTask();
    bool synchronizeBulk(BulkView &);
    bool synchronizeProjected(BulkView &);
    const Task &cachedTask(std::size_t row) const {
        return plan_.tasks.at(row);
    }
    bool containsSourceRow(std::size_t row) const {
        return positions_.contains(row);
    }
    std::size_t lastTaskVisits() const {
        return lastTaskVisits_;
    }
    std::function<bool(std::string, int, QString)> editRequested;
    std::function<void(std::string, int)> unchangedEditRequested;

private:
    Document &document_;
    PlanInput plan_;
    std::vector<std::size_t> rows_;
    std::unordered_map<std::size_t, int> positions_;
    std::size_t lastTaskVisits_ = 0;
    std::optional<ScheduleResult> result_;
    std::shared_ptr<const XmlScheduleValues> scheduleValues_;
    std::string documentId_;
    std::uint64_t session_ = 0, revision_ = 0, calculationGeneration_ = 0;
    bool initialized_ = false, readOnly_ = false;
    bool weeklyColumns_ = false;
    std::string baselineId_;
    std::unordered_map<std::string, int> numbers_;
    std::unordered_map<std::string, TaskSchedule> schedules_;
    struct PlannedDates {
        std::string start, finish;
    };
    std::unordered_map<std::string, PlannedDates> plannedDates_;
    std::unordered_map<std::string, QString> assignments_, issues_;
    AttributeLabels attributeLabels_;
    mutable std::shared_ptr<EvaluationCache> renderCache_ = std::make_shared<EvaluationCache>();
    mutable std::unordered_map<std::string, std::shared_ptr<const FormulaValues>> computed_;
    std::shared_ptr<const FormulaValues> computedValues(const Task &) const;
    QString text(const Task &, int column) const;
    const TaskSchedule *schedule(const Task &) const;
};
} // namespace jobs
