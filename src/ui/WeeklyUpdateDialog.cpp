#include "ui/WeeklyUpdateDialog.h"
#include "ui/TaskInput.h"
#include "ui/EditorUi.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <unordered_map>
#include <algorithm>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string weekStart(const Project &p) {
    if (!p.statusAt)
        return {};
    const auto date = QDate::fromString(q(*p.statusAt).left(10), Qt::ISODate);
    return date.addDays(1 - date.dayOfWeek()).toString(Qt::ISODate).toStdString() + " 00:00";
}
struct Fact {
    const ProgressEntry *entry = nullptr;
    int history = 0;
};
using Facts = std::unordered_map<std::string, Fact>;
Facts facts(const Project &p) {
    Facts result;
    for (const auto &e : p.progress) {
        auto &f = result[e.taskId];
        ++f.history;
        // Same as progressAsOf: latest status, last ledger entry wins ties.
        if (p.statusAt && e.statusAt <= *p.statusAt && (!f.entry || f.entry->statusAt <= e.statusAt))
            f.entry = &e;
    }
    return result;
}
bool inWeek(const ProgressEntry *e, const std::string &begin) {
    return e && e->statusAt >= begin;
}
bool target(const ProgressEntry *e, const std::string &begin) {
    return !e || !e->actualFinish || inWeek(e, begin);
}
QString reportState(const ProgressEntry *e, const Project &p, const std::string &begin) {
    return !p.statusAt                  ? "상태일 선택 필요"
           : !e                         ? "미보고 · 착수 여부 미확인"
           : e->statusAt == *p.statusAt ? "현재 상태일 보고"
           : inWeek(e, begin)           ? "이번 주 앞선 보고"
                                        : "이전 주 보고만";
}
} // namespace
WeeklyUpdateDialog::WeeklyUpdateDialog(Document &document, QWidget *parent)
    : QDialog(parent), document_(document), session_(document.sessionGeneration()) {
    setObjectName("weeklyUpdateDialog");
    setWindowTitle("주간 갱신 · 보고 현황과 연속 입력");
    resize(1120, 720);
    auto *layout = new QVBoxLayout(this);
    heading_ = new QLabel;
    heading_->setObjectName("weeklyHeading");
    heading_->setTextFormat(Qt::PlainText);
    heading_->setWordWrap(true);
    layout->addWidget(heading_);
    status_ = new QPushButton("상태일 설정/변경…");
    status_->setObjectName("weeklySetStatus");
    layout->addWidget(status_);
    summary_ = new QLabel;
    summary_->setObjectName("weeklySummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    auto *rule =
        new QLabel("범위: 전체 프로젝트 (주 창의 검색·WBS와 별개). 주는 선택한 상태일의 월요일부터입니다.\n"
                   "기본 확인 대상: 완료가 보고되지 않은 작업 + 이번 주 보고 작업. 향후 계획도 포함될 수 있습니다.\n"
                   "미보고는 미착수/0%를 뜻하지 않습니다. 이전 보고를 현재 상태일 보고로 자동 제출하지 않습니다.");
    rule->setWordWrap(true);
    layout->addWidget(rule);
    auto *find = new QHBoxLayout;
    filter_ = new QComboBox;
    filter_->setObjectName("weeklyFilter");
    filter_->setAccessibleName("주간 보고 필터");
    filter_->addItems({"이번 주 확인 대상", "대상 중 현재 상태일 미보고", "이번 주 기존 보고", "선택 상태일까지 미보고",
                       "전체 작업"});
    search_ = new QLineEdit;
    search_->setObjectName("weeklySearch");
    search_->setAccessibleName("주간 갱신 작업 검색");
    search_->setPlaceholderText("작업 이름·ID 검색");
    search_->setClearButtonEnabled(true);
    find->addWidget(filter_);
    find->addWidget(search_, 1);
    layout->addLayout(find);
    tasks_ = new QTableWidget(0, 6);
    tasks_->setObjectName("weeklyTasks");
    tasks_->setHorizontalHeaderLabels({"작업", "작업 상태", "보고 현황", "최근 보고 상태일", "실제 / 잔여", "보고자"});
    tasks_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasks_->setSelectionMode(QAbstractItemView::SingleSelection);
    tasks_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tasks_->setMinimumHeight(180);
    tasks_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int c = 0; c < 6; ++c)
        tasks_->setColumnWidth(c, c == 0 ? 210 : c == 2 ? 220 : c == 3 ? 155 : 145);
    layout->addWidget(tasks_, 1);
    details_ = new QLabel;
    details_->setObjectName("weeklyDetails");
    details_->setTextFormat(Qt::PlainText);
    details_->setWordWrap(true);
    layout->addWidget(details_);
    continue_ = new QCheckBox("저장 후 다음 미보고 대상 입력으로 계속 (현재 목록 순서)");
    continue_->setObjectName("weeklyContinue");
    continue_->setChecked(true);
    layout->addWidget(continue_);
    notice_ = new QLabel;
    notice_->setObjectName("weeklyNotice");
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    edit_ = buttons->addButton("선택 작업 입력/정정…", QDialogButtonBox::ActionRole);
    edit_->setObjectName("weeklyEditProgress");
    next_ = buttons->addButton("다음 미보고 대상 선택", QDialogButtonBox::ActionRole);
    next_->setObjectName("weeklyNext");
    recalculate_ = buttons->addButton("입력 마치고 재예측", QDialogButtonBox::ActionRole);
    recalculate_->setObjectName("weeklyRecalculate");
    recalculate_->setToolTip("주간 갱신 창을 닫고 현재 입력으로 재예측합니다. 파일 저장은 별도입니다.");
    connect(recalculate_, &QPushButton::clicked, this, [this] {
        recalculationRequested_ = true;
        accept();
    });
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
    connect(tasks_, &QTableWidget::itemSelectionChanged, this, [this] { selected(); });
    connect(tasks_, &QTableWidget::cellDoubleClicked, this, [this] { editSelected(); });
    connect(edit_, &QPushButton::clicked, this, [this] { editSelected(); });
    connect(status_, &QPushButton::clicked, this, [this] {
        if (editStatus && !document_.readOnly())
            editStatus();
        notice_->clear();
        reload();
    });
    connect(next_, &QPushButton::clicked, this, [this] {
        const auto start = tasks_->currentRow();
        for (int step = 1; step <= tasks_->rowCount(); ++step) {
            const auto row = (std::max(start, 0) + step) % tasks_->rowCount();
            if (tasks_->item(row, 0)->data(Qt::UserRole + 1).toBool()) {
                tasks_->selectRow(row);
                tasks_->scrollToItem(tasks_->item(row, 0));
                return;
            }
        }
        notice_->setText(
            "현재 목록에는 현재 상태일의 미보고 대상이 없습니다. 다른 필터나 전체 작업에서 다시 확인할 수 있습니다.");
    });
    listener_ = document_.subscribe([this](auto) {
        QTimer::singleShot(0, this, [this] {
            if (!editing_)
                reload();
        });
    });
    reload();
    prepareEditorDialog(*this);
    // These are short fixed choices; keep their meaning visible beside the search box.
    int filterWidth = 0;
    for (int i = 0; i < filter_->count(); ++i)
        filterWidth = std::max(filterWidth, filter_->fontMetrics().horizontalAdvance(filter_->itemText(i)));
    filter_->setMinimumWidth(filterWidth + 44);
}
WeeklyUpdateDialog::~WeeklyUpdateDialog() {
    document_.unsubscribe(listener_);
}
std::string WeeklyUpdateDialog::selectedId() const {
    const auto row = tasks_->currentRow();
    return row >= 0 && tasks_->item(row, 0) ? tasks_->item(row, 0)->data(Qt::UserRole).toString().toStdString()
                                            : std::string{};
}
void WeeklyUpdateDialog::reload(const std::string &requested) {
    if (!document_.project() || document_.sessionGeneration() != session_) {
        reject();
        return;
    }
    const auto keep = requested.empty() ? selectedId() : requested;
    const auto &p = *document_.project();
    const auto begin = weekStart(p);
    auto lookup = facts(p);
    heading_->setText(q(p.name) + " · 상태일 " + (p.statusAt ? q(*p.statusAt) : "미설정 · 먼저 상태일을 선택하세요") +
                      (p.statusAt ? "\n보고 주: " + q(begin).left(10) + " ~ " + q(*p.statusAt) + " (현지 시각)" : ""));
    int targets = 0, current = 0, week = 0, old = 0, missing = 0, selectedRow = -1;
    const QSignalBlocker block(tasks_);
    tasks_->setRowCount(0);
    for (const auto &t : p.tasks) {
        const auto *e = lookup[t.id].entry;
        const bool include = target(e, begin), exact = e && p.statusAt && e->statusAt == *p.statusAt;
        targets += include;
        current += exact;
        week += e && !exact && inWeek(e, begin);
        old += e && !inWeek(e, begin);
        missing += !e;
        const auto filter = filter_->currentIndex();
        if ((filter == 0 && !include) || (filter == 1 && (!include || exact)) || (filter == 2 && !inWeek(e, begin)) ||
            (filter == 3 && e))
            continue;
        const auto query = search_->text().trimmed();
        if (!query.isEmpty() && !q(t.name).contains(query, Qt::CaseInsensitive) &&
            !q(t.id).contains(query, Qt::CaseInsensitive))
            continue;
        const auto row = tasks_->rowCount();
        tasks_->insertRow(row);
        const auto day = taskDayMinutes(p, t);
        const auto value =
            !e ? QString("미보고")
               : taskDurationText(e->actualDurationMinutes, day) + " / " +
                     (usesWork(t)
                          ? (e->remainingWorkUnitMinutes
                                 ? QString::number(double(*e->remainingWorkUnitMinutes) / 6000, 'g', 12) + "h 공수"
                                 : "공수 미보고")
                          : taskDurationText(e->remainingMinutes, day));
        QStringList cells{q(t.name),
                          !e                ? "착수 여부 미확인"
                          : e->actualFinish ? "완료 보고"
                          : e->actualStart  ? "진행 보고"
                                            : "미착수 보고",
                          reportState(e, p, begin),
                          e ? q(e->statusAt) : "—",
                          value,
                          e ? q(e->reporter) : "—"};
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            item->setToolTip(cells[c] + "\n작업 ID: " + q(t.id) + "\n전체 보고 이력 " +
                             QString::number(lookup[t.id].history) + "건 · 상태일 이후 보고는 현황에서 제외");
            tasks_->setItem(row, c, item);
        }
        tasks_->item(row, 0)->setData(Qt::UserRole, q(t.id));
        tasks_->item(row, 0)->setData(Qt::UserRole + 1, include && !exact);
        if (t.id == keep)
            selectedRow = row;
    }
    summary_->setText(QString("전체 %1 · 확인 대상 %2 · 현재 상태일 보고 %3 · 이번 주 앞선 보고 %4 · 이전 주 보고 %5 · "
                              "미보고 %6 · 표시 %7")
                          .arg(p.tasks.size())
                          .arg(targets)
                          .arg(current)
                          .arg(week)
                          .arg(old)
                          .arg(missing)
                          .arg(tasks_->rowCount()));
    if (!p.statusAt)
        summary_->setText(QString("전체 %1개 작업 · 보고 현황은 상태일을 선택한 뒤 표시합니다.").arg(p.tasks.size()));
    if (tasks_->rowCount()) {
        if (selectedRow < 0) {
            selectedRow = 0;
            for (int r = 0; r < tasks_->rowCount(); ++r)
                if (tasks_->item(r, 0)->data(Qt::UserRole + 1).toBool()) {
                    selectedRow = r;
                    break;
                }
        }
        tasks_->selectRow(selectedRow);
    }
    status_->setEnabled(!document_.readOnly() && !editing_);
    selected();
}
void WeeklyUpdateDialog::selected() {
    const auto id = selectedId();
    const auto &p = document_.project();
    const bool writable = p && p->statusAt && !document_.readOnly() && !editing_;
    edit_->setEnabled(writable && !id.empty());
    bool pending = false;
    for (int r = 0; r < tasks_->rowCount(); ++r)
        pending |= tasks_->item(r, 0)->data(Qt::UserRole + 1).toBool();
    next_->setEnabled(p && p->statusAt && !editing_ && pending);
    recalculate_->setEnabled(p && p->statusAt && !editing_);
    details_->setText("작업을 선택하세요. 입력 내용은 작업별로 저장하며 파일 저장은 주 창에서 별도로 수행합니다.");
    if (!p)
        return;
    for (const auto &t : p->tasks)
        if (t.id == id) {
            const auto *e = progressAsOf(*p, id);
            auto text = q(t.name) + " · " + taskDurationHelp(*p, t).section('\n', 0, 0);
            text += e ? "\n실제 시작 " + (e->actualStart ? q(*e->actualStart) : "미착수 보고") + " · 실제 완료 " +
                            (e->actualFinish ? q(*e->actualFinish) : "미완료 보고") +
                            "\n최근 보고 사유: " + q(e->reason)
                    : usesWork(t)
                        ? "\n선택한 상태일까지의 보고가 없습니다. 공수 기준 작업은 명시적 잔여 공수를 입력해야 합니다."
                        : "\n선택한 상태일까지의 보고가 없습니다. 계획 기간을 잔여 기본값으로 제안하며 제출 전 확인이 "
                          "필요합니다.";
            details_->setText(text);
            break;
        }
}
void WeeklyUpdateDialog::editSelected() {
    if (editing_ || !editProgress || !document_.project() || !document_.project()->statusAt || document_.readOnly())
        return;
    const auto first = selectedId();
    if (first.empty())
        return;
    std::vector<std::string> queue{first};
    if (continue_->isChecked()) {
        const auto start = tasks_->currentRow();
        for (int step = 1; step < tasks_->rowCount(); ++step) {
            const auto r = (start + step) % tasks_->rowCount();
            if (tasks_->item(r, 0)->data(Qt::UserRole + 1).toBool())
                queue.push_back(tasks_->item(r, 0)->data(Qt::UserRole).toString().toStdString());
        }
    }
    editing_ = true;
    int saved = 0;
    std::string last = first;
    for (const auto &id : queue) {
        if (!document_.project() || document_.sessionGeneration() != session_ || document_.readOnly() ||
            !document_.project()->statusAt)
            break;
        last = id;
        if (!editProgress(id, this, continue_->isChecked()))
            break;
        ++saved;
        reload(id);
    }
    editing_ = false;
    reload(last);
    notice_->setText(QString("이번 입력에서 %1건을 제출했습니다. 취소·미입력 작업은 자동 보고하지 않습니다.\n"
                             "입력 마치고 재예측으로 현재 일정을 계산하세요. 파일 저장은 별도입니다.")
                         .arg(saved));
}
} // namespace ppm
