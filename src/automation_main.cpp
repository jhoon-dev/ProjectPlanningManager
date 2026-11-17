#include "persistence/Automation.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "PPM local JSON API: inspect or preview/apply one atomic task batch. No server or authentication.");
    parser.addHelpOption();
    parser.addOptions({{{"p", "project"}, "Native project file", "path"},
                       {{"r", "request"}, "JSON request; omit to inspect IDs/revision", "path"},
                       {"apply", "Apply and atomically save; default is preview"},
                       {"workspace", "Optional local role policy file", "path"},
                       {"actor", "Local member ID (requires --workspace)", "id"}});
    parser.process(app);
    try {
        if (!parser.isSet("project") || (parser.isSet("apply") && !parser.isSet("request")) ||
            (parser.isSet("actor") != parser.isSet("workspace")))
            throw std::runtime_error(
                "--project required; --apply requires --request; --workspace and --actor are paired");
        jobs::JsonProjectStore files;
        jobs::Document document(files);
        document.open(parser.value("project").toStdString(), !parser.isSet("request"));
        std::optional<jobs::Workspace> policy;
        if (parser.isSet("workspace")) {
            policy = jobs::JsonWorkspaceStore(files).load(parser.value("workspace").toStdString()).workspace;
            const auto actor = parser.value("actor").toStdString();
            const auto id = document.project()->documentId;
            bool listed = false;
            for (const auto &ref : policy->projects)
                listed |= ref.documentId == id && files.sameFile(ref.path, document.path());
            if (!listed ||
                !jobs::permits(*policy, actor, id,
                               parser.isSet("request") ? jobs::WorkspaceAction::Edit : jobs::WorkspaceAction::View))
                throw std::runtime_error("local workspace role denied");
        }
        QJsonObject result;
        if (!parser.isSet("request"))
            result = jobs::describeAutomationProject(*document.project());
        else {
            QFile request(parser.value("request"));
            if (!request.open(QIODevice::ReadOnly) || request.size() > 1024 * 1024)
                throw std::runtime_error("request unreadable or exceeds 1 MiB");
            const auto bytes = request.read(1024 * 1024 + 1);
            auto preview = jobs::previewAutomation(document, bytes);
            result = preview.receipt;
            if (parser.isSet("apply")) {
                result = jobs::applyAutomation(document, preview);
                document.save();
                result["saved"] = true;
            }
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return 0;
    } catch (const std::exception &e) {
        QJsonObject result{{"state", "failed"}, {"message", QString::fromUtf8(e.what())}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return 1;
    }
}
