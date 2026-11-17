#include "persistence/Automation.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <set>
#include <map>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::WORKSPACE_INVALID, {message});
}
void keys(const QJsonObject &o, const QStringList &allowed) {
    for (const auto &k : o.keys())
        if (!allowed.contains(k))
            invalid("알 수 없는 자동화 필드: " + k.toStdString());
}
std::string text(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isString() || v.toString().isEmpty() || v.toString().size() > 4096 || v.toString().contains(QChar(0)))
        invalid("자동화 문자열 필드");
    return v.toString().toUtf8().toStdString();
}
std::int64_t number(const QJsonValue &v, std::int64_t maximum) {
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > maximum || n != std::floor(n))
        invalid("자동화 정수 필드");
    return static_cast<std::int64_t>(n);
}
} // namespace
QJsonObject describeAutomationProject(const Project &p) {
    QJsonArray tasks;
    for (const auto &t : p.tasks)
        tasks.append(QJsonObject{{"taskId", QString::fromStdString(t.id)},
                                 {"name", QString::fromStdString(t.name)},
                                 {"durationMinutes", t.durationMinutes ? QJsonValue(qint64(*t.durationMinutes))
                                                                       : QJsonValue(QJsonValue::Null)}});
    return {{"format", "PPM-AUTOMATION-PROJECT-1"},
            {"documentId", QString::fromStdString(p.documentId)},
            {"revision", qint64(p.revision)},
            {"tasks", tasks}};
}
AutomationPreview previewAutomation(Document &document, const QByteArray &bytes,
                                    std::shared_ptr<CalculationControl> control) {
    if (bytes.size() > 1024 * 1024)
        invalid("자동화 요청은 1 MiB 이하입니다.");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        invalid("자동화 JSON");
    const auto root = json.object();
    keys(root, {"format", "documentId", "revision", "requestId", "operations"});
    if (root["format"] != "PPM-AUTOMATION-1" || !document.project() ||
        text(root, "documentId") != document.project()->documentId ||
        number(root["revision"], 9007199254740991LL) != static_cast<std::int64_t>(document.project()->revision))
        invalid("자동화 형식·프로젝트 ID·revision 불일치");
    const auto requestId = text(root, "requestId");
    if (requestId.size() > 200 || requestId.find_first_of("\r\n") != std::string::npos)
        invalid("자동화 요청 ID는 줄바꿈 없는 200바이트 이하입니다.");
    if (!root["operations"].isArray() || root["operations"].toArray().isEmpty() ||
        root["operations"].toArray().size() > 10000)
        invalid("자동화 작업 수 1~10000");
    std::vector<TaskEdit> edits;
    std::set<std::string> taskIds;
    for (const auto &value : root["operations"].toArray()) {
        if (!value.isObject())
            invalid("자동화 작업 형식");
        const auto o = value.toObject();
        keys(o, {"taskId", "name", "durationMinutes"});
        TaskEdit edit;
        edit.taskId = text(o, "taskId");
        if (!taskIds.insert(edit.taskId).second || (!o.contains("name") && !o.contains("durationMinutes")))
            invalid("자동화 작업 중복·빈 변경");
        if (o.contains("name"))
            edit.name = text(o, "name");
        if (o.contains("durationMinutes")) {
            if (o["durationMinutes"].isNull())
                edit.duration = std::optional<std::int64_t>{};
            else
                edit.duration = std::optional<std::int64_t>{number(o["durationMinutes"], 5000000)};
        }
        edits.push_back(std::move(edit));
    }
    const auto revision = document.project()->revision;
    auto prepared =
        Document::prepareEdit(document.makeEditRequest(BatchTaskEdit{std::move(edits)}), std::move(control));
    if (prepared->failure())
        throw AppError(*prepared->failure());
    if (prepared->state() != EditState::Ready && prepared->state() != EditState::NoChange)
        invalid("자동화 미리보기 취소/실패");
    QJsonObject receipt{
        {"format", "PPM-AUTOMATION-RECEIPT-1"},
        {"requestId", QString::fromStdString(requestId)},
        {"documentId", root["documentId"]},
        {"sourceRevision", qint64(revision)},
        {"requestSha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {"state", "preview"},
        {"operations", root["operations"]}};
    QJsonArray changes;
    std::map<std::string, const Task *> tasks;
    for (const auto &task : document.project()->tasks)
        tasks.emplace(task.id, &task);
    for (const auto &operation : root["operations"].toArray()) {
        const auto o = operation.toObject();
        const auto &task = *tasks.at(text(o, "taskId"));
        QJsonObject change{{"taskId", o["taskId"]}};
        if (o.contains("name")) {
            change["beforeName"] = QString::fromStdString(task.name);
            change["afterName"] = o["name"];
        }
        if (o.contains("durationMinutes")) {
            change["beforeDurationMinutes"] =
                task.durationMinutes ? QJsonValue(qint64(*task.durationMinutes)) : QJsonValue(QJsonValue::Null);
            change["afterDurationMinutes"] = o["durationMinutes"];
        }
        changes.append(change);
    }
    receipt["changes"] = changes;
    return {std::move(prepared), std::move(receipt)};
}
QJsonObject applyAutomation(Document &document, AutomationPreview &preview) {
    if (!preview.edit)
        invalid("자동화 미리보기 없음");
    const auto state = document.applyPreparedEdit(*preview.edit);
    if (state != EditState::Applied && state != EditState::NoChange)
        invalid("자동화 원본/역할 변경·취소: 아무 변경도 적용하지 않았습니다.");
    preview.receipt["state"] = state == EditState::Applied ? "applied" : "no-change";
    preview.receipt["resultRevision"] = qint64(document.project()->revision);
    preview.edit.reset();
    return preview.receipt;
}
} // namespace jobs
