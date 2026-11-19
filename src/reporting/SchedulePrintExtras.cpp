#include "reporting/SchedulePrintExtras.h"
#include "formatting/PlanTokens.h"
#include <QPainter>
#include <QTextLayout>
#include <QPainterPath>
#include <map>
#include <unordered_map>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QStringList wrap(const QString &s, const QFont &f, double width) {
    QTextLayout l(s, f);
    QTextOption o;
    o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    l.setTextOption(o);
    QStringList out;
    l.beginLayout();
    while (true) {
        auto line = l.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        out << s.mid(line.textStart(), line.textLength());
    }
    l.endLayout();
    return out.isEmpty() ? QStringList{QString{}} : out;
}
double stamp(const QString &s) {
    return QDate::fromString(s.left(10), Qt::ISODate).toJulianDay() +
           (s.mid(11, 2).toInt() * 60 + s.mid(14, 2).toInt()) / 1440.;
}
} // namespace
void prepareScheduleExtras(SchedulePrintLayout &out, const ScheduleResult &schedule) {
    const auto &p = *out.source.project;
    std::unordered_map<std::string, const Task *> tasks;
    std::unordered_map<std::string, const TaskSchedule *> dates;
    for (const auto &t : p.tasks)
        tasks[t.id] = &t;
    if (schedule.status == CalculationStatus::Valid)
        for (const auto &r : schedule.tasks)
            dates[r.taskId] = &r;
    if (out.options.wbs) {
        struct Group {
            std::size_t count = 0;
            QString start, finish, baseStart, baseFinish;
        };
        std::map<std::string, Group> groups;
        std::unordered_map<std::string, const WbsNode *> nodes;
        for (const auto &n : p.wbsNodes)
            nodes[n.id] = &n;
        std::size_t visits = 0;
        for (const auto &row : out.rows) {
            auto id = tasks.at(row.taskId)->wbsId;
            while (!id.empty()) {
                calculationCheckpoint();
                if (++visits > 1000000)
                    throw std::runtime_error("WBS 요약 탐색 한도 초과");
                auto &g = groups[id];
                ++g.count;
                if (dates.contains(row.taskId)) {
                    const auto *r = dates.at(row.taskId);
                    const auto a = q(r->start), b = q(r->finish);
                    if (g.start.isEmpty() || a < g.start)
                        g.start = a;
                    if (g.finish.isEmpty() || b > g.finish)
                        g.finish = b;
                }
                for (const auto &bar : row.bars)
                    if (bar.kind == ScheduleBarKind::Baseline) {
                        if (g.baseStart.isEmpty() || bar.start < g.baseStart)
                            g.baseStart = bar.start;
                        if (g.baseFinish.isEmpty() || bar.finish > g.baseFinish)
                            g.baseFinish = bar.finish;
                    }
                id = nodes.at(id)->parentId;
            }
        }
        std::vector<SchedulePrintRow> result;
        std::size_t chars = 0;
        for (const auto &n : p.wbsNodes)
            if (groups.contains(n.id)) {
                const auto &g = groups.at(n.id);
                QStringList path;
                auto id = n.id;
                while (!id.empty()) {
                    path.prepend(q(nodes.at(id)->name));
                    id = nodes.at(id)->parentId;
                }
                SchedulePrintRow r;
                r.summary = true;
                r.cells = QStringList(out.columns.size(), QString{});
                r.cells[0] = "WBS";
                r.cells[1] = path.join(" / ") + QString(" · 표시 %1작업").arg(g.count);
                for (int c = 2; c < r.cells.size(); ++c) {
                    auto key = out.columns[c].key;
                    r.cells[c] = key == "start" ? g.start : key == "finish" ? g.finish : QString("—");
                }
                if (!g.start.isEmpty())
                    r.bars.push_back({g.start, g.finish, ScheduleBarKind::Summary});
                if (!g.baseStart.isEmpty())
                    r.bars.push_back({g.baseStart, g.baseFinish, ScheduleBarKind::Baseline});
                for (int c = 0; c < r.cells.size(); ++c) {
                    chars += r.cells[c].size();
                    if (chars > 16 * 1024 * 1024)
                        throw std::runtime_error("WBS 요약 텍스트 한도 초과");
                    auto lines = wrap(r.cells[c], out.font, out.widths[c] - 6);
                    r.maxLines = std::max(r.maxLines, int(lines.size()));
                    r.lines.push_back(lines);
                }
                result.push_back(std::move(r));
            }
        out.summaryCount = result.size();
        result.insert(result.end(), std::make_move_iterator(out.rows.begin()), std::make_move_iterator(out.rows.end()));
        out.rows = std::move(result);
    }
    if (!out.options.relations)
        return;
    std::unordered_map<std::string, std::size_t> printed;
    for (std::size_t i = 0; i < out.rows.size(); ++i)
        if (!out.rows[i].summary)
            printed[out.rows[i].taskId] = i;
    for (const auto &t : p.tasks)
        for (const auto &d : dependencies(t)) {
            calculationCheckpoint();
            if (!printed.contains(t.id) && !printed.contains(d.predecessorId))
                continue;
            if (!printed.contains(t.id) || !printed.contains(d.predecessorId) || !dates.contains(t.id) ||
                !dates.contains(d.predecessorId)) {
                ++out.omittedRelations;
                continue;
            }
            if (out.relations.size() >= 100000)
                throw std::runtime_error("인쇄 관계 한도 초과");
            const auto *a = dates.at(d.predecessorId), *b = dates.at(t.id);
            const bool sf = d.type == DependencyType::FS || d.type == DependencyType::FF,
                       ts = d.type == DependencyType::FS || d.type == DependencyType::SS;
            const auto from = printed.at(d.predecessorId), to = printed.at(t.id);
            out.relations.push_back({from, to, d.type, d.lagMinutes, q(sf ? a->finish : a->start),
                                     q(ts ? b->remainingStart.value_or(b->start) : b->finish),
                                     out.rows[from].cells[0] + " → " + out.rows[to].cells[0] + " " +
                                         q(enumToken(d.type)) + " " + QString::number(d.lagMinutes) + "m"});
            const auto *fact = p.statusAt ? progressAsOf(p, t.id) : nullptr;
            auto &relation = out.relations.back();
            relation.inactive = p.statusAt && p.progressPolicy && p.progressPolicy->policy == overrideProgressPolicy &&
                                fact && fact->actualStart && !fact->actualFinish;
            if (b->completedActual) {
                relation.inactive = true;
                relation.label += " · 완료 실적 참고";
            } else if (relation.inactive)
                relation.label += " · 진척 정책 비활성";
        }
}
void paintScheduleRelations(QPainter &p, const SchedulePrintLayout &l, const SchedulePrintPage &page) {
    if (page.provenance || l.relations.empty())
        return;
    std::map<std::size_t, double> rows;
    double y = l.bodyTop;
    for (const auto &f : page.fragments) {
        if (f.line == 0)
            rows[f.row] = y + f.height / 2;
        y += f.height;
    }
    const double left = l.content.left() + l.tableWidth, width = l.content.width() - l.tableWidth;
    const double first = page.from.toJulianDay(), span = page.from.daysTo(page.to) + 1;
    auto x = [&](const QString &s) {
        return std::clamp(left + (stamp(s) - first) / span * width, left + 2, left + width - 2);
    };
    p.save();
    p.setClipRect(QRectF(left, l.bodyTop, width, l.bodyHeight));
    p.setPen(QPen(QColor("#665080"), 0.8));
    QFont f = l.font;
    f.setPointSize(7);
    p.setFont(f);
    for (const auto &e : l.relations) {
        const bool a = rows.contains(e.from), b = rows.contains(e.to);
        if (!a && !b)
            continue;
        p.setPen(QPen(QColor("#665080"), 0.8, e.inactive ? Qt::DashLine : Qt::SolidLine));
        if (a && b) {
            QPointF s(x(e.start), rows.at(e.from)), t(x(e.finish), rows.at(e.to));
            QPainterPath path(s);
            const double mid = (s.x() + t.x()) / 2;
            path.lineTo(mid, s.y());
            path.lineTo(mid, t.y());
            path.lineTo(t);
            p.drawPath(path);
            const bool targetStart = e.type == DependencyType::FS || e.type == DependencyType::SS;
            p.drawLine(t, t + QPointF(targetStart ? -4 : 4, -3));
            p.drawLine(t, t + QPointF(targetStart ? -4 : 4, 3));
        } else {
            const auto yy = rows.at(a ? e.from : e.to);
            p.drawText(QRectF(left + 3, yy - 14, width - 6, 12), e.label + " · 다른 행 페이지");
        }
    }
    p.restore();
}
} // namespace ppm
