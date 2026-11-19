#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "ui/TaskInput.h"
#include <QRegularExpression>
#include "domain/CalculationControl.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace ppm {
int taskDayMinutes(const PlanInput &plan, const Task &task) {
    if (task.calendarId.empty())
        return 480;
    const auto calendar = std::find_if(plan.calendars.begin(), plan.calendars.end(),
                                       [&](const auto &c) { return c.id == task.calendarId; });
    if (calendar == plan.calendars.end())
        return 0;
    int minutes = 0;
    for (const auto &period : calendar->periods)
        minutes += period.end - period.begin;
    return minutes > 0 && minutes <= 1440 ? minutes : 0;
}
QString taskDurationText(std::int64_t minutes, int dayMinutes) {
    // Only use a decimal when it round-trips to the exact stored working minute.
    if (dayMinutes > 0 && minutes % dayMinutes == 0)
        return QString::number(minutes / dayMinutes) + "d";
    if (dayMinutes > 0 && (minutes % dayMinutes) * 1000 % dayMinutes == 0)
        return QString::number(double(minutes) / dayMinutes, 'f', 3).remove(QRegularExpression("0+$")) + "d";
    if (minutes % 60 == 0)
        return QString::number(minutes / 60) + "h";
    return QString::number(minutes) + "m";
}
QString taskDurationHelp(const PlanInput &plan, const Task &task) {
    const auto day = taskDayMinutes(plan, task);
    QString name = "기본 월–금 달력";
    for (const auto &c : plan.calendars)
        if (c.id == task.calendarId)
            name = QString::fromStdString(c.name);
    return QString("%1 · %2\n숫자/일(d) · 시간(h) · 분(m), 예: 2d, 16h, 960m. 빈칸=미정, 0=마일스톤.\n"
                   "d는 달력의 하루 근무량이며 24시간 경과일이 아닙니다. 휴무·휴일·중단은 일정 계산에 반영됩니다.")
        .arg(name)
        .arg(day > 0 ? QString("1d=%1h (%2분)").arg(double(day) / 60, 0, 'g', 12).arg(day)
                     : QString("하루 근무량 확인 필요 · h/m으로 입력"));
}
std::optional<std::int64_t> taskDurationInput(const QString &value, int dayMinutes) {
    const auto text = value.trimmed();
    if (text.isEmpty())
        return {};
    bool okay = false;
    static thread_local const QRegularExpression pattern("^([0-9]+)(?:\\.([0-9]{1,6}))?\\s*(d|h|m|일|시간|분)?$",
                                                         QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(text);
    const auto unit = match.captured(3).toLower();
    const auto multiplier = unit == "h" || unit == "시간" ? 60 : unit == "m" || unit == "분" ? 1 : dayMinutes;
    const auto fraction = match.captured(2);
    std::int64_t scale = 1;
    for (qsizetype i = 0; i < fraction.size(); ++i)
        scale *= 10;
    const auto amount = (match.captured(1) + fraction).toLongLong(&okay);
    // Bound before multiplying, and reject sub-minute values rather than rounding them.
    if (!match.hasMatch() || !okay || multiplier <= 0 || multiplier > 1440 || amount > 5000000LL * scale / multiplier ||
        amount * multiplier % scale != 0)
        throw AppError(ErrorCode::TASKINPUT_001, {});
    return amount * multiplier / scale;
}
std::vector<Dependency> taskDependencyInput(const QString &value, const Project &p, const std::string &taskId) {
    std::vector<Dependency> edges;
    static thread_local const QRegularExpression separator("[\\s,]+"),
        relation("^([0-9]+)(FS|SS|FF|SF)?([+-][0-9]+(?:\\.[0-9]+)?[mhd])?$", QRegularExpression::CaseInsensitiveOption);
    for (const auto &token : value.trimmed().split(separator, Qt::SkipEmptyParts)) {
        calculationCheckpoint();
        const auto match = relation.match(token);
        bool okay = false;
        const auto number = match.captured(1).toULongLong(&okay);
        if (!match.hasMatch() || !okay || number < 1 || number > p.tasks.size())
            throw AppError(ErrorCode::TASKINPUT_002, {});
        const auto &predecessor = p.tasks[std::size_t(number - 1)];
        if (predecessor.id == taskId)
            throw AppError(ErrorCode::TASKINPUT_003, {});
        const auto lag = match.captured(3).toLower();
        double minutes = 0;
        if (!lag.isEmpty()) {
            minutes = lag.chopped(1).toDouble(&okay) * (lag.endsWith('d') ? 480 : lag.endsWith('h') ? 60 : 1);
            if (!okay || !std::isfinite(minutes) || std::abs(minutes) > 5000000 ||
                std::abs(minutes - std::round(minutes)) > 0.000001)
                throw AppError(ErrorCode::TASKINPUT_004, {});
        }
        edges.push_back({predecessor.id,
                         match.captured(2).isEmpty()
                             ? DependencyType::FS
                             : dependencyTypeFromToken(match.captured(2).toUpper().toUtf8().toStdString()),
                         std::int64_t(std::round(minutes))});
    }
    return edges;
}
QString encodeTaskTsv(const QList<QStringList> &rows) {
    QStringList lines;
    for (const auto &row : rows) {
        QStringList fields;
        for (auto field : row) {
            if (field.contains('"') || field.contains('\t') || field.contains('\n') || field.contains('\r')) {
                field.replace("\"", "\"\"");
                field = '"' + field + '"';
            }
            fields << field;
        }
        lines << fields.join('\t');
    }
    return lines.join('\n');
}
QList<QStringList> decodeTaskTsv(const QString &text) {
    constexpr qsizetype limit = 4 * 1024 * 1024;
    if (text.size() > limit || text.toUtf8().size() > limit)
        throw AppError(ErrorCode::TASKINPUT_005, {});
    if (text.isEmpty())
        throw AppError(ErrorCode::TASKINPUT_006, {});
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false, closed = false, started = false;
    qsizetype cells = 0;
    auto finishField = [&] {
        row << field;
        field.clear();
        closed = false;
        started = false;
        if (++cells > 30000)
            throw AppError(ErrorCode::TASKINPUT_007, {});
    };
    auto finishRow = [&] {
        finishField();
        if (rows.size() >= 10000)
            throw AppError(ErrorCode::TASKINPUT_008, {});
        if (!rows.isEmpty() && row.size() != rows.front().size())
            throw AppError(ErrorCode::TASKINPUT_009, {diagnosticArgument(std::to_string(rows.size() + 1))});
        rows << row;
        row.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (i % 4096 == 0)
            calculationCheckpoint();
        const auto c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else
                field += c;
            continue;
        }
        if (c == '\t') {
            finishField();
            continue;
        }
        if (c == '\r' || c == '\n') {
            finishRow();
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            continue;
        }
        if (closed)
            throw AppError(ErrorCode::TASKINPUT_010, {});
        if (c == '"' && !started) {
            quoted = true;
            started = true;
        } else {
            field += c;
            started = true;
        }
    }
    if (quoted)
        throw AppError(ErrorCode::TASKINPUT_011, {});
    if (started || closed || !field.isEmpty() || !row.isEmpty())
        finishRow();
    return rows;
}
} // namespace ppm
