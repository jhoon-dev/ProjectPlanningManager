#include "ui/LevelingWitnessDialog.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <unordered_map>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromStdString(s);
}
const std::string &resume(const LevelingWitnessDates &r) {
    return r.remainingStart ? *r.remainingStart : r.start;
}
} // namespace
class LevelingWitnessModel final : public QAbstractTableModel {
    struct Row {
        QString name;
        LevelingWitnessDates current;
        std::optional<std::string> actualStart, actualFinish;
        std::int64_t total, free;
        bool completed;
    };
    std::vector<Row> rows_;
    std::unordered_map<std::string, std::size_t> positions_;
    std::shared_ptr<const LevelingFloatWitnesses> evidence_;
    std::unordered_map<std::string, const LevelingWitnessDates *> proposed_;
    std::vector<std::size_t> visible_;
    std::string focus_;
    QString error_;
    bool changed(const Row &r) const {
        const auto it = proposed_.find(r.current.taskId);
        return it != proposed_.end() && *it->second != r.current;
    }

public:
    LevelingWitnessModel(const Project &p, const ScheduleResult &result, QObject *parent)
        : QAbstractTableModel(parent) {
        if (p.documentId != result.documentId || p.revision != result.inputRevision ||
            result.status != CalculationStatus::Valid || !result.resourceLeveled || !result.levelingFloatCalculated ||
            !result.levelingWitnesses || result.levelingWitnesses->tasks.empty()) {
            error_ = "같은 입력의 검증된 재배치 증인이 없습니다. 평준화 비교를 다시 계산하세요.";
            return;
        }
        std::unordered_map<std::string, const TaskSchedule *> schedules;
        for (const auto &r : result.tasks)
            schedules.emplace(r.taskId, &r);
        for (const auto &task : p.tasks) {
            const auto it = schedules.find(task.id);
            if (it == schedules.end()) {
                error_ = "원본 작업과 계산 결과가 일치하지 않습니다.";
                rows_.clear();
                positions_.clear();
                return;
            }
            const auto &r = *it->second;
            const auto *fact = result.isForecast ? progressAsOf(p, task.id) : nullptr;
            positions_[task.id] = rows_.size();
            rows_.push_back({QString::number(rows_.size() + 1) + " · " + qs(task.name),
                             {r.taskId, r.start, r.finish, r.remainingStart},
                             fact ? fact->actualStart : std::nullopt,
                             fact ? fact->actualFinish : std::nullopt,
                             r.totalFloatMinutes,
                             r.freeFloatMinutes,
                             r.completedActual});
        }
        evidence_ = result.levelingWitnesses;
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(visible_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 8;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return {};
        const QStringList columns{"작업",      "현재 잔여 시작",  "현재 완료",       "증인 잔여 시작",
                                  "증인 완료", "실제 시작(고정)", "실제 완료(고정)", "변경"};
        return section >= 0 && section < columns.size() ? QVariant(columns[section]) : QVariant{};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || std::size_t(index.row()) >= visible_.size() || index.column() >= 8)
            return {};
        const auto &row = rows_[visible_[std::size_t(index.row())]];
        const auto &id = row.current.taskId;
        const auto it = proposed_.find(id);
        const auto &candidate = it == proposed_.end() ? row.current : *it->second;
        if (role == Qt::UserRole)
            return qs(id);
        if (role == Qt::ToolTipRole)
            return qs(id) + (changed(row) ? "\n검증된 재배치 날짜" : "\n현재 날짜 유지");
        if (role == Qt::BackgroundRole && (id == focus_ || changed(row)))
            return QColor(id == focus_ ? "#e4f2ff" : "#fff3cc");
        if (role == Qt::ForegroundRole && (id == focus_ || changed(row)))
            return QColor("#203040");
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return row.name;
        case 1:
            return row.completed ? QString("—") : qs(resume(row.current));
        case 2:
            return qs(row.current.finish);
        case 3:
            return row.completed ? QString("—") : qs(resume(candidate));
        case 4:
            return qs(candidate.finish);
        case 5:
            return row.actualStart ? qs(*row.actualStart) : QString("—");
        case 6:
            return row.actualFinish ? qs(*row.actualFinish) : QString("—");
        case 7:
            return QString(row.completed  ? "완료 실적"
                           : id == focus_ ? (changed(row) ? "선택 작업 · 이동" : "선택 작업 · 유지")
                           : changed(row) ? "함께 이동"
                                          : "현재 유지");
        }
        return {};
    }
    std::vector<std::pair<std::string, QString>> choices() const {
        std::vector<std::pair<std::string, QString>> result;
        if (evidence_)
            for (const auto &w : evidence_->tasks)
                if (positions_.contains(w.taskId))
                    result.emplace_back(w.taskId, rows_[positions_.at(w.taskId)].name);
        return result;
    }
    int focusRow() const {
        for (std::size_t i = 0; i < visible_.size(); ++i)
            if (rows_[visible_[i]].current.taskId == focus_)
                return int(i);
        return -1;
    }
    QString select(const std::string &id, bool onlyChanged) {
        beginResetModel();
        proposed_.clear();
        visible_.clear();
        focus_ = id;
        const auto *proof = evidence_ ? &evidence_->tasks : nullptr;
        const auto found =
            proof ? std::find_if(proof->begin(), proof->end(), [&](const auto &w) { return w.taskId == id; })
                  : std::vector<LevelingFloatWitness>::const_iterator{};
        if (!proof || found == proof->end() || !positions_.contains(id)) {
            endResetModel();
            return error_.isEmpty() ? "선택 작업의 검증된 재배치 증인이 없습니다." : error_;
        }
        for (const auto &r : found->dates) {
            if (!positions_.contains(r.taskId) || proposed_.contains(r.taskId) || !validStamp(r.start) ||
                !validStamp(r.finish) || r.start > r.finish ||
                (r.remainingStart &&
                 (!validStamp(*r.remainingStart) || *r.remainingStart < r.start || *r.remainingStart > r.finish))) {
                proposed_.clear();
                endResetModel();
                return "재배치 증인 날짜를 표시할 수 없습니다.";
            }
            const auto &row = rows_[positions_.at(r.taskId)];
            if ((row.actualStart && r.start != *row.actualStart) ||
                (row.actualFinish && r.finish != *row.actualFinish)) {
                proposed_.clear();
                endResetModel();
                return "재배치 증인이 고정 실적과 일치하지 않습니다.";
            }
            proposed_.emplace(r.taskId, &r);
        }
        if (!proposed_.contains(id)) {
            proposed_.clear();
            endResetModel();
            return "선택 작업의 증인 날짜가 없습니다.";
        }
        int peers = 0;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto moved = changed(rows_[i]);
            if (moved && rows_[i].current.taskId != id)
                ++peers;
            if (!onlyChanged || moved || rows_[i].current.taskId == id)
                visible_.push_back(i);
        }
        const auto &row = rows_[positions_.at(id)];
        const auto message = row.name + " · 총여유 " + QString::number(double(row.total) / 60, 'g', 10) +
                             "h / 자유여유 " + QString::number(double(row.free) / 60, 'g', 10) + "h · 함께 이동 " +
                             QString::number(peers) + "작업";
        endResetModel();
        return message;
    }
};
LevelingWitnessDialog::LevelingWitnessDialog(const Project &p, const ScheduleResult &result, const std::string &focus,
                                             const QString &scenario, QWidget *parent)
    : QDialog(parent) {
    setObjectName("levelingWitnessDialog");
    setWindowTitle("평준화 총여유 근거 · " + scenario);
    resize(1280, 650);
    setMinimumSize(780, 420);
    auto *layout = new QVBoxLayout(this);
    auto *source =
        new QLabel(scenario + " · " + qs(p.name) + " · 원본 rev " + QString::number(p.revision) +
                       (result.isForecast && p.statusAt ? " · 잔여 예측 / 상태일 " + qs(*p.statusAt) : " · 계획"),
                   this);
    source->setObjectName("witnessSource");
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    source->setToolTip(qs(p.documentId) + "\n" + qs(result.engineVersion) + "\n" + qs(result.calculationPolicy));
    layout->addWidget(source);
    auto *controls = new QHBoxLayout;
    tasks_ = new QComboBox(this);
    tasks_->setObjectName("witnessTask");
    tasks_->setToolTip("검증된 총여유 재배치 증인이 있는 작업 목록입니다.");
    changedOnly_ = new QCheckBox("선택 작업과 이동한 작업만", this);
    changedOnly_->setObjectName("witnessChangedOnly");
    changedOnly_->setChecked(true);
    controls->addWidget(new QLabel("총여유를 확인할 작업", this));
    controls->addWidget(tasks_, 1);
    controls->addWidget(changedOnly_);
    layout->addLayout(controls);
    summary_ = new QLabel(this);
    summary_->setObjectName("witnessSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);
    model_ = new LevelingWitnessModel(p, result, this);
    table_ = new QTableView(this);
    table_->setObjectName("witnessDates");
    table_->setModel(model_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->setColumnWidth(0, 180);
    for (int c = 1; c < 7; ++c)
        table_->setColumnWidth(c, 148);
    table_->setColumnWidth(7, 145);
    layout->addWidget(table_, 1);
    auto *note = new QLabel("총여유의 검증된 배치입니다. 자유여유는 다른 작업 날짜를 현재대로 고정한 수치입니다.\n"
                            "구간 재생성을 허용하며 교차 작업의 최종 자원 반환 순서를 유지합니다. 표시하지 않은 작업은 "
                            "현재 날짜 그대로입니다.\n"
                            "각 작업의 증인은 서로 다른 배치일 수 있습니다. 다른 작업의 최후 날짜를 합쳐 적용하지 "
                            "마세요. 이 창은 원본 일정을 변경하지 않습니다.",
                            this);
    note->setTextFormat(Qt::PlainText);
    note->setObjectName("witnessPolicy");
    if (result.calculationPolicy.ends_with("resource-material-float-v1"))
        note->setText(
            note->text() +
            "\n자재 입고 시각·재고·소모도 검산한 배치입니다. 잔여 예측은 확인 재고와 잔여 소모를 사용합니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *bottom = new QHBoxLayout;
    bottom->addStretch();
    auto *close = new QPushButton("닫기", this);
    close->setObjectName("closeWitness");
    bottom->addWidget(close);
    layout->addLayout(bottom);
    for (const auto &[id, name] : model_->choices())
        tasks_->addItem(name, qs(id));
    tasks_->setEnabled(tasks_->count() > 0);
    changedOnly_->setEnabled(tasks_->count() > 0);
    const auto initial = tasks_->findData(qs(focus));
    if (initial >= 0)
        tasks_->setCurrentIndex(initial);
    connect(tasks_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(changedOnly_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    refresh();
}
void LevelingWitnessDialog::refresh() {
    summary_->setText(model_->select(tasks_->currentData().toString().toStdString(), changedOnly_->isChecked()));
    if (const auto row = model_->focusRow(); row >= 0) {
        table_->selectRow(row);
        table_->scrollTo(model_->index(row, 0));
    }
}
} // namespace ppm
