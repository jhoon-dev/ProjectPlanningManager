#include "domain/ResourceAvailability.h"
#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlFormula.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlCodes.h"
#include "domain/XmlRollDown.h"
#include "domain/ResourcePlan.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "formatting/Money.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/ResourceCurveExchange.h"
#include "calculation/ResourcePlanning.h"
#include "persistence/XerActualCosts.h"
#include "domain/Classification.h"
#include "persistence/UdfMapping.h"
#include "domain/CalculationControl.h"
#include "persistence/ExchangePackage.h"
#include <QCryptographicHash>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringConverter>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace ppm {
namespace {
constexpr qsizetype byteLimit = 16 * 1024 * 1024;
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}

QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
void diagnostic(QJsonArray &out, const QString &path, const QString &message) {
    out.append(QJsonObject{{"path", path}, {"message", message}});
}
QString encodingName(XerEncoding e) {
    return e == XerEncoding::Utf8 ? "UTF-8" : "Windows-1252";
}
bool forbiddenTextCharacter(QChar c) {
    const auto category = c.category();
    return category == QChar::Other_Control || category == QChar::Separator_Line ||
           category == QChar::Separator_Paragraph;
}
QString decode(const QByteArray &bytes, XerEncoding encoding) {
    if (bytes.size() > byteLimit)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_001, {});
    if (encoding == XerEncoding::Utf8) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString text = decoder.decode(bytes);
        if (decoder.hasError())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_002, {});
        if (text.startsWith(QChar(0xfeff)))
            text.remove(0, 1);
        return text;
    }
    if (bytes.startsWith("\xef\xbb\xbf"))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_003, {});
    static constexpr char16_t cp[32] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                        0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    QString out;
    out.reserve(bytes.size());
    for (unsigned char c : bytes) {
        if (c >= 0x80 && c <= 0x9f) {
            auto v = cp[c - 0x80];
            if (!v)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_004, {});
            out += QChar(v);
        } else
            out += QChar(c);
    }
    return out;
}
qint64 integer(const QString &s, const QString &path, qint64 lo = 0, qint64 hi = 2147483647) {
    static const QRegularExpression re("^-?[0-9]+$");
    bool ok = false;
    auto n = s.toLongLong(&ok);
    if (!re.match(s).hasMatch() || !ok || n < lo || n > hi)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_005, {diagnosticArgument(path)});
    return n;
}
qint64 micro(const QString &s, const QString &path, bool negative = false) {
    static const QRegularExpression re("^(-?)([0-9]+)(?:\\.([0-9]{1,6}))?$");
    auto m = re.match(s);
    if (!m.hasMatch() || (!negative && !m.captured(1).isEmpty()))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_006, {diagnosticArgument(path)});
    auto n = integer(m.captured(2), path, 0, 1000000000) * 1000000 + m.captured(3).leftJustified(6, '0').toLongLong();
    return m.captured(1).isEmpty() ? n : -n;
}
qint64 minutes(const QString &s, const QString &path, QJsonArray &diagnostics, bool negative = false) {
    auto scaled = micro(s, path, negative) * 60;
    auto rounded = (scaled + (scaled < 0 ? -500000 : 500000)) / 1000000;
    if (std::abs(scaled - rounded * 1000000) > 30 || std::abs(rounded) > 5000000)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_007, {diagnosticArgument(path)});
    if (scaled != rounded * 1000000)
        diagnostic(diagnostics, path, "6자리 소수 시간 표현의 양자화 오차를 정수 분으로 보정");
    return rounded;
}
int units(const QString &s, const QString &path, bool allowZero) {
    auto m = micro(s, path);
    if (m % 10000 || m > 1000000000 || (!allowZero && !m))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_008, {diagnosticArgument(path)});
    return int(m / 10000);
}
QString stamp(const QString &s, const QString &path) {
    if (!validStamp(str(s)))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_009, {diagnosticArgument(path)});
    return s;
}
using Row = XerRow;
using Table = XerTable;
struct File {
    QStringList header;
    std::map<QString, Table> tables;
    QJsonArray normalizations;
};
QString path(const Table &t, const Row &r, const QString &field) {
    return t.name + "/line-" + QString::number(r.line) + "/" + field;
}
QString get(const Table &t, const Row &r, const QString &field) {
    t.used.insert(field);
    auto i = t.fields.indexOf(field);
    auto value = i < 0 ? QString{} : r.cells.at(i);
    if (value.contains(QChar(0x7f))) {
        if (field != "clndr_data" && !field.endsWith("_descr") && !field.endsWith("_memo"))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_012, {diagnosticArgument(path(t, r, field))});
        value.replace(QChar(0x7f), QChar('\n'));
    }
    return value;
}
QString id(const Table &t, const Row &r, const QString &field) {
    return QString::number(integer(get(t, r, field), path(t, r, field)));
}
File parse(const QString &text) {
    File file;
    Table *current = nullptr;
    bool ended = false, header = false;
    qsizetype totalCells = 0;
    if (text.count('\n') > 300001)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_010, {});
    auto lines = text.split('\n', Qt::KeepEmptyParts);
    static const QRegularExpression tableName("^[A-Z][A-Z0-9_]{0,127}$"), fieldName("^[A-Za-z][A-Za-z0-9_]{0,127}$");
    int lineNumber = 0;
    for (auto line : lines) {
        calculationCheckpoint();
        ++lineNumber;
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (ended)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_011, {});
        for (auto c : line)
            if (forbiddenTextCharacter(c) && c != '\t' && !(c == QChar(0x7f) && line.startsWith("%R\t")))
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_012, {diagnosticArgument(QString::number(lineNumber))});
        if (line.count('\t') > 256)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_013, {});
        const auto cells = line.split('\t', Qt::KeepEmptyParts);
        totalCells += cells.size();
        if (totalCells > 1000000)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_014, {});
        for (const auto &c : cells)
            if (c.size() > 1024 * 1024)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_015, {});
        if (!header) {
            if (cells.size() != 9 || cells[0] != "ERMHDR" ||
                !QRegularExpression("^[0-9]+(?:\\.[0-9]+){1,2}$").match(cells[1]).hasMatch())
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_016, {});
            file.header = cells;
            header = true;
            continue;
        }
        if (cells[0] == "%T") {
            if (cells.size() != 2 || !tableName.match(cells[1]).hasMatch() || file.tables.contains(cells[1]) ||
                (current && current->fields.isEmpty()) || file.tables.size() >= 128)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_017, {});
            auto [it, inserted] = file.tables.emplace(cells[1], Table{cells[1], {}, {}, {}});
            (void)inserted;
            current = &it->second;
        } else if (cells[0] == "%F") {
            if (!current || !current->fields.isEmpty() || cells.size() < 2)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_018, {});
            std::set<QString> names;
            for (qsizetype i = 1; i < cells.size(); ++i)
                if (!fieldName.match(cells[i]).hasMatch() || !names.insert(cells[i]).second)
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_019, {});
            current->fields = cells.mid(1);
        } else if (cells[0] == "%R") {
            if (!current || current->fields.isEmpty() || cells.size() > current->fields.size() + 1)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_020, {diagnosticArgument(QString::number(lineNumber))});
            auto values = cells.mid(1);
            if (values.size() < current->fields.size()) {
                totalCells += current->fields.size() - values.size();
                if (totalCells > 1000000)
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_014, {});
                diagnostic(file.normalizations, current->name + "/line-" + QString::number(lineNumber),
                           "생략된 행 끝 필드를 빈 값으로 복원 · 필수 값 검증 유지");
                while (values.size() < current->fields.size())
                    values.append(QString{});
            }
            if (line.contains(QChar(0x7f)))
                diagnostic(file.normalizations, current->name + "/line-" + QString::number(lineNumber),
                           "DEL 메모 줄바꿈 표기 · 사용 필드는 줄바꿈으로 해석 · 원본 바이트 보존");
            current->rows.push_back({std::move(values), lineNumber});
        } else if (cells[0] == "%E") {
            if (cells.size() != 1 || (current && current->fields.isEmpty()))
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_021, {});
            ended = true;
        } else
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_022, {diagnosticArgument(QString::number(lineNumber))});
    }
    if (!header || !ended)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_023, {});
    return file;
}
const Table &table(File &file, const char *name) {
    auto [it, inserted] = file.tables.try_emplace(name, Table{name, {}, {}, {}});
    (void)inserted;
    const std::size_t limit =
        (it->first == "TASKPRED" || it->first == "TASKRSRC" || it->first == "TASKACTV" || it->first == "UDFVALUE")
            ? 100000
            : 10000;
    if (it->second.rows.size() > limit)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_024, {diagnosticArgument(QString::fromLatin1(name))});
    return it->second;
}
QByteArray serializeXer(const File &file) {
    QByteArray bytes("\xef\xbb\xbf");
    auto line = [&](const QStringList &cells) {
        for (const auto &cell : cells) {
            if (cell.size() > 1024 * 1024)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_075);
            for (const auto c : cell)
                if (forbiddenTextCharacter(c))
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_076);
        }
        bytes += cells.join('\t').toUtf8() + "\r\n";
        if (bytes.size() > byteLimit)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_077);
    };
    line(file.header);
    for (const auto &[name, t] : file.tables) {
        if (t.fields.empty())
            continue;
        calculationCheckpoint();
        line({"%T", name});
        QStringList fields{"%F"};
        fields.append(t.fields);
        line(fields);
        for (const auto &r : t.rows) {
            QStringList cells{"%R"};
            cells.append(r.cells);
            line(cells);
        }
    }
    line({"%E"});
    return bytes;
}
void unique(const Table &t, const QString &field) {
    std::set<QString> seen;
    for (const auto &r : t.rows) {
        calculationCheckpoint();
        if (!seen.insert(id(t, r, field)).second)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_025, {diagnosticArgument(path(t, r, field))});
    }
}
void projectRef(const Table &t, const Row &r, const QString &project, const QString &field = "proj_id") {
    if (id(t, r, field) != project)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_026, {diagnosticArgument(path(t, r, field))});
}
// All identity/ownership checks precede pruning, so selection cannot bypass input limits.
std::vector<XerProjectInfo> projectInventory(File &file) {
    for (const auto &[name, key] :
         std::initializer_list<std::pair<const char *, const char *>>{{"PROJECT", "proj_id"},
                                                                      {"TASK", "task_id"},
                                                                      {"PROJWBS", "wbs_id"},
                                                                      {"TASKPRED", "task_pred_id"},
                                                                      {"TASKRSRC", "taskrsrc_id"},
                                                                      {"CALENDAR", "clndr_id"},
                                                                      {"RSRC", "rsrc_id"},
                                                                      {"RSRCRATE", "rsrc_rate_id"},
                                                                      {"ACTVTYPE", "actv_code_type_id"},
                                                                      {"ACTVCODE", "actv_code_id"},
                                                                      {"UDFTYPE", "udf_type_id"}})
        unique(table(file, name), key);
    const auto &projects = table(file, "PROJECT");
    const auto &udfTypes = table(file, "UDFTYPE");
    const auto &udfValues = table(file, "UDFVALUE");
    std::map<QString, QString> udfOwners, taskOwners, assignmentOwners;
    std::set<QString> projectIds, resourceIds;
    for (const auto &row : projects.rows)
        projectIds.insert(id(projects, row, "proj_id"));
    const auto &allTasks = table(file, "TASK"), &allResources = table(file, "RSRC");
    for (const auto &row : allTasks.rows)
        taskOwners[id(allTasks, row, "task_id")] = id(allTasks, row, "proj_id");
    for (const auto &row : allResources.rows)
        resourceIds.insert(id(allResources, row, "rsrc_id"));
    const auto &allAssignments = table(file, "TASKRSRC");
    for (const auto &row : allAssignments.rows)
        assignmentOwners[id(allAssignments, row, "taskrsrc_id")] = id(allAssignments, row, "proj_id");
    for (const auto &row : udfTypes.rows)
        udfOwners[id(udfTypes, row, "udf_type_id")] = get(udfTypes, row, "table_name");
    std::set<std::tuple<QString, QString, QString>> uniqueValues;
    for (const auto &row : udfValues.rows) {
        const auto type = id(udfValues, row, "udf_type_id"), fk = id(udfValues, row, "fk_id");
        auto pid = get(udfValues, row, "proj_id");
        pid = pid.isEmpty() ? "0" : id(udfValues, row, "proj_id");
        if (!udfOwners.contains(type) || (pid != "0" && !projectIds.contains(pid)) ||
            !uniqueValues.emplace(type, pid, fk).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDFVALUE 정의·프로젝트 참조 또는 중복"});
        const auto &owner = udfOwners.at(type);
        if ((owner == "TASK" && (!taskOwners.contains(fk) || taskOwners.at(fk) != pid)) ||
            (owner == "RSRC" && !resourceIds.contains(fk)) ||
            (owner == "TASKRSRC" && (!assignmentOwners.contains(fk) || assignmentOwners.at(fk) != pid)) ||
            (owner == "PROJECT" && (!projectIds.contains(fk) || (pid != "0" && pid != fk))))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDFVALUE 소유 객체 참조"});
    }
    if (projects.rows.empty())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_042);
    std::vector<XerProjectInfo> result;
    std::map<QString, std::size_t> indices;
    for (const auto &r : projects.rows) {
        calculationCheckpoint();
        auto pid = id(projects, r, "proj_id");
        indices.emplace(pid, result.size());
        auto code = get(projects, r, "proj_short_name");
        result.push_back({pid, code, code, get(projects, r, "plan_start_date")});
    }
    for (const char *name : {"TASK", "PROJWBS", "TASKRSRC", "SCHEDOPTIONS", "TASKACTV"}) {
        const auto &t = table(file, name);
        for (const auto &r : t.rows) {
            calculationCheckpoint();
            const auto owner = id(t, r, "proj_id");
            if (!indices.contains(owner))
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_026, {diagnosticArgument(path(t, r, "proj_id"))});
            auto &info = result[indices.at(owner)];
            if (t.name == "TASK")
                ++info.tasks;
            if (t.name == "PROJWBS" && get(t, r, "proj_node_flag") == "Y")
                info.name = get(t, r, "wbs_name");
        }
    }
    const auto &links = table(file, "TASKPRED");
    for (const auto &r : links.rows) {
        calculationCheckpoint();
        const auto to = id(links, r, "proj_id"), from = id(links, r, "pred_proj_id");
        if (to != from) {
            if (indices.contains(to))
                ++result[indices.at(to)].externalIncoming;
            if (indices.contains(from))
                ++result[indices.at(from)].externalOutgoing;
        }
    }
    validateXerActualReferences(file.tables);
    return result;
}
QJsonObject projectScope(File &file, const QString &selected, std::size_t projectCount) {
    std::map<QString, std::size_t> before;
    for (const char *name :
         {"PROJECT", "TASK", "PROJWBS", "TASKPRED", "TASKRSRC", "SCHEDOPTIONS", "RSRC", "RSRCRATE", "CALENDAR",
          "ACTVTYPE", "ACTVCODE", "TASKACTV", "UDFTYPE", "UDFVALUE", "PROJCOST", "FINDATES", "TASKFIN", "TRSRCFIN"})
        before[name] = table(file, name).rows.size();
    auto keep = [&](const char *name, auto predicate) {
        auto &t = file.tables.at(name);
        std::erase_if(t.rows, [&](const Row &r) {
            calculationCheckpoint();
            return !predicate(t, r);
        });
    };
    keep("TASKPRED", [&](const Table &t, const Row &r) {
        const auto to = id(t, r, "proj_id"), from = id(t, r, "pred_proj_id");
        if ((to == selected || from == selected) && to != from)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_026,
                           {diagnosticArgument(path(t, r, "proj_id") + " " + from + " -> " + to)},
                           ErrorContext{.projectId = str(selected), .path = str(path(t, r, "proj_id"))});
        return to == selected;
    });
    for (const char *name :
         {"PROJECT", "TASK", "PROJWBS", "TASKRSRC", "SCHEDOPTIONS", "TASKACTV", "PROJCOST", "TASKFIN", "TRSRCFIN"})
        keep(name, [&](const Table &t, const Row &r) { return id(t, r, "proj_id") == selected; });
    std::set<QString> financialIds;
    for (const auto *name : {"TASKFIN", "TRSRCFIN"}) {
        const auto &t = table(file, name);
        for (const auto &r : t.rows)
            financialIds.insert(id(t, r, "fin_dates_id"));
    }
    keep("FINDATES", [&](const Table &t, const Row &r) { return financialIds.contains(id(t, r, "fin_dates_id")); });
    keep("ACTVTYPE", [&](const Table &t, const Row &r) {
        const auto owner = get(t, r, "proj_id");
        return owner.isEmpty() || id(t, r, "proj_id") == "0" || id(t, r, "proj_id") == selected;
    });
    std::set<QString> types;
    for (const auto &r : table(file, "ACTVTYPE").rows)
        types.insert(id(table(file, "ACTVTYPE"), r, "actv_code_type_id"));
    keep("ACTVCODE", [&](const Table &t, const Row &r) { return types.contains(id(t, r, "actv_code_type_id")); });
    std::set<QString> resources, calendars;
    const auto &assignments = table(file, "TASKRSRC");
    for (const auto &r : assignments.rows)
        resources.insert(id(assignments, r, "rsrc_id"));
    keep("RSRC", [&](const Table &t, const Row &r) { return resources.contains(id(t, r, "rsrc_id")); });
    if (resources.size() != file.tables.at("RSRC").rows.size())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_072);
    keep("RSRCRATE", [&](const Table &t, const Row &r) { return resources.contains(id(t, r, "rsrc_id")); });
    std::map<QString, QString> udfOwners;
    for (const auto &r : file.tables.at("UDFTYPE").rows)
        udfOwners[id(file.tables.at("UDFTYPE"), r, "udf_type_id")] = get(file.tables.at("UDFTYPE"), r, "table_name");
    std::set<QString> udfUsed;
    keep("UDFVALUE", [&](const Table &t, const Row &r) {
        const auto type = id(t, r, "udf_type_id"), fk = id(t, r, "fk_id"), pid = get(t, r, "proj_id");
        const auto owner = udfOwners.at(type);
        const bool selectedValue = owner == "PROJECT" ? fk == selected
                                   : owner == "RSRC"  ? resources.contains(fk) && (pid.isEmpty() || pid == "0" ||
                                                                                  id(t, r, "proj_id") == selected)
                                                      : !pid.isEmpty() && id(t, r, "proj_id") == selected;
        if (selectedValue)
            udfUsed.insert(type);
        return selectedValue;
    });
    keep("UDFTYPE", [&](const Table &t, const Row &r) { return udfUsed.contains(id(t, r, "udf_type_id")); });
    const auto &projects = table(file, "PROJECT");
    const auto fallback = id(projects, projects.rows.front(), "clndr_id");
    calendars.insert(fallback);
    for (const char *name : {"TASK", "RSRC"}) {
        const auto &t = table(file, name);
        for (const auto &r : t.rows) {
            calculationCheckpoint();
            const auto ref = get(t, r, "clndr_id");
            calendars.insert(ref.isEmpty() ? fallback : id(t, r, "clndr_id"));
        }
    }
    const auto &ct = table(file, "CALENDAR");
    std::map<QString, const Row *> rows;
    for (const auto &r : ct.rows)
        rows.emplace(id(ct, r, "clndr_id"), &r);
    std::vector<QString> pending(calendars.begin(), calendars.end());
    for (std::size_t i = 0; i < pending.size(); ++i) {
        calculationCheckpoint();
        const auto ref = pending[i];
        if (!rows.contains(ref))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_045, {diagnosticArgument(ref)});
        const auto &r = *rows.at(ref);
        const auto owner = get(ct, r, "proj_id");
        if (!owner.isEmpty() && id(ct, r, "proj_id") != "0" && id(ct, r, "proj_id") != selected)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_044);
        const auto parent = get(ct, r, "base_clndr_id");
        if (!parent.isEmpty()) {
            const auto pid = id(ct, r, "base_clndr_id");
            if (calendars.insert(pid).second)
                pending.push_back(pid);
        }
    }
    keep("CALENDAR", [&](const Table &t, const Row &r) { return calendars.contains(id(t, r, "clndr_id")); });
    QJsonArray counts;
    for (const auto &[name, count] : before) {
        const auto kept = file.tables.at(name).rows.size();
        counts.append(QJsonObject{{"table", name},
                                  {"inputRows", qint64(count)},
                                  {"selectedRows", qint64(kept)},
                                  {"excludedRows", qint64(count - kept)}});
    }
    return {{"mode", "single-project-from-multi"},
            {"selectedProjectId", selected},
            {"sourceProjectCount", qint64(projectCount)},
            {"excludedProjectCount", qint64(projectCount - 1)},
            {"externalRelations", "reject-incoming-and-outgoing"},
            {"tables", counts}};
}
struct Record {
    QString name;
    std::map<QString, QString> attrs;
    std::vector<Record> children;
};
class CalendarParser {
public:
    explicit CalendarParser(const QString &s, int &count) : s_(s), count_(count) {}
    Record parse() {
        auto r = node(1);
        space();
        if (pos_ != s_.size())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_027, {});
        return r;
    }

private:
    const QString &s_;
    qsizetype pos_ = 0;
    int &count_;
    void space() {
        while (pos_ < s_.size() && s_[pos_].isSpace())
            ++pos_;
    }
    void expect(QChar c) {
        space();
        if (pos_ >= s_.size() || s_[pos_++] != c)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_028, {});
    }
    QString until(QChar c) {
        auto start = pos_;
        while (pos_ < s_.size() && s_[pos_] != c)
            ++pos_;
        if (pos_ == s_.size())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_029, {});
        return s_.mid(start, pos_ - start).trimmed();
    }
    Record node(int depth) {
        calculationCheckpoint();
        if (depth > 32 || ++count_ > 300000)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_030, {});
        expect('(');
        auto head = until('(');
        expect('(');
        auto divider = head.indexOf("||");
        if (divider < 1)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_031, {});
        integer(head.left(divider), "clndr_data/record");
        Record out;
        out.name = head.mid(divider + 2).trimmed();
        if (out.name.size() > 128)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_032, {});
        auto attrs = until(')');
        expect(')');
        if (!attrs.isEmpty()) {
            auto parts = attrs.split('|', Qt::KeepEmptyParts);
            if (parts.size() % 2)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_033, {});
            for (qsizetype i = 0; i < parts.size(); i += 2)
                if (!out.attrs.emplace(parts[i], parts[i + 1]).second)
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_034, {});
        }
        expect('(');
        space();
        while (pos_ < s_.size() && s_[pos_] == '(') {
            out.children.push_back(node(depth + 1));
            space();
        }
        expect(')');
        expect(')');
        return out;
    }
};
int clockMinute(const QString &s) {
    if (s == "24:00")
        return 1440;
    if (!QRegularExpression("^[0-2][0-9]:[0-5][0-9]$").match(s).hasMatch() || s.left(2).toInt() > 23)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_035, {});
    return s.left(2).toInt() * 60 + s.right(2).toInt();
}
const Record *find(const Record &r, const QString &name) {
    const Record *result = nullptr;
    for (const auto &c : r.children)
        if (c.name == name) {
            if (result)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_036, {diagnosticArgument(name)});
            result = &c;
        }
    return result;
}
void graph(const Project &p) {
    std::map<std::string, int> incoming;
    std::map<std::string, std::vector<std::string>> outgoing;
    for (const auto &t : p.tasks)
        incoming[t.id] = 0;
    for (const auto &t : p.tasks)
        for (const auto &pred : t.predecessors) {
            if (t.id == pred || !incoming.contains(pred))
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_037, {});
            ++incoming[t.id];
            outgoing[pred].push_back(t.id);
        }
    std::vector<std::string> ready;
    for (const auto &[id, n] : incoming)
        if (!n)
            ready.push_back(id);
    std::size_t visited = 0;
    while (!ready.empty()) {
        auto id = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto &to : outgoing[id])
            if (!--incoming[to])
                ready.push_back(to);
    }
    if (visited != p.tasks.size())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_038, {});
}
QJsonObject reportFor(const Project &p, bool importing) {
    return {{"contract", "ppm-xer-plan-v1"},
            {"direction", importing ? "import" : "export"},
            {"documentId", qs(p.documentId)},
            {"revision", QString::number(p.revision)},
            {"nativeSnapshotProfile", qs(p.calculationProfile)},
            {"targetPolicy", QJsonObject{{"calculationProfile", effortProfile},
                                         {"projectStartAnchor", "09:00"},
                                         {"lagCalendar", "successor-working-calendar"}}},
            {"compatibility",
             QJsonObject{{"import", importing ? "mapped-subset" : "not-applicable"},
                         {"originalPreservation", importing ? "exact-xer-bytes" : "exact-native-input-snapshot"},
                         {"ppmRecomputation", "unverified"},
                         {"externalRoundTrip", "unverified"}}}};
}
const std::map<QString, ConstraintType> constraintMap{{"", ConstraintType::ASAP},
                                                      {"CS_ALAP", ConstraintType::ALAP},
                                                      {"CS_MSOA", ConstraintType::SNET},
                                                      {"CS_MSOB", ConstraintType::SNLT},
                                                      {"CS_MEOA", ConstraintType::FNET},
                                                      {"CS_MEOB", ConstraintType::FNLT},
                                                      {"CS_MANDSTART", ConstraintType::MSO},
                                                      {"CS_MANDFIN", ConstraintType::MFO},
                                                      {"CS_MSO", ConstraintType::MSO},
                                                      {"CS_MEO", ConstraintType::MFO}};
} // namespace

QByteArray readPrimaveraXer(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_039, {diagnosticArgument(f.errorString())});
    if (f.size() > byteLimit)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_040, {});
    auto b = f.read(byteLimit + 1);
    if (f.error() != QFileDevice::NoError || b.size() > byteLimit || !f.atEnd())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_041, {});
    return b;
}
std::vector<XerProjectInfo> listPrimaveraXerProjects(const QByteArray &bytes, XerEncoding encoding) {
    calculationCheckpoint();
    auto file = parse(decode(bytes, encoding));
    return projectInventory(file);
}
PrimaveraXerExchange importPrimaveraXer(const QByteArray &bytes, const QString &sourceName, XerEncoding encoding,
                                        const QString &selectedProjectId) {
    EvaluationScope evaluation;
    calculationCheckpoint();
    auto file = parse(decode(bytes, encoding));
    const auto inventory = projectInventory(file);
    const auto selected =
        selectedProjectId.isEmpty() && inventory.size() == 1 ? inventory.front().id : selectedProjectId;
    if (std::none_of(inventory.begin(), inventory.end(), [&](const auto &info) { return info.id == selected; }))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_042);
    QJsonObject selection;
    if (inventory.size() > 1)
        selection = projectScope(file, selected, inventory.size());
    auto &projects = table(file, "PROJECT");
    const auto &pr = projects.rows.front();
    const auto projectId = id(projects, pr, "proj_id");
    auto code = get(projects, pr, "proj_short_name");
    Project p;
    p.documentId = str(QUuid::createUuid().toString(QUuid::WithoutBraces));
    p.calculationProfile = effortProfile;
    p.name = str(code);
    p.currency = str(file.header[8]);
    currencyDecimals(p.currency);
    auto start = stamp(get(projects, pr, "plan_start_date"), path(projects, pr, "plan_start_date"));
    p.startDate = str(start.left(10));
    QJsonArray diagnostics = file.normalizations;
    QJsonArray mapping;
    diagnostic(diagnostics, "PROJECT",
               "기간 기반 계획과 명시 실적·원가의 지원 부분을 투영합니다. 공수/자원 종속 일정과 P6 재계산 호환은 "
               "미검증입니다.");
    diagnostic(diagnostics, "CALENDAR",
               "수입 작업/자원에 달력을 명시 배정합니다. 새 native 작업은 PPM 기본 달력을 사용합니다. 기간 기반 "
               "일정은 작업 달력을 사용합니다.");
    if (start.mid(11) != "09:00")
        diagnostic(diagnostics, "PROJECT/plan_start_date", "시작 날짜는 유지 · native 시작 기준은 09:00");
    auto map = [&](const QString &kind, const QString &source, const std::string &target,
                   const QString &activityCode = QString{}) {
        mapping.append(
            QJsonObject{{"kind", kind}, {"xerId", source}, {"ppmId", qs(target)}, {"activityCode", activityCode}});
    };
    const auto &options = table(file, "SCHEDOPTIONS");
    if (options.rows.size() > 1)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_043, {});
    QString lagPolicy;
    if (!options.rows.empty()) {
        projectRef(options, options.rows[0], projectId);
        lagPolicy = get(options, options.rows[0], "sched_calendar_on_relationship_lag");
    }
    if (lagPolicy != "rcal_Successor")
        diagnostic(diagnostics, "SCHEDOPTIONS/sched_calendar_on_relationship_lag",
                   "원본 지연 달력 정책 " + (lagPolicy.isEmpty() ? "미기록" : lagPolicy) +
                       " → PPM 후행 작업 달력 기준; 계산 동등성 미검증");
    const auto &ct = table(file, "CALENDAR");
    unique(ct, "clndr_id");
    std::map<QString, const Row *> calendarRows;
    for (const auto &r : ct.rows) {
        auto ref = get(ct, r, "proj_id");
        if (!ref.isEmpty()) {
            const auto owner = QString::number(integer(ref, "CALENDAR/proj_id"));
            if (owner != "0" && owner != projectId)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_044, {});
        }
        calendarRows[id(ct, r, "clndr_id")] = &r;
    }
    std::map<QString, TaskCalendar> calendars;
    std::set<QString> resolving;
    int calendarNodes = 0;
    std::size_t holidayCount = 0;
    std::function<void(const QString &, int)> resolve = [&](const QString &calendarId, int depth) {
        if (calendars.contains(calendarId))
            return;
        if (depth > 32 || resolving.contains(calendarId) || !calendarRows.contains(calendarId))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_045, {diagnosticArgument(calendarId)});
        resolving.insert(calendarId);
        const auto &r = *calendarRows.at(calendarId);
        TaskCalendar c;
        c.id = "xer-cal-" + str(calendarId);
        c.name = str(get(ct, r, "clndr_name"));
        c.weekdays.clear();
        c.periods.clear();
        auto parent = get(ct, r, "base_clndr_id");
        if (!parent.isEmpty()) {
            parent = QString::number(integer(parent, "CALENDAR/base_clndr_id"));
            resolve(parent, depth + 1);
            c.holidays = calendars.at(parent).holidays;
        }
        auto data = get(ct, r, "clndr_data");
        CalendarParser parser(data, calendarNodes);
        auto root = parser.parse();
        if (root.name != "CalendarData" || !root.attrs.empty())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_046, {});
        for (const auto &section : root.children)
            if (section.name != "DaysOfWeek" && section.name != "Exceptions") {
                if (section.name == "VIEW")
                    diagnostic(diagnostics, "CALENDAR/" + calendarId + "/VIEW", "달력 표시 옵션은 원본에만 보존");
                else
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_047, {diagnosticArgument(section.name)});
            }
        auto *week = find(root, "DaysOfWeek");
        if (!week || week->children.size() != 7 || !week->attrs.empty())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_048, {});
        std::set<int> days;
        for (const auto &day : week->children) {
            int type = int(integer(day.name, "CALENDAR/day", 1, 7));
            if (!days.insert(type).second || !day.attrs.empty())
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_049, {});
            std::vector<WorkPeriod> periods;
            for (const auto &hours : day.children) {
                if (hours.attrs.size() != 2 || !hours.attrs.contains("s") || !hours.attrs.contains("f") ||
                    !hours.children.empty())
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_050, {});
                auto end = clockMinute(hours.attrs.at("f"));
                periods.push_back({clockMinute(hours.attrs.at("s")), end == 0 ? 1440 : end});
            }
            if (!periods.empty()) {
                c.weekdays.push_back(type == 1 ? 7 : type - 1);
                if (c.periods.empty())
                    c.periods = periods;
                else if (c.periods != periods)
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_051, {});
            }
        }
        std::sort(c.weekdays.begin(), c.weekdays.end());
        std::set<std::string> holidays(c.holidays.begin(), c.holidays.end()), ownDates;
        if (auto *exceptions = find(root, "Exceptions")) {
            if (!exceptions->attrs.empty())
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_052, {});
            for (const auto &exception : exceptions->children) {
                if (exception.attrs.size() != 1 || !exception.attrs.contains("d") || !exception.children.empty())
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_053, {});
                auto day = QDate(1899, 12, 30)
                               .addDays(integer(exception.attrs.at("d"), "CALENDAR/exception", -693593, 2958465));
                auto date = str(day.toString(Qt::ISODate));
                if (!validDate(date) || !ownDates.insert(date).second)
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_054, {});
                holidays.insert(date);
            }
        }
        if (holidays.size() > 10000)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_055, {});
        holidayCount += holidays.size();
        if (holidayCount > 100000)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_056, {});
        c.holidays.assign(holidays.begin(), holidays.end());
        calendars[calendarId] = c;
        p.calendars.push_back(c);
        map("calendar", calendarId, c.id);
        resolving.erase(calendarId);
    };
    for (const auto &[calendarId, r] : calendarRows) {
        (void)r;
        resolve(calendarId, 0);
    }
    auto defaultCalendar = id(projects, pr, "clndr_id");
    if (!calendars.contains(defaultCalendar))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_057, {});
    auto calendar = [&](const Table &t, const Row &r) {
        auto ref = get(t, r, "clndr_id");
        if (ref.isEmpty())
            ref = defaultCalendar;
        else
            ref = QString::number(integer(ref, path(t, r, "clndr_id")));
        if (!calendars.contains(ref))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_058, {diagnosticArgument(ref)});
        return calendars.at(ref).id;
    };
    const auto &wbs = table(file, "PROJWBS");
    unique(wbs, "wbs_id");
    std::map<QString, std::string> wbsIds;
    QString rootId;
    std::map<QString, QString> parents;
    for (const auto &r : wbs.rows) {
        projectRef(wbs, r, projectId);
        auto wid = id(wbs, r, "wbs_id");
        auto rootFlag = get(wbs, r, "proj_node_flag");
        if (rootFlag != "Y" && rootFlag != "N")
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_059, {});
        if (rootFlag == "Y") {
            if (!rootId.isEmpty())
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_060, {});
            rootId = wid;
            p.name = str(get(wbs, r, "wbs_name"));
            wbsIds[wid] = "";
            map("project-root", wid, "");
            if (!get(wbs, r, "parent_wbs_id").isEmpty())
                diagnostic(diagnostics, path(wbs, r, "parent_wbs_id"),
                           "프로젝트 루트의 외부 EPS 부모는 native 계층에 포함하지 않음");
        } else {
            wbsIds[wid] = "xer-wbs-" + str(wid);
            parents[wid] = get(wbs, r, "parent_wbs_id");
        }
    }
    if (rootId.isEmpty())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_061, {});
    for (const auto &r : wbs.rows) {
        auto wid = id(wbs, r, "wbs_id");
        if (wid == rootId)
            continue;
        auto parent = QString::number(integer(parents.at(wid), "PROJWBS/parent_wbs_id"));
        if (!wbsIds.contains(parent))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_062, {});
        p.wbsNodes.push_back({wbsIds.at(wid), str(get(wbs, r, "wbs_name")), wbsIds.at(parent)});
        map("wbs", wid, wbsIds.at(wid));
    }
    const auto &tasks = table(file, "TASK");
    unique(tasks, "task_id");
    std::map<QString, std::string> taskIds;
    for (const auto &r : tasks.rows) {
        projectRef(tasks, r, projectId);
        auto tid = id(tasks, r, "task_id"), wid = id(tasks, r, "wbs_id");
        if (!wbsIds.contains(wid))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_063, {});
        auto type = get(tasks, r, "task_type");
        if (type != "TT_Task" && type != "TT_Rsrc" && type != "TT_Mile" && type != "TT_FinMile")
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_064, {diagnosticArgument(type)});
        Task t;
        t.id = "xer-task-" + str(tid);
        t.name = str(get(tasks, r, "task_name"));
        t.wbsId = wbsIds.at(wid);
        t.calendarId = calendar(tasks, r);
        auto d = get(tasks, r, "target_drtn_hr_cnt");
        if (!d.isEmpty())
            t.durationMinutes = minutes(d, path(tasks, r, "target_drtn_hr_cnt"), diagnostics);
        if (type == "TT_Mile" || type == "TT_FinMile") {
            if (t.durationMinutes != 0)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_065, {});
            diagnostic(diagnostics, path(tasks, r, "task_type"), "시작/완료 마일스톤을 native 0분 이벤트로 투영");
        } else if (type == "TT_Rsrc")
            diagnostic(diagnostics, path(tasks, r, "task_type"), "자원 종속 작업을 기간 기반 작업으로 투영");
        auto durationType = get(tasks, r, "duration_type");
        if (durationType != "DT_FixedDrtn")
            diagnostic(diagnostics, path(tasks, r, "duration_type"),
                       "작업 유형/공수 유지 규칙은 기간 기반 고정 기간으로 투영");
        if (!get(tasks, r, "cstr_type2").isEmpty() || !get(tasks, r, "cstr_date2").isEmpty())
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_066, {});
        auto constraint = get(tasks, r, "cstr_type");
        if (!constraintMap.contains(constraint))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_067, {diagnosticArgument(constraint)});
        t.constraintType = constraintMap.at(constraint);
        auto at = get(tasks, r, "cstr_date");
        if (t.constraintType != ConstraintType::ASAP && t.constraintType != ConstraintType::ALAP)
            t.constraintAt = str(stamp(at, path(tasks, r, "cstr_date")));
        else if (!at.isEmpty())
            diagnostic(diagnostics, path(tasks, r, "cstr_date"), "날짜 없는 제약의 원본 시각은 적용하지 않음");
        if (constraint == "CS_MSO" || constraint == "CS_MEO")
            diagnostic(diagnostics, path(tasks, r, "cstr_type"),
                       "Start/Finish On을 native 하드 고정 날짜로 투영 · 의미 차이 있음");
        taskIds[tid] = t.id;
        map("task", tid, t.id, get(tasks, r, "task_code"));
        p.tasks.push_back(std::move(t));
    }
    std::map<std::string, std::size_t> taskIndex;
    for (std::size_t i = 0; i < p.tasks.size(); ++i)
        taskIndex[p.tasks[i].id] = i;
    const auto &links = table(file, "TASKPRED");
    unique(links, "task_pred_id");
    for (const auto &r : links.rows) {
        projectRef(links, r, projectId);
        projectRef(links, r, projectId, "pred_proj_id");
        auto to = id(links, r, "task_id"), from = id(links, r, "pred_task_id");
        if (!taskIds.contains(to) || !taskIds.contains(from))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_068, {});
        auto type = get(links, r, "pred_type");
        if (type != "PR_FS" && type != "PR_SS" && type != "PR_FF" && type != "PR_SF")
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_069, {});
        auto lag = minutes(get(links, r, "lag_hr_cnt"), path(links, r, "lag_hr_cnt"), diagnostics, true);
        auto &t = p.tasks[taskIndex.at(taskIds.at(to))];
        t.predecessors.push_back(taskIds.at(from));
        if (type != "PR_FS" || lag)
            t.dependencyRules.push_back({taskIds.at(from), dependencyTypeFromToken(str(type.mid(3))), lag});
    }
    const auto &resources = table(file, "RSRC");
    unique(resources, "rsrc_id");
    std::map<QString, std::string> resourceIds;
    const auto &rates = table(file, "RSRCRATE");
    unique(rates, "rsrc_rate_id");
    std::map<QString, std::vector<const Row *>> rateRows;
    for (const auto &r : rates.rows)
        rateRows[id(rates, r, "rsrc_id")].push_back(&r);
    for (const auto &r : resources.rows) {
        auto rid = id(resources, r, "rsrc_id");
        auto type = get(resources, r, "rsrc_type");
        if (type != "RT_Labor" && type != "RT_Equip")
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_070, {diagnosticArgument(type)});
        Resource resource;
        resource.id = "xer-resource-" + str(rid);
        resource.name = str(get(resources, r, "rsrc_name"));
        resource.type = type == "RT_Equip" ? "equipment" : "labor";
        resource.calendarId = calendar(resources, r);
        resource.maxUnits.reset();
        const auto &rows = rateRows[rid];
        if (rows.size() == 1) {
            const auto &rate = *rows.front();
            auto at = get(rates, rate, "start_date"), capacity = get(rates, rate, "max_qty_per_hr");
            if (!at.isEmpty())
                stamp(at, path(rates, rate, "start_date"));
            if (!capacity.isEmpty() && (at.isEmpty() || at <= qs(p.startDate) + " 09:00"))
                resource.maxUnits = units(capacity, path(rates, rate, "max_qty_per_hr"), true);
        }
        // Repeated effective price rows may carry the same constant capacity.
        if (rows.size() > 1) {
            std::optional<int> constant;
            bool supported = true, effective = false;
            for (const auto *row : rows) {
                const auto at = get(rates, *row, "start_date"), value = get(rates, *row, "max_qty_per_hr");
                if (!at.isEmpty())
                    stamp(at, path(rates, *row, "start_date"));
                if (value.isEmpty()) {
                    supported = false;
                    continue;
                }
                const auto capacity = units(value, path(rates, *row, "max_qty_per_hr"), true);
                if (constant && *constant != capacity)
                    supported = false;
                constant = capacity;
                effective |= at.isEmpty() || at <= qs(p.startDate) + " 09:00";
            }
            if (supported && effective)
                resource.maxUnits = constant;
        }
        if (!resource.maxUnits)
            diagnostic(diagnostics, path(resources, r, "rsrc_id"),
                       "가용량 미정 · 단일 유효 기본 가용량 이외 곡선을 변환하지 않음");
        resourceIds[rid] = resource.id;
        map("resource", rid, resource.id);
        p.resources.push_back(std::move(resource));
    }
    for (const auto &[rid, rows] : rateRows) {
        (void)rows;
        if (!resourceIds.contains(rid))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_071, {});
    }
    const auto &assignments = table(file, "TASKRSRC");
    unique(assignments, "taskrsrc_id");
    const auto &curves = table(file, "RSRCCURVDATA");
    unique(curves, "curv_id");
    struct PendingCurve {
        std::string task, resource;
        NativeCurve curve;
        std::optional<std::int64_t> work;
    };
    std::vector<PendingCurve> pendingCurves;
    for (const auto &r : assignments.rows) {
        projectRef(assignments, r, projectId);
        auto tid = id(assignments, r, "task_id"), rid = id(assignments, r, "rsrc_id");
        if (!taskIds.contains(tid) || !resourceIds.contains(rid))
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_072, {});
        auto curve = get(assignments, r, "curv_id");
        if (!curve.isEmpty() && curve != "0") {
            const auto curveId = QString::number(integer(curve, path(assignments, r, "curv_id")));
            const Row *definition = nullptr;
            for (const auto &v : curves.rows)
                if (id(curves, v, "curv_id") == curveId)
                    definition = &v;
            if (!definition)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_073);
            for (const auto &field : {"target_lag_drtn_hr_cnt", "remain_lag_drtn_hr_cnt"}) {
                const auto lag = get(assignments, r, field);
                if (!lag.isEmpty() && curveDecimal(lag, 6000, maxWork) != 0)
                    throw AppError(
                        Diagnostic{"RESOURCE_PLANNING_INVALID",
                                   {"곡선이 있는 지연 배정은 현재 지원하지 않습니다. 원본 XER을 확인하세요."},
                                   {},
                                   "RESOURCE_PLANNING_INVALID"});
            }
            NativeCurve c;
            c.source = "p6-xer";
            c.externalId = str(curveId);
            c.name = str(get(curves, *definition, "curv_name"));
            if (c.name.empty())
                c.name = "P6 curve " + c.externalId;
            for (int i = 0; i <= 20; ++i)
                c.weights.push_back(
                    int(curveDecimal(get(curves, *definition, "pct_usage_" + QString::number(i)), 1000000, 100000000)));
            auto quantity = get(assignments, r, "target_qty");
            pendingCurves.push_back({taskIds.at(tid), resourceIds.at(rid), std::move(c),
                                     quantity.isEmpty()
                                         ? std::nullopt
                                         : std::optional<std::int64_t>{curveDecimal(quantity, 6000, maxWork)}});
        }
        p.assignments.push_back(
            {taskIds.at(tid), resourceIds.at(rid),
             units(get(assignments, r, "target_qty_per_hr"), path(assignments, r, "target_qty_per_hr"), false)});
        map("assignment", id(assignments, r, "taskrsrc_id"), taskIds.at(tid) + "/" + resourceIds.at(rid));
    }
    const auto &types = table(file, "ACTVTYPE"), &codes = table(file, "ACTVCODE"), &taskCodes = table(file, "TASKACTV");
    std::map<QString, std::size_t> typeIds;
    std::map<QString, QString> codeOwners;
    for (const auto &r : types.rows) {
        calculationCheckpoint();
        const auto owner = get(types, r, "proj_id");
        if (!owner.isEmpty() && id(types, r, "proj_id") != "0" && id(types, r, "proj_id") != projectId)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_026, {"ACTVTYPE/proj_id"});
        auto tid = id(types, r, "actv_code_type_id");
        typeIds.emplace(tid, p.codeTypes.size());
        p.codeTypes.push_back({str("xer-code-type-" + tid), str(get(types, r, "actv_code_type")), {}});
        map("code-type", tid, p.codeTypes.back().id);
    }
    for (const auto &r : codes.rows) {
        calculationCheckpoint();
        const auto tid = id(codes, r, "actv_code_type_id"), cid = id(codes, r, "actv_code_id");
        if (!typeIds.contains(tid))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"ACTVCODE/actv_code_type_id"});
        codeOwners.emplace(cid, tid);
        auto parent = get(codes, r, "parent_actv_code_id");
        p.codeTypes[typeIds.at(tid)].values.push_back(
            {str("xer-code-" + cid), str(get(codes, r, "short_name")),
             parent.isEmpty() ? std::string{} : str("xer-code-" + id(codes, r, "parent_actv_code_id"))});
        map("code-value", cid, str("xer-code-" + cid));
    }
    std::map<QString, Task *> codeTasks;
    std::map<std::string, Task *> nativeTasks;
    for (auto &task : p.tasks)
        nativeTasks.emplace(task.id, &task);
    for (const auto &[tid, native] : taskIds)
        codeTasks.emplace(tid, nativeTasks.at(native));
    for (const auto &r : taskCodes.rows) {
        calculationCheckpoint();
        projectRef(taskCodes, r, projectId);
        const auto tid = id(taskCodes, r, "task_id"), type = id(taskCodes, r, "actv_code_type_id"),
                   value = id(taskCodes, r, "actv_code_id");
        if (!codeTasks.contains(tid) || !typeIds.contains(type) || !codeOwners.contains(value) ||
            codeOwners.at(value) != type)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"TASKACTV 참조"});
        codeTasks.at(tid)->attributes.push_back({"code:" + p.codeTypes[typeIds.at(type)].id, str("xer-code-" + value)});
    }
    if (!types.rows.empty())
        diagnostic(
            diagnostics, "ACTVTYPE/scope",
            "코드 사전은 프로젝트별 native 사전으로 복사합니다. 전역/EPS 공유 범위·설명·색상은 원본에 보존합니다.");
    std::map<QString, ExternalField> udf;
    std::set<QString> integerUdf;
    const auto &typesUdf = table(file, "UDFTYPE"), &valuesUdf = table(file, "UDFVALUE");
    for (const auto &r : typesUdf.rows) {
        const auto idValue = id(typesUdf, r, "udf_type_id"), owner = get(typesUdf, r, "table_name");
        const auto type = get(typesUdf, r, "logical_data_type");
        if (type == "FT_INTEGER")
            integerUdf.insert(idValue);
        if ((owner != "TASK" && owner != "RSRC" && owner != "PROJECT" && owner != "TASKRSRC") ||
            (type != "FT_TEXT" && type != "FT_FLOAT" && type != "FT_INTEGER" && type != "FT_START_DATE" &&
             type != "FT_END_DATE")) {
            diagnostic(diagnostics, "UDFTYPE/" + idValue, "미지원 UDF 형식/범위 · 원본에 보존");
            continue;
        }
        auto scope = owner == "TASK"       ? FieldScope::Task
                     : owner == "RSRC"     ? FieldScope::Resource
                     : owner == "TASKRSRC" ? FieldScope::Assignment
                                           : FieldScope::Project;
        auto nativeType = type == "FT_TEXT"       ? CustomFieldType::Text
                          : type.endsWith("DATE") ? CustomFieldType::Date
                                                  : CustomFieldType::Number;
        auto name = get(typesUdf, r, "udf_type_label");
        if (name.isEmpty())
            name = get(typesUdf, r, "udf_type_name");
        udf.emplace(idValue, importUdfField(p, false, scope, str(idValue), str(name), nativeType, mapping));
    }
    std::map<std::string, std::size_t> udfResourceIndex;
    for (std::size_t i = 0; i < p.resources.size(); ++i)
        udfResourceIndex[p.resources[i].id] = i;
    std::map<QString, std::size_t> udfAssignmentIndex;
    for (std::size_t i = 0; i < assignments.rows.size(); ++i)
        udfAssignmentIndex[id(assignments, assignments.rows[i], "taskrsrc_id")] = i;
    for (const auto &r : valuesUdf.rows) {
        calculationCheckpoint();
        const auto idValue = id(valuesUdf, r, "udf_type_id");
        if (!udf.contains(idValue))
            continue;
        const auto &f = udf.at(idValue);
        auto fk = id(valuesUdf, r, "fk_id");
        std::vector<TaskAttribute> *values = nullptr;
        if (f.scope == FieldScope::Task) {
            if (!taskIds.contains(fk))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 작업 참조"});
            values = &p.tasks[taskIndex.at(taskIds.at(fk))].attributes;
        } else if (f.scope == FieldScope::Assignment) {
            if (!udfAssignmentIndex.contains(fk))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 배정 참조"});
            values = &p.assignments.at(udfAssignmentIndex.at(fk)).attributes;
        } else if (f.scope == FieldScope::Project) {
            if (fk != projectId)
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 프로젝트 참조"});
            values = &p.projectAttributes;
        } else {
            if (!resourceIds.contains(fk))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 자원 참조"});
            values = &p.resources[udfResourceIndex.at(resourceIds.at(fk))].attributes;
        }
        const QString column = f.type == CustomFieldType::Text     ? "udf_text"
                               : f.type == CustomFieldType::Number ? "udf_number"
                                                                   : "udf_date";
        for (const auto *other : {"udf_text", "udf_number", "udf_date", "udf_code_id"})
            if (column != other && !get(valuesUdf, r, other).isEmpty())
                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                               {"UDFVALUE 형식과 다른 값 열이 함께 설정되었습니다."});
        auto value = get(valuesUdf, r, column);
        if (integerUdf.contains(idValue) && parseFieldNumber(str(value)) % fieldNumberScale != 0)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"FT_INTEGER UDF에 소수 값이 있습니다."});
        if (f.type == CustomFieldType::Date) {
            if (!QRegularExpression("^[0-9]{4}-[0-9]{2}-[0-9]{2} 00:00$").match(value).hasMatch())
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"날짜 UDF는 자정의 civil date만 지원합니다."});
            value = value.left(10);
        }
        importUdfValue(p, f, *values, str(value));
    }
    for (auto &curve : pendingCurves) {
        calculationCheckpoint();
        p = applyNativeCurve(p, curve.task, curve.resource, std::move(curve.curve), curve.work);
        diagnostic(diagnostics, "TASKRSRC/" + qs(curve.task) + "/curve",
                   "P6 21지점 원본 정의와 정수 계획 배정 투영 보존 · 원본 날짜/외부 재계산 동등성은 별도 검토");
    }
    auto actualCosts = importXerActualCosts(p, file.tables, diagnostics);
    validate(p); // Preserve native input diagnostics before building optional source evidence.
    captureXerFinancials(p, file.tables, QFileInfo(sourceName).fileName(), hash(bytes), projectId);
    actualCosts["financialSnapshotStored"] = p.xerFinancials.has_value();
    for (const auto &[name, t] : file.tables) {
        if (t.rows.empty())
            continue;
        if (t.used.empty()) {
            diagnostic(diagnostics, name, "미지원 테이블 · " + QString::number(t.rows.size()) + "행 원본에 보존");
            continue;
        }
        for (const auto &field : t.fields)
            if (!t.used.contains(field))
                diagnostic(diagnostics, name + "/" + field, "미지원 필드 · 원본에 보존");
    }
    graph(p);
    auto report = reportFor(p, true);
    report["actualCosts"] = actualCosts;
    report["source"] = QJsonObject{{"name", QFileInfo(sourceName).fileName()},
                                   {"sha256", hash(bytes)},
                                   {"encoding", encodingName(encoding)},
                                   {"header", QJsonArray::fromStringList(file.header)},
                                   {"version", file.header[1]},
                                   {"producer", "unverified"},
                                   {"projectId", projectId},
                                   {"projectCode", code},
                                   {"lagPolicy", lagPolicy}};
    if (!selection.isEmpty()) {
        report["projectSelection"] = selection;
        diagnostic(diagnostics, "PROJECT/selection",
                   "선택 프로젝트 ID " + projectId + " · 제외 프로젝트 " + QString::number(inventory.size() - 1) +
                       "개 · original.xer에는 모든 프로젝트의 원본 자료가 포함됩니다.");
    }
    report["mapping"] = mapping;
    report["diagnostics"] = diagnostics;
    return {std::move(p), bytes, report, true};
}

namespace {
QString timeText(int minutes) {
    return QString("%1:%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0'));
}
QString record(const QString &name, const QString &attrs, const QStringList &children = {}) {
    return "(0||" + name + "(" + attrs + ")(" + children.join("") + "))";
}
QString calendarData(const TaskCalendar &c) {
    QStringList days, exceptions;
    for (int day = 1; day <= 7; ++day) {
        int native = day == 1 ? 7 : day - 1;
        QStringList periods;
        if (std::find(c.weekdays.begin(), c.weekdays.end(), native) != c.weekdays.end()) {
            int i = 0;
            for (const auto &p : c.periods)
                periods << record(QString::number(i++), "s|" + timeText(p.begin) + "|f|" + timeText(p.end));
        }
        days << record(QString::number(day), {}, periods);
    }
    int i = 0;
    for (const auto &holiday : c.holidays)
        exceptions << record(
            QString::number(i++),
            "d|" + QString::number(QDate(1899, 12, 30).daysTo(QDate::fromString(qs(holiday), Qt::ISODate))));
    return record("CalendarData", {}, {record("DaysOfWeek", {}, days), record("Exceptions", {}, exceptions)});
}
QString unitText(int units) {
    return QString::number(units / 100) + "." + QString::number(units % 100).rightJustified(2, '0');
}
QString hourText(std::int64_t minutes) {
    const auto sign = minutes < 0 ? "-" : "";
    auto n = std::abs(minutes);
    auto micros = (n * 1000000 + 30) / 60;
    return sign + QString::number(micros / 1000000) + "." + QString::number(micros % 1000000).rightJustified(6, '0');
}
} // namespace
PrimaveraXerExchange exportPrimaveraXer(const Project &p) {
    EvaluationScope evaluation;
    validate(p);
    ScheduleFormulaScope formulaSchedule(p, nullptr, ScheduleFormulaMode::Plan);
    graph(p);
    if (p.tasks.size() > 10000 || p.wbsNodes.size() >= 10000 || p.resources.size() > 10000 ||
        p.assignments.size() > 100000 || p.calendars.size() >= 10000)
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_074, {});
    QByteArray bytes("\xef\xbb\xbf");
    auto line = [&](const QStringList &cells) {
        for (const auto &cell : cells) {
            if (cell.size() > 1024 * 1024)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_075, {});
            for (auto c : cell)
                if (forbiddenTextCharacter(c))
                    throw AppError(ErrorCode::FILE_PRIMAVERAXER_076, {});
        }
        bytes += cells.join('\t').toUtf8() + "\r\n";
        if (bytes.size() > byteLimit)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_077, {});
    };
    auto header = [&](const QString &name, const QStringList &fields) {
        line({"%T", name});
        QStringList values{"%F"};
        values.append(fields);
        line(values);
    };
    auto row = [&](const QStringList &values) {
        QStringList cells{"%R"};
        cells.append(values);
        line(cells);
    };
    QJsonArray diagnostics, mapping;
    std::set<std::string> curveTasks;
    for (const auto &a : p.assignments)
        if (!a.periods.empty() || a.nativeCurve || a.productivityEstimate)
            curveTasks.insert(a.taskId);
    std::map<std::pair<std::string, std::string>, CurveExport> curveExports;
    std::map<std::string, std::int64_t> curveDurations;
    for (const auto &a : p.assignments)
        if (curveTasks.contains(a.taskId)) {
            auto curve = exportResourceCurve(p, a);
            if (curve.definition.durationMinutes < 20) {
                if (curve.definition.durationMinutes == 0)
                    curveDurations[a.taskId] = 0;
                diagnostic(diagnostics, "TASKRSRC/" + qs(a.taskId) + "/curve",
                           "20분 미만 곡선은 XER 21지점으로 투영하지 않음 · 전체 구간은 source.ppm에 보존");
                continue;
            }
            curveDurations[a.taskId] = curve.definition.durationMinutes;
            if (!curve.exact)
                diagnostic(diagnostics, "TASKRSRC/" + qs(a.taskId) + "/" + qs(a.resourceId),
                           "현재 계획 공수를 20개 구간/21지점 비중으로 집계 · 세부 배정률 차이는 source.ppm에 보존");
            curveExports.emplace(std::pair{a.taskId, a.resourceId}, std::move(curve));
        }
    if (hasXmlFormulas(p))
        diagnostic(diagnostics, "PROJECT/xml-formulas",
                   "XML 계산식은 현재 결과를 정적 값으로 투영하고 원본 식·바인딩·실행 설정은 source.ppm에 보존합니다.");
    if (hasXmlRollUp(p))
        diagnostic(diagnostics, "PROJECT/xml-roll-up",
                   "XML 요약 roll-up 규칙과 WBS/루트 명시 요약 값은 XER에서 실행하지 않으며 source.ppm에 보존합니다.");
    if (hasXmlCodeMasks(p))
        diagnostic(diagnostics, "PROJECT/xml-code-masks",
                   "XML 코드 마스크·자동 생성 설정·WBS 코드는 XER에서 실행하지 않습니다. 필드 표시 값은 정적 문자열로 "
                   "투영하며 원래 정의·코드는 source.ppm에 보존합니다.");
    if (hasResourcePlan(p))
        diagnostic(diagnostics, "PROJECT/resourcePlan",
                   "자재·비용계정·역할은 source.ppm에만 보존 · 연결된 역할의 구체 자원 배정은 기간 기반 배정으로 투영");
    auto map = [&](const QString &kind, int id, const std::string &target) {
        mapping.append(QJsonObject{{"kind", kind},
                                   {"xerId", QString::number(id)},
                                   {"ppmId", qs(target)},
                                   {"activityCode", kind == "task" ? "J" + QString::number(id) : QString{}}});
    };
    if (!p.activities.empty() || !p.stepReports.empty())
        diagnostic(diagnostics, "PROJECT/activities",
                   "Steps·LOE·WBS Summary 정의/보고는 XER에 매핑하지 않습니다. source.ppm에 보존합니다.");
    if (p.printPreferences || !p.networkPositions.empty())
        diagnostic(diagnostics, "PROJECT/presentation",
                   "인쇄 설정·수동 네트워크 배치는 교환하지 않습니다. source.ppm에 보존합니다.");
    if (!p.remainingWork.empty())
        diagnostic(diagnostics, "TASKRSRC/remaining_dates",
                   "날짜별 잔여 공수는 교환하지 않습니다. source.ppm에 보존합니다.");
    if (hasAssignmentPeriods(p))
        diagnostic(
            diagnostics, "TASKRSRC/contour",
            "기간별 계획 배정은 P6 21지점 곡선으로 투영합니다. 세부 구간·현재/동결 원본은 source.ppm에 보존합니다.");
    if (hasClassification(p) || !p.savedTaskViews.empty() || !p.savedResourceViews.empty())
        diagnostic(diagnostics, "TASK/UDF/views",
                   "사용자 필드는 UDF 매핑표를 따릅니다. 저장된 보기는 source.ppm에 전체 "
                   "데이터를 보존합니다.");
    if (p.progressPolicy)
        diagnostic(diagnostics, "PROJECT/progress_policy",
                   "채택 진척 정책은 교환하지 않습니다. native source.ppm에 설정과 근거를 보존하며 실제 날짜·잔여 "
                   "기간의 외부 재계산 정책은 미검증입니다.");
    if (p.planLeveling || p.remainingLeveling)
        diagnostic(diagnostics, "TASK/leveling",
                   "평준화 채택 정책/날짜는 XER에 매핑하지 않습니다. source.ppm에서 보존하며 XER은 논리 계획입니다.");
    if (!p.levelingDecisions.empty())
        diagnostic(diagnostics, "PROJECT/leveling_decisions",
                   "평준화 결정 이력은 XER에 매핑하지 않습니다. source.ppm에 원본 이력을 보존합니다.");
    const auto projectCode = "PPM-" + hash(qs(p.documentId).toUtf8()).left(12);
    line({"ERMHDR", "20.12", qs(p.startDate), "Project", "PPM", "PPM", "dbxDatabaseNoName", "Project Management",
          qs(p.currency)});
    diagnostic(diagnostics, "PROJECT",
               "기간 기반 계획과 상태일의 명시 실적·원가 지원 부분을 투영합니다. P6 계산/재저장 호환은 미검증. 전체 "
               "현재 입력/승인·원장은 source.ppm 참조.");
    header("CURRTYPE",
           {"curr_id", "decimal_digit_cnt", "curr_symbol", "decimal_symbol", "digit_group_symbol", "pos_curr_fmt_type",
            "neg_curr_fmt_type", "curr_type", "curr_short_name", "group_digit_cnt", "base_exch_rate"});
    row({"1", QString::number(currencyDecimals(p.currency)), qs(p.currency), ".", ",", "#1.1", "(#1.1)", qs(p.currency),
         qs(p.currency), "3", "1"});
    header("PROJECT", {"proj_id", "proj_short_name", "clndr_id", "plan_start_date", "def_task_type",
                       "def_duration_type", "task_code_prefix", "task_code_base", "task_code_step"});
    row({"1", projectCode, "1", qs(p.startDate) + " 09:00", "TT_Task", "DT_FixedDrtn", "J", "1", "1"});
    header("SCHEDOPTIONS", {"schedoptions_id", "proj_id", "sched_calendar_on_relationship_lag"});
    row({"1", "1", "rcal_Successor"});
    header("CALENDAR", {"clndr_id", "default_flag", "clndr_name", "proj_id", "base_clndr_id", "clndr_type",
                        "day_hr_cnt", "week_hr_cnt", "rsrc_private", "clndr_data"});
    std::map<std::string, int> calendars{{"", 1}};
    TaskCalendar standard;
    standard.name = "PPM standard";
    standard.holidays = p.holidays;
    std::vector<TaskCalendar> allCalendars{standard};
    allCalendars.insert(allCalendars.end(), p.calendars.begin(), p.calendars.end());
    int next = 1;
    for (const auto &c : allCalendars) {
        if (c.holidays.size() > 10000)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_078, {});
        int minutes = 0;
        for (const auto &period : c.periods)
            minutes += period.end - period.begin;
        int uid = next++;
        calendars[c.id] = uid;
        map("calendar", uid, c.id);
        row({QString::number(uid),
             uid == 1 ? "Y" : "N",
             qs(c.name),
             "1",
             {},
             "CA_Project",
             hourText(minutes),
             hourText(minutes * int(c.weekdays.size())),
             "N",
             calendarData(c)});
    }
    header("PROJWBS",
           {"wbs_id", "proj_id", "seq_num", "proj_node_flag", "wbs_short_name", "wbs_name", "parent_wbs_id"});
    row({"1", "1", "0", "Y", projectCode, qs(p.name), {}});
    map("project-root", 1, "");
    std::map<std::string, int> wbs{{"", 1}};
    next = 2;
    for (const auto &g : p.wbsNodes) {
        wbs[g.id] = next;
        map("wbs", next++, g.id);
    }
    for (const auto &g : p.wbsNodes)
        row({QString::number(wbs.at(g.id)), "1", QString::number(wbs.at(g.id)), "N",
             "W" + QString::number(wbs.at(g.id)), qs(g.name), QString::number(wbs.at(g.parentId))});
    header("TASK", {"task_id", "proj_id", "wbs_id", "clndr_id", "task_code", "task_name", "task_type", "duration_type",
                    "status_code", "complete_pct_type", "target_drtn_hr_cnt", "remain_drtn_hr_cnt", "cstr_type",
                    "cstr_date", "cstr_type2", "cstr_date2"});
    std::map<std::string, int> tasks;
    next = 1;
    const std::map<ConstraintType, QString> reverse{
        {ConstraintType::ASAP, ""},          {ConstraintType::ALAP, "CS_ALAP"}, {ConstraintType::MSO, "CS_MANDSTART"},
        {ConstraintType::MFO, "CS_MANDFIN"}, {ConstraintType::SNET, "CS_MSOA"}, {ConstraintType::SNLT, "CS_MSOB"},
        {ConstraintType::FNET, "CS_MEOA"},   {ConstraintType::FNLT, "CS_MEOB"}};
    for (const auto &t : p.tasks) {
        int uid = next++;
        tasks[t.id] = uid;
        map("task", uid, t.id);
        auto duration = curveDurations.contains(t.id) ? hourText(curveDurations.at(t.id))
                        : t.durationMinutes           ? hourText(*t.durationMinutes)
                                                      : QString{};
        row({QString::number(uid),
             "1",
             QString::number(wbs.at(t.wbsId)),
             QString::number(calendars.at(t.calendarId)),
             "J" + QString::number(uid),
             qs(t.name),
             (curveDurations.contains(t.id) ? curveDurations.at(t.id) == 0 : t.durationMinutes == 0) ? "TT_Mile"
                                                                                                     : "TT_Task",
             "DT_FixedDrtn",
             "TK_NotStart",
             "CP_Drtn",
             duration,
             duration,
             reverse.at(t.constraintType),
             t.constraintAt ? qs(*t.constraintAt) : QString{},
             {},
             {}});
        if (!t.durationMinutes)
            diagnostic(diagnostics, "TASK/" + qs(t.id) + "/target_drtn_hr_cnt",
                       "미정 기간 공백 · 외부 앱 기본값 해석 가능");
        if (t.deadlineAt)
            diagnostic(diagnostics, "TASK/" + qs(t.id) + "/deadline",
                       "소프트 마감일을 expected finish/하드 제약으로 변환하지 않음");
        if (t.taskType != TaskType::FixedDuration || t.workUnitMinutes || !t.breaks.empty())
            diagnostic(diagnostics, "TASK/" + qs(t.id), "공수/작업 유형/분할 규칙 대신 명목 기간을 투영");
    }
    std::map<std::string, int> codeTypes;
    std::map<std::pair<std::string, std::string>, int> codeValues;
    header("ACTVTYPE", {"actv_code_type_id", "actv_code_type", "seq_num", "actv_code_type_scope", "proj_id"});
    next = 1;
    for (const auto &type : p.codeTypes) {
        codeTypes[type.id] = next;
        map("code-type", next, type.id);
        row({QString::number(next), qs(type.name), QString::number(next), "AS_Project", "1"});
        ++next;
    }
    next = 1;
    for (const auto &type : p.codeTypes)
        for (const auto &value : type.values)
            codeValues[{type.id, value.id}] = next++;
    header("ACTVCODE",
           {"actv_code_id", "parent_actv_code_id", "actv_code_type_id", "short_name", "actv_code_name", "seq_num"});
    for (const auto &type : p.codeTypes)
        for (const auto &value : type.values) {
            const auto uid = codeValues.at({type.id, value.id});
            map("code-value", uid, type.id + "/" + value.id);
            row({QString::number(uid),
                 value.parentId.empty() ? QString{} : QString::number(codeValues.at({type.id, value.parentId})),
                 QString::number(codeTypes.at(type.id)), qs(value.name), qs(value.name), QString::number(uid)});
        }
    header("TASKACTV", {"task_id", "actv_code_id", "proj_id", "actv_code_type_id"});
    for (const auto &task : p.tasks)
        for (const auto &attribute : task.attributes)
            if (attribute.key.starts_with("code:")) {
                const auto type = attribute.key.substr(5);
                row({QString::number(tasks.at(task.id)), QString::number(codeValues.at({type, attribute.value})), "1",
                     QString::number(codeTypes.at(type))});
            }
    header("TASKPRED",
           {"task_pred_id", "task_id", "pred_task_id", "proj_id", "pred_proj_id", "pred_type", "lag_hr_cnt"});
    next = 1;
    for (const auto &t : p.tasks)
        for (const auto &d : dependencies(t)) {
            if (next > 100000)
                throw AppError(ErrorCode::FILE_PRIMAVERAXER_079, {});
            row({QString::number(next++), QString::number(tasks.at(t.id)), QString::number(tasks.at(d.predecessorId)),
                 "1", "1", "PR_" + qs(enumToken(d.type)), hourText(d.lagMinutes)});
        }
    header("RSRC", {"rsrc_id", "clndr_id", "curr_id", "rsrc_name", "rsrc_short_name", "rsrc_type", "active_flag"});
    std::map<std::string, int> resources;
    next = 1;
    for (const auto &r : p.resources) {
        int uid = next++;
        resources[r.id] = uid;
        map("resource", uid, r.id);
        row({QString::number(uid), QString::number(calendars.at(r.calendarId)), "1", qs(r.name),
             "R" + QString::number(uid), r.type == "equipment" ? "RT_Equip" : "RT_Labor", "Y"});
        if (!r.exceptions.empty() || hasResourceAvailability(r))
            diagnostic(diagnostics, "RSRC/" + qs(r.id), "날짜별 가용량 예외는 source.ppm에만 보존");
        if (!r.maxUnits)
            diagnostic(diagnostics, "RSRC/" + qs(r.id) + "/capacity", "가용량 미정 · 외부 앱 기본값 해석 가능");
        if (r.productivity)
            diagnostic(diagnostics, "RSRC/" + qs(r.id) + "/productivity",
                       "물량/자원시간 생산성 정의는 source.ppm에 보존 · 단가/가용량으로 변환하지 않음");
    }
    header("RSRCRATE", {"rsrc_rate_id", "rsrc_id", "start_date", "max_qty_per_hr"});
    next = 1;
    for (const auto &r : p.resources)
        if (r.maxUnits)
            row({QString::number(next++), QString::number(resources.at(r.id)), qs(p.startDate) + " 00:00",
                 unitText(*r.maxUnits)});
    QStringList curveFields{"curv_id", "curv_name", "default_flag"};
    for (int i = 0; i <= 20; ++i)
        curveFields.append("pct_usage_" + QString::number(i));
    header("RSRCCURVDATA", curveFields);
    std::map<std::pair<std::string, std::string>, int> curveIds;
    int curveId = 1;
    for (const auto &[key, projection] : curveExports) {
        const auto &c = projection.definition;
        curveIds[key] = curveId;
        map("curve", curveId++, key.first + "/" + key.second);
        QStringList cells{QString::number(curveIds[key]), qs(c.name), "N"};
        for (int weight : c.weights)
            cells.append(QString::number(weight / 1000000) + "." +
                         QString::number(weight % 1000000).rightJustified(6, '0'));
        row(cells);
    }
    header("TASKRSRC", {"taskrsrc_id", "task_id", "proj_id", "rsrc_id", "target_qty_per_hr", "remain_qty_per_hr",
                        "target_qty", "remain_qty", "curv_id"});
    next = 1;
    std::map<std::string, int> assignmentIds;
    for (const auto &a : p.assignments) {
        assignmentIds[assignmentAttributeId(a)] = next;
        map("assignment", next, a.taskId + "/" + a.resourceId);
        const auto &task = p.tasks[std::size_t(tasks.at(a.taskId) - 1)];
        const auto key = std::pair{a.taskId, a.resourceId};
        auto quantity = curveDurations.contains(a.taskId) && curveDurations.at(a.taskId) == 0 ? QString("0")
                        : curveExports.contains(key) ? curveHourText(curveExports.at(key).definition.workUnitMinutes)
                        : task.durationMinutes
                            ? QString::number(double(*task.durationMinutes) * a.units / 6000.0, 'f', 6)
                            : QString{};
        row({QString::number(next++), QString::number(tasks.at(a.taskId)), "1",
             QString::number(resources.at(a.resourceId)), unitText(a.units), unitText(a.units), quantity, quantity,
             curveIds.contains(key) ? QString::number(curveIds.at(key)) : QString{}});
        if (a.productivityEstimate)
            diagnostic(diagnostics, "TASKRSRC/" + qs(a.taskId) + "/estimate",
                       "물량·생산성 산정 근거는 source.ppm에 보존 · 환산된 계획 공수/곡선만 교환");
    }
    const RollDownValues inherited(p);
    if (hasXmlRollDown(p))
        diagnostic(diagnostics, "PROJECT/roll-down",
                   "배정 roll-down의 현재 유효 값은 정적 UDF로 투영합니다. 자동 연결·직접 값/미설정 구분은 "
                   "source.ppm에 보존합니다.");
    const auto udf = exportUdfFields(p, false, diagnostics, mapping);
    header("UDFTYPE", {"udf_type_id", "table_name", "udf_type_name", "udf_type_label", "logical_data_type"});
    for (const auto &f : udf)
        row({qs(f.externalId),
             f.scope == FieldScope::Task         ? "TASK"
             : f.scope == FieldScope::Resource   ? "RSRC"
             : f.scope == FieldScope::Assignment ? "TASKRSRC"
                                                 : "PROJECT",
             "user_field_" + qs(f.externalId), qs(f.name),
             f.type == CustomFieldType::Text   ? "FT_TEXT"
             : f.type == CustomFieldType::Date ? "FT_START_DATE"
                                               : "FT_FLOAT"});
    header("UDFVALUE", {"udf_type_id", "fk_id", "proj_id", "udf_date", "udf_number", "udf_text", "udf_code_id"});
    std::size_t udfCells = 0;
    auto udfRow = [&](const ExternalField &f, const QString &fk, const Task *task, const Resource *resource,
                      const Assignment *assignment = nullptr) {
        calculationCheckpoint();
        if (++udfCells > 100000)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 조회 셀 한도 100,000개 초과"});
        auto v = exportUdfValue(p, f, task, resource, diagnostics, assignment, &inherited);
        if (!v)
            return;
        if (f.type == CustomFieldType::Text && v->empty())
            diagnostic(
                diagnostics, "UDFVALUE/" + qs(f.externalId) + "/" + fk,
                "빈 텍스트를 기록합니다. 외부 제품은 미설정으로 해석할 수 있습니다. source.ppm에 구분을 보존합니다.");
        row({qs(f.externalId), fk, "1", f.type == CustomFieldType::Date ? qs(*v) + " 00:00" : "",
             f.type == CustomFieldType::Number ? qs(*v) : "", f.type == CustomFieldType::Text ? qs(*v) : "", ""});
    };
    for (const auto &f : udf) {
        if (f.scope == FieldScope::Task)
            for (const auto &t : p.tasks)
                udfRow(f, QString::number(tasks.at(t.id)), &t, nullptr);
        else if (f.scope == FieldScope::Resource)
            for (const auto &r : p.resources)
                udfRow(f, QString::number(resources.at(r.id)), nullptr, &r);
        else if (f.scope == FieldScope::Assignment)
            for (const auto &a : p.assignments)
                udfRow(f, QString::number(assignmentIds.at(assignmentAttributeId(a))), nullptr, nullptr, &a);
        else
            udfRow(f, "1", nullptr, nullptr);
    }
    line({"%E"});
    auto detailed = parse(decode(bytes, XerEncoding::Utf8));
    const auto actualCosts = exportXerActualCosts(p, detailed.tables, diagnostics);
    bytes = serializeXer(detailed);
    importPrimaveraXer(bytes, "project.xer");
    if (!p.baselines.empty() || !p.progress.empty() || !p.costEntries.empty() || !p.scenarios.empty() || p.statusAt ||
        !p.taskCosts.empty())
        diagnostic(
            diagnostics, "PROJECT/history-costs",
            "상태일의 실적·원가 지원 부분을 투영합니다. 기준선·원장 정정 이력·시나리오는 source.ppm에 보존합니다.");
    diagnostic(diagnostics, "PROJECT/IDs",
               "새 XER ID/Activity Code를 생성합니다. P6의 기존 프로젝트/활동 업데이트 동일성은 보장하지 않습니다.");
    auto report = reportFor(p, false);
    report["actualCosts"] = actualCosts;
    report["source"] = QJsonObject{{"name", "source.ppm"}, {"product", "PPM"}};
    report["outputProjectCode"] = projectCode;
    report["outputEncoding"] = "UTF-8 BOM";
    report["outputVersion"] = "20.12";
    report["xerSha256"] = hash(bytes);
    report["mapping"] = mapping;
    report["diagnostics"] = diagnostics;
    return {p, bytes, report, false};
}
QByteArray combinePrimaveraXer(std::vector<PrimaveraXerExchange> &exchanges) {
    if (exchanges.empty() || exchanges.size() > 100)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    const std::map<QString, QString> primary{{"PROJECT", "proj_id"},       {"SCHEDOPTIONS", "schedoptions_id"},
                                             {"CALENDAR", "clndr_id"},     {"PROJWBS", "wbs_id"},
                                             {"TASK", "task_id"},          {"TASKPRED", "task_pred_id"},
                                             {"RSRC", "rsrc_id"},          {"RSRCRATE", "rsrc_rate_id"},
                                             {"TASKRSRC", "taskrsrc_id"},  {"ACTVTYPE", "actv_code_type_id"},
                                             {"ACTVCODE", "actv_code_id"}, {"UDFTYPE", "udf_type_id"},
                                             {"PROJCOST", "cost_item_id"}, {"FINDATES", "fin_dates_id"},
                                             {"RSRCCURVDATA", "curv_id"}};
    const std::map<QString, QString> aliases{{"pred_proj_id", "proj_id"},
                                             {"pred_task_id", "task_id"},
                                             {"parent_wbs_id", "wbs_id"},
                                             {"base_clndr_id", "clndr_id"},
                                             {"parent_actv_code_id", "actv_code_id"}};
    const std::map<QString, QString> kinds{{"calendar", "clndr_id"},
                                           {"project-root", "wbs_id"},
                                           {"wbs", "wbs_id"},
                                           {"task", "task_id"},
                                           {"resource", "rsrc_id"},
                                           {"assignment", "taskrsrc_id"},
                                           {"code-type", "actv_code_type_id"},
                                           {"code-value", "actv_code_id"},
                                           {"curve", "curv_id"}};
    File combined;
    std::map<QString, int> next;
    for (std::size_t i = 0; i < exchanges.size(); ++i) {
        calculationCheckpoint();
        auto &exchange = exchanges[i];
        if (exchange.importing)
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"내보내기 자료만 결합할 수 있습니다."});
        auto file = parse(decode(exchange.xer, XerEncoding::Utf8));
        if (i == 0)
            combined.header = file.header;
        if (file.header[8] != combined.header[8])
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"일괄 XER 내보내기는 같은 통화의 프로젝트를 선택하세요."});
        std::map<QString, std::map<QString, QString>> ids;
        std::map<QString, QString> udfOwner;
        for (const auto &r : file.tables.at("UDFTYPE").rows)
            udfOwner[id(file.tables.at("UDFTYPE"), r, "udf_type_id")] = get(file.tables.at("UDFTYPE"), r, "table_name");
        for (const auto &[name, key] : primary)
            for (const auto &r : file.tables.at(name).rows) {
                auto &counter = next[key];
                ids[key].emplace(id(file.tables.at(name), r, key), QString::number(++counter));
            }
        for (const auto &[name, source] : file.tables) {
            if (source.fields.empty() || (name == "CURRTYPE" && i != 0))
                continue;
            auto &target = combined.tables[name];
            if (target.fields.empty()) {
                target.name = name;
                target.fields = source.fields;
            }
            if (target.fields != source.fields)
                throw AppError(ErrorCode::WORKSPACE_INVALID, {"XER 테이블 필드가 일치하지 않습니다."});
            for (const auto &r : source.rows) {
                calculationCheckpoint();
                Row output = r;
                for (qsizetype column = 0; column < source.fields.size(); ++column) {
                    auto key = source.fields[column];
                    if (name == "UDFTYPE" && key == "udf_type_name") {
                        output.cells[column] = "user_field_" + ids.at("udf_type_id").at(id(source, r, "udf_type_id"));
                        continue;
                    }
                    if (name == "UDFVALUE" && key == "fk_id") {
                        const auto owner = udfOwner.at(id(source, r, "udf_type_id"));
                        key = owner == "TASK"       ? "task_id"
                              : owner == "RSRC"     ? "rsrc_id"
                              : owner == "TASKRSRC" ? "taskrsrc_id"
                                                    : "proj_id";
                    }
                    if (aliases.contains(key))
                        key = aliases.at(key);
                    if (output.cells[column].isEmpty() || !ids.contains(key))
                        continue;
                    const auto value = QString::number(integer(output.cells[column], name + "/" + key));
                    if (!ids.at(key).contains(value))
                        throw AppError(ErrorCode::WORKSPACE_INVALID, {str(name + "/" + key)});
                    output.cells[column] = ids.at(key).at(value);
                }
                target.rows.push_back(std::move(output));
            }
        }
        QJsonArray mappings;
        for (const auto &value : exchange.report["mapping"].toArray()) {
            auto entry = value.toObject();
            const auto kind = entry["kind"].toString();
            if (kind == "udf")
                entry["externalId"] = ids.at("udf_type_id").at(entry["externalId"].toString());
            if (kinds.contains(kind))
                entry["xerId"] = ids.at(kinds.at(kind)).at(entry["xerId"].toString());
            entry["projectId"] = ids.at("proj_id").at("1");
            mappings.append(entry);
        }
        exchange.report["combinedMapping"] = mappings;
        exchange.report["combinedProjectId"] = ids.at("proj_id").at("1");
    }
    QByteArray bytes("\xef\xbb\xbf");
    auto line = [&](const QStringList &cells) {
        bytes += cells.join('\t').toUtf8() + "\r\n";
        if (bytes.size() > byteLimit)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_077);
    };
    line(combined.header);
    for (const char *name : {"CURRTYPE", "PROJECT", "SCHEDOPTIONS", "CALENDAR", "PROJWBS", "TASK", "ACTVTYPE",
                             "ACTVCODE", "TASKACTV", "TASKPRED", "RSRC", "RSRCRATE", "TASKRSRC", "UDFTYPE", "UDFVALUE",
                             "PROJCOST", "FINDATES", "TASKFIN", "TRSRCFIN"}) {
        if (!combined.tables.contains(name))
            continue;
        const auto &t = combined.tables.at(name);
        line({"%T", name});
        QStringList fields{"%F"};
        fields.append(t.fields);
        line(fields);
        for (const auto &r : t.rows) {
            QStringList cells{"%R"};
            cells.append(r.cells);
            line(cells);
        }
    }
    line({"%E"});
    // Validate aggregate limits, IDs, ownership and every selected project after remapping.
    const auto inventory = listPrimaveraXerProjects(bytes);
    if (inventory.size() != exchanges.size())
        throw AppError(ErrorCode::WORKSPACE_INVALID, {"XER 프로젝트 개수가 일치하지 않습니다."});
    for (const auto &info : inventory)
        (void)importPrimaveraXer(bytes, "workspace.xer", XerEncoding::Utf8, info.id);
    return bytes;
}
QString publishPrimaveraXer(const PrimaveraXerExchange &e, const QString &destination) {
    return publishPlanExchange(e.project, e.xer, e.report, e.importing, "xer", destination);
}
QString primaveraXerPreview(const PrimaveraXerExchange &e) {
    auto text =
        QString("%1\n작업 %2 · WBS %3 · 달력 %4 · 자원 %5 · 배정 %6\n\n계획·실적·원가 지원 부분 변환 · P6 재계산/왕복 "
                "호환: 미검증\nPPM "
                "지연 정책: 후행 작업 달력 · 시작일 기준 09:00\n원본 보존: %7\n\n원본/진단은 교환 폴더에 "
                "보관합니다. .ppm만 사본 "
                "저장/이동하면 동반 파일은 자동 복제되지 않습니다.\n\n")
            .arg(qs(e.project.name))
            .arg(e.project.tasks.size())
            .arg(e.project.wbsNodes.size())
            .arg(e.project.calendars.size())
            .arg(e.project.resources.size())
            .arg(e.project.assignments.size())
            .arg(e.importing ? "original.xer 바이트 보존" : "현재 입력/이력의 source.ppm 사본");
    const auto actual = e.report["actualCosts"].toObject();
    text += QString("실적·원가 상세 교환 · 상태일 %1 · 실적 %2건 · 원가 %3건 · 날짜별 원장 %4건\n"
                    "누계 AC에는 날짜별 배분액이 포함됩니다. P6 EVM·과거 정정 이력은 원본/보고를 확인하세요.\n")
                .arg(actual["statusAt"].toString())
                .arg(actual["progressReports"].toInt())
                .arg(actual["costReports"].toInt())
                .arg(actual["datedActuals"].toInt());
    std::map<std::string, const CostEntry *> currentCosts;
    for (const auto &entry : e.project.costEntries)
        if (e.project.statusAt && entry.statusAt <= *e.project.statusAt) {
            auto &current = currentCosts[entry.taskId];
            if (!current || current->statusAt <= entry.statusAt)
                current = &entry;
        }
    int actualRows = 0;
    for (const auto &[taskId, entry] : currentCosts) {
        if (actualRows++ == 2000) {
            text += "이후 원가 명세는 conversion.json 참조\n";
            break;
        }
        if (entry->actualMinor)
            text +=
                QString("작업 %1 · 누계 실제원가 %2 %3\n")
                    .arg(qs(taskId), moneyui::format(entry->actualMinor, e.project.currency), qs(e.project.currency));
    }
    for (const auto &v : e.report["mapping"].toArray()) {
        const auto f = v.toObject();
        if (f["kind"] == "udf")
            text +=
                QString("UDF %1 · %2 → %3 (%4)\n")
                    .arg(f["scope"].toString(), f["name"].toString(), f["externalId"].toString(), f["type"].toString());
    }
    if (e.report.contains("projectSelection")) {
        const auto selection = e.report["projectSelection"].toObject();
        text += QString("선택 프로젝트 ID %1 / 전체 %2개 · 나머지는 변환하지 않습니다.\n"
                        "original.xer에는 선택하지 않은 프로젝트도 포함됩니다.\n\n")
                    .arg(selection["selectedProjectId"].toString())
                    .arg(selection["sourceProjectCount"].toInt());
    }
    int count = 0;
    for (const auto &d : e.report["diagnostics"].toArray()) {
        if (count++ == 2000) {
            text += "이후 진단은 conversion.json 참조\n";
            break;
        }
        auto obj = d.toObject();
        text += obj["path"].toString() + ": " + obj["message"].toString() + "\n";
    }
    return text;
}
} // namespace ppm
