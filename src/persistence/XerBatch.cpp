#include "domain/ResourceAvailability.h"
#include "persistence/XerBatch.h"
#include "domain/CalculationControl.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString projectFolder(std::size_t i) {
    return QString("projects/%1").arg(i + 1, 3, 10, QChar('0'));
}
QJsonObject manifest(bool importing) {
    return {{"contract", "jobs-xer-workspace-v1"},
            {"direction", importing ? "import" : "export"},
            {"atomicity", "all-or-nothing-new-folder"},
            {"externalRelations", "reject-incoming-and-outgoing"},
            {"resourceIdentity", "source-hash-and-resource-id; never-by-name"},
            {"projection", "duration-based-plan; actuals-costs-preserved-in-original-or-native"}};
}
void addProject(XerBatchExchange &batch, PrimaveraXerExchange exchange) {
    if (batch.projects.size() >= 100)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    batch.workspace.projects.push_back({exchange.project.documentId, {}, exchange.project.name});
    batch.projects.push_back(std::move(exchange));
}
TaskCalendar resourceCalendar(const Project &p, const Resource &r) {
    TaskCalendar calendar;
    if (r.calendarId.empty())
        calendar.holidays = p.holidays;
    else {
        auto found =
            std::find_if(p.calendars.begin(), p.calendars.end(), [&](const auto &c) { return c.id == r.calendarId; });
        if (found == p.calendars.end())
            throw AppError(ErrorCode::WORKSPACE_RESOURCE_MISSING, {r.id});
        calendar = *found;
    }
    calendar.id.clear();
    calendar.name.clear();
    return calendar;
}
void validateBatch(const XerBatchExchange &batch) {
    validateWorkspace(batch.workspace);
    if (batch.projects.empty() || batch.projects.size() != batch.workspace.projects.size())
        throw AppError(ErrorCode::WORKSPACE_NO_PROJECTS);
    std::map<std::string, const Project *> projects;
    for (std::size_t i = 0; i < batch.projects.size(); ++i) {
        calculationCheckpoint();
        const auto &e = batch.projects[i];
        validate(e.project);
        if (e.importing != batch.importing || e.project.documentId != batch.workspace.projects[i].documentId)
            throw AppError(ErrorCode::WORKSPACE_PROJECT_ID_MISMATCH, {e.project.documentId});
        projects.emplace(e.project.documentId, &e.project);
    }
    for (const auto &binding : batch.workspace.bindings) {
        const auto &p = *projects.at(binding.projectId);
        auto resource = std::find_if(p.resources.begin(), p.resources.end(),
                                     [&](const auto &r) { return r.id == binding.resourceId; });
        auto shared = std::find_if(batch.workspace.resources.begin(), batch.workspace.resources.end(),
                                   [&](const auto &r) { return r.id == binding.sharedId; });
        if (resource == p.resources.end() || resource->type != shared->type)
            throw AppError(ErrorCode::WORKSPACE_RESOURCE_MISSING, {binding.resourceId});
    }
}
} // namespace
XerBatchExchange importXerBatch(const std::vector<XerBatchSource> &sources) {
    if (sources.empty() || sources.size() > 100)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    XerBatchExchange batch;
    batch.workspace.id = uuid();
    batch.report = manifest(true);
    QJsonArray inputs, diagnostics;
    std::set<QString> sourceHashes;
    std::map<QString, std::vector<std::pair<std::size_t, std::string>>> resources;
    qint64 totalBytes = 0;
    for (const auto &source : sources) {
        calculationCheckpoint();
        totalBytes += source.bytes.size();
        if (totalBytes > 64 * 1024 * 1024)
            throw AppError(ErrorCode::WORKSPACE_LIMIT);
        const auto digest = hash(source.bytes);
        if (!sourceHashes.insert(digest).second)
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"동일한 XER 원본이 중복 선택되었습니다."});
        const auto inventory = listPrimaveraXerProjects(source.bytes, source.encoding);
        if (source.projectIds.empty())
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"가져올 프로젝트를 선택하세요."});
        std::set<QString> selected;
        for (const auto &pid : source.projectIds) {
            if (!selected.insert(pid).second ||
                std::none_of(inventory.begin(), inventory.end(), [&](const auto &p) { return p.id == pid; }))
                throw AppError(ErrorCode::WORKSPACE_INVALID, {"선택한 XER 프로젝트 ID가 중복되었거나 없습니다."});
            auto exchange = importPrimaveraXer(source.bytes, source.name, source.encoding, pid);
            exchange.report["batchSourceIndex"] = qint64(batch.originals.size());
            exchange.report["batchSourceProjectId"] = pid;
            exchange.report["originalFile"] =
                QString("../../originals/%1.xer").arg(batch.originals.size() + 1, 3, 10, QChar('0'));
            for (const auto &item : exchange.report["mapping"].toArray()) {
                const auto mapping = item.toObject();
                if (mapping["kind"].toString() == "resource")
                    resources[digest + "/" + mapping["xerId"].toString()].push_back(
                        {batch.projects.size(), mapping["jobsId"].toString().toUtf8().toStdString()});
            }
            addProject(batch, std::move(exchange));
        }
        inputs.append(
            QJsonObject{{"name", QFileInfo(source.name).fileName()},
                        {"sha256", digest},
                        {"file", QString("originals/%1.xer").arg(batch.originals.size() + 1, 3, 10, QChar('0'))},
                        {"encoding", source.encoding == XerEncoding::Utf8 ? "UTF-8" : "Windows-1252"},
                        {"selectedProjectIds", QJsonArray::fromStringList(source.projectIds)},
                        {"projectCount", qint64(inventory.size())}});
        batch.originals.push_back(source.bytes);
    }
    for (const auto &[sourceId, occurrences] : resources) {
        if (occurrences.size() < 2)
            continue;
        auto local = [&](const auto &entry) -> const Resource & {
            const auto &p = batch.projects[entry.first].project;
            return *std::find_if(p.resources.begin(), p.resources.end(),
                                 [&](const auto &r) { return r.id == entry.second; });
        };
        const auto &first = local(occurrences.front());
        const auto calendar = resourceCalendar(batch.projects[occurrences.front().first].project, first);
        const bool same = std::all_of(occurrences.begin(), occurrences.end(), [&](const auto &entry) {
            const auto &r = local(entry);
            return !hasResourceAvailability(r) && !hasResourceAvailability(first) && r.name == first.name &&
                   r.type == first.type && r.maxUnits == first.maxUnits && r.exceptions == first.exceptions &&
                   resourceCalendar(batch.projects[entry.first].project, r) == calendar;
        });
        if (!same) {
            diagnostics.append(QJsonObject{
                {"sourceResource", sourceId},
                {"message", "프로젝트별 달력·가용량이 다르거나 기간별 가용량이 있어 자동 공유 연결을 생략했습니다."}});
            continue;
        }
        const auto sharedId = uuid();
        batch.workspace.resources.push_back(
            {sharedId, first.name, first.type, first.maxUnits, first.exceptions, calendar});
        for (const auto &entry : occurrences)
            batch.workspace.bindings.push_back(
                {batch.projects[entry.first].project.documentId, entry.second, sharedId});
    }
    batch.report["sources"] = inputs;
    batch.report["diagnostics"] = diagnostics;
    validateBatch(batch);
    return batch;
}
XerBatchExchange exportXerBatch(const std::vector<Project> &projects, const Workspace &workspace) {
    XerBatchExchange batch;
    batch.importing = false;
    batch.workspace.id = uuid();
    batch.report = manifest(false);
    std::set<std::string> selected;
    for (const auto &project : projects) {
        if (!selected.insert(project.documentId).second)
            throw AppError(ErrorCode::WORKSPACE_DUPLICATE_PROJECT, {project.documentId});
        addProject(batch, exportPrimaveraXer(project));
    }
    if (!workspace.id.empty()) {
        validateWorkspace(workspace);
        batch.workspace.integrated = workspace.integrated;
        for (const auto &link : workspace.links) {
            const bool from = selected.contains(link.predecessorProjectId),
                       to = selected.contains(link.successorProjectId);
            if (from != to)
                throw AppError(ErrorCode::WORKSPACE_INVALID, {"프로젝트 간 링크의 양쪽 프로젝트를 함께 내보내세요."});
            if (from)
                batch.workspace.links.push_back(link);
        }
        std::set<std::string> shared;
        for (const auto &binding : workspace.bindings)
            if (selected.contains(binding.projectId)) {
                batch.workspace.bindings.push_back(binding);
                shared.insert(binding.sharedId);
            }
        for (const auto &resource : workspace.resources)
            if (shared.contains(resource.id))
                batch.workspace.resources.push_back(resource);
    }
    validateBatch(batch);
    batch.combined = combinePrimaveraXer(batch.projects);
    batch.report["xerSha256"] = hash(batch.combined);
    batch.report["diagnostics"] = QJsonArray{
        QJsonObject{{"message", "XER 자원·달력은 프로젝트별 ID로 분리합니다. 명시적 공유 자원 연결은 "
                                "workspace.ppmworkspace에 보존합니다. 상태일의 실적·원가는 상세 교환 계약을 따르며, "
                                "정정 이력·기준선·자재·역할은 source.ppm에, 프로젝트 간 "
                                "링크와 통합 옵션은 workspace.ppmworkspace에 보존하며 XER에는 투영하지 않습니다."}}};
    return batch;
}
QString publishXerBatch(const XerBatchExchange &batch, const QString &destination,
                        std::function<void()> beforePublish) {
    calculationCheckpoint();
    validateBatch(batch);
    const QFileInfo target(destination);
    const auto parent = target.dir().canonicalPath();
    if (parent.isEmpty() || target.fileName().isEmpty() || target.fileName() == "." || target.fileName() == ".." ||
        target.exists() || target.isSymLink())
        throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_002);
    QDir directory(parent);
    QTemporaryDir stage(directory.filePath(".ppm-xer-batch-XXXXXX"));
    if (!stage.isValid())
        throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_003);
    auto write = [&](const QString &name, const QByteArray &bytes) {
        calculationCheckpoint();
        if (!QDir(stage.path()).mkpath(QFileInfo(name).path()))
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_003);
        QSaveFile file(stage.filePath(name));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_004, {name.toUtf8().toStdString()});
        QFile check(stage.filePath(name));
        if (!check.open(QIODevice::ReadOnly) || check.readAll() != bytes)
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_005, {name.toUtf8().toStdString()});
    };
    JsonProjectStore store;
    auto workspace = batch.workspace;
    auto report = batch.report;
    QJsonArray outputs;
    for (std::size_t i = 0; i < batch.projects.size(); ++i) {
        calculationCheckpoint();
        const auto &exchange = batch.projects[i];
        const auto prefix = projectFolder(i);
        const auto native = prefix + (batch.importing ? "/project.ppm" : "/source.ppm");
        if (!QDir(stage.path()).mkpath(prefix))
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_003);
        const auto path = stage.filePath(native).toUtf8().toStdString();
        store.save(path, exchange.project, {});
        if (store.load(path).project != exchange.project)
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_006);
        workspace.projects[i].path = path;
        QFile file(stage.filePath(native));
        if (!file.open(QIODevice::ReadOnly))
            throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_007);
        auto conversion = exchange.report;
        conversion["nativeSha256"] = hash(file.readAll());
        conversion["nativeFile"] = QFileInfo(native).fileName();
        // Import originals are stored once, without duplicating large multi-project files.
        if (!batch.importing)
            write(prefix + "/project.xer", exchange.xer);
        write(prefix + "/conversion.json", QJsonDocument(conversion).toJson());
        outputs.append(QJsonObject{{"documentId", qs(exchange.project.documentId)},
                                   {"name", qs(exchange.project.name)},
                                   {"nativeFile", native},
                                   {"nativeSha256", conversion["nativeSha256"]},
                                   {"conversion", prefix + "/conversion.json"},
                                   {"combinedProjectId", conversion["combinedProjectId"]},
                                   {"sourceIndex", conversion["batchSourceIndex"]},
                                   {"sourceProjectId", conversion["batchSourceProjectId"]}});
    }
    for (std::size_t i = 0; i < batch.originals.size(); ++i)
        write(QString("originals/%1.xer").arg(i + 1, 3, 10, QChar('0')), batch.originals[i]);
    if (!batch.importing)
        write("workspace.xer", batch.combined);
    JsonWorkspaceStore workspaceStore(store);
    const auto path = stage.filePath("workspace.ppmworkspace").toUtf8().toStdString();
    workspaceStore.save(path, workspace, {});
    if (workspaceStore.load(path).workspace != workspace)
        throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_006);
    report["projects"] = outputs;
    report["sharedResources"] = qint64(workspace.resources.size());
    report["bindings"] = qint64(workspace.bindings.size());
    write("conversion.json", QJsonDocument(report).toJson());
    if (beforePublish)
        beforePublish();
    calculationCheckpoint();
    const auto finalPath = directory.filePath(target.fileName());
    if (QFileInfo::exists(finalPath) || QFileInfo(finalPath).isSymLink() ||
        !directory.rename(QFileInfo(stage.path()).fileName(), target.fileName()))
        throw AppError(ErrorCode::FILE_EXCHANGEPACKAGE_008);
    stage.setAutoRemove(false);
    return finalPath;
}
QString xerBatchPreview(const XerBatchExchange &batch) {
    QString text =
        QString("%1 · 프로젝트 %2개 · 공유 자원 %3개 · 연결 %4개\n전체 성공 시 새 교환 폴더를 저장합니다.\n프로젝트 간 "
                "외부 관계는 차단합니다.\n인코딩은 명시적으로 선택하며 이름으로 자원·달력을 합치지 않습니다.\n\n")
            .arg(batch.importing ? "XER 일괄 가져오기" : "XER 일괄 내보내기")
            .arg(batch.projects.size())
            .arg(batch.workspace.resources.size())
            .arg(batch.workspace.bindings.size());
    for (std::size_t i = 0; i < batch.projects.size(); ++i) {
        auto detail = primaveraXerPreview(batch.projects[i]);
        if (batch.importing) {
            const auto source = batch.projects[i].report["batchSourceIndex"].toInt();
            detail.replace("original.xer", QString("originals/%1.xer").arg(source + 1, 3, 10, QChar('0')));
        }
        text += projectFolder(i) + "\n" + detail + "\n\n";
    }
    text += QString::fromUtf8(QJsonDocument(batch.report).toJson());
    return text;
}
} // namespace jobs
