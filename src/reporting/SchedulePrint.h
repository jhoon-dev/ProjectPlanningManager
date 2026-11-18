#pragma once
#include "calculation/Scheduler.h"
#include "reporting/GanttStyle.h"
#include <QDate>
#include <QFont>
#include <QPageLayout>
#include <QPageSize>
#include <QRectF>
#include <QStringList>
#include <set>
class QPainter;
class QPagedPaintDevice;
namespace jobs {
struct SchedulePrintColumn {
    QString key, title;
    double width = 80;
};
struct SchedulePrintInputRow {
    std::string taskId;
    int number = 0;
    QStringList cells;
};
struct SchedulePrintSource {
    std::shared_ptr<const Project> project;
    std::uint64_t session = 0;
    bool unsaved = false;
    QString scope, generatedAt, printId, baselineId;
    std::vector<SchedulePrintColumn> columns;
    std::vector<SchedulePrintInputRow> rows;
    std::set<std::string> selected;
};
struct SchedulePrintOptions {
    QPageLayout page{QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(10, 10, 10, 10),
                     QPageLayout::Millimeter};
    QFont font;
    int fontPoints = 9, tablePercent = 52, nameWidth = 140, daysPerPage = 14;
    bool selectedOnly = false, fitTime = true, baseline = true;
    bool relations = false, wbs = false;
    bool meetingSummary = false;
    QDate from, to; // Invalid pair means automatic visible schedule/baseline extent.
};
enum class ScheduleBarKind { Plan, Critical, Actual, Remaining, Baseline, Summary };
struct SchedulePrintBar {
    QString start, finish;
    ScheduleBarKind kind = ScheduleBarKind::Plan;
};
struct SchedulePrintRow {
    std::string taskId;
    bool summary = false;
    QStringList cells;
    std::vector<QStringList> lines;
    int maxLines = 1;
    std::vector<SchedulePrintBar> bars;
    GanttProgress progress = GanttProgress::Plan;
    bool critical = false;
    std::optional<std::int64_t> finishVariance;
};
struct SchedulePrintFragment {
    std::size_t row = 0;
    int line = 0, count = 0;
    double height = 0;
};
struct SchedulePrintPage {
    std::vector<int> columns;
    std::vector<SchedulePrintFragment> fragments;
    QDate from, to;
    int rowPage = 0, columnPage = 0, timePage = 0;
    bool provenance = false;
};
struct SchedulePrintRelation {
    std::size_t from = 0, to = 0;
    DependencyType type = DependencyType::FS;
    std::int64_t lag = 0;
    QString start, finish, label;
    bool inactive = false;
};
struct SchedulePrintLayout {
    std::vector<SchedulePrintRelation> relations;
    std::size_t omittedRelations = 0, summaryCount = 0;
    SchedulePrintSource source;
    SchedulePrintOptions options;
    QFont font;
    QRectF content;
    double tableWidth = 0, lineHeight = 0, bodyTop = 0, bodyHeight = 0;
    std::vector<double> widths;
    std::vector<SchedulePrintColumn> columns;
    std::vector<SchedulePrintRow> rows;
    std::vector<SchedulePrintPage> pages;
    CalculationStatus status = CalculationStatus::Incomplete;
    QString mode;
    QString meetingContext, meetingResult;
    std::size_t taskCount = 0;
};
SchedulePrintLayout buildSchedulePrint(const SchedulePrintSource &, const SchedulePrintOptions &,
                                       const std::shared_ptr<CalculationControl> &control = {});
void paintSchedulePage(QPainter &, const SchedulePrintLayout &, int page);
int paintSchedule(QPagedPaintDevice &, const SchedulePrintLayout &, int first = 1, int last = 0,
                  const std::shared_ptr<CalculationControl> &control = {}, int copies = 1, bool reverse = false,
                  bool collate = true);
int exportSchedulePdf(const SchedulePrintLayout &, const QString &path,
                      const std::shared_ptr<CalculationControl> &control = {});
} // namespace jobs
