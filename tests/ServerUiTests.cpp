#include "collaboration/ServerClient.h"
#include "ui/ServerDialog.h"
#include "ui/DesktopWorkspace.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void wait(const std::function<bool()> &ready) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 45000)
        QTest::qWait(10);
    check(ready(), "async operation timed out");
}
template <class T> T *child(QWidget &w, const char *id) {
    auto *p = w.findChild<T *>(id);
    check(p, id);
    return p;
}
ServerClient::Result response(ServerClient &client, const QByteArray &method, const QString &path,
                              const QJsonObject &body = {}) {
    bool done = false;
    ServerClient::Result result;
    client.request(method, path, body, [&](auto r) {
        result = std::move(r);
        done = true;
    });
    wait([&] { return done; });
    return result;
}
void login(ServerClient &client, const QUrl &url, const QString &user) {
    client.setEndpoint(url);
    bool done = false;
    bool success = false;
    client.login(user, "PPM integration password 123", [&](auto r) {
        success = r.ok();
        check(!r.json.contains("token") && r.bytes.isEmpty(), "view received session secret");
        done = true;
    });
    wait([&] { return done; });
    check(success, "native login failed");
}
void tests() {
    QTemporaryDir temp;
    const auto ready = temp.filePath("ready.json");
    QProcess process;
    process.start(PPM_SERVER_TEST_PYTHON, {PPM_SERVER_FIXTURE, "--engine", PPM_SERVER_TEST_ENGINE, "--database",
                                           temp.filePath("service.sqlite"), "--ready", ready});
    check(process.waitForStarted(), "fixture process failed to start");
    struct Cleanup {
        QProcess &p;
        ~Cleanup() {
            p.kill();
            p.waitForFinished();
        }
    } cleanup{process};
    wait([&] { return QFile::exists(ready) || process.state() == QProcess::NotRunning; });
    check(QFile::exists(ready), process.readAllStandardError().constData());
    QFile file(ready);
    check(file.open(QIODevice::ReadOnly), "ready file");
    const QUrl url(QJsonDocument::fromJson(file.readAll()).object().value("url").toString());
    ServerClient first, second;
    bool denied = false;
    try {
        first.setEndpoint(QUrl("http://example.com"));
    } catch (const std::exception &) {
        denied = true;
    }
    check(denied, "remote cleartext accepted");
    login(first, url, "editor");
    login(second, url, "editor");
    auto snapshot = response(first, "GET", "/projects/server-fixture");
    check(snapshot.ok(), "native read failed");
    const auto batch = [&](const QString &name, const QString &requestId) {
        return QJsonObject{
            {"expectedVersion", snapshot.json["version"]},
            {"request", QJsonObject{{"format", "PPM-AUTOMATION-1"},
                                    {"documentId", "server-fixture"},
                                    {"revision", snapshot.json["summary"].toObject()["revision"]},
                                    {"requestId", requestId},
                                    {"operations", QJsonArray{QJsonObject{{"taskId", "A"}, {"name", name}}}}}}};
    };
    auto a = response(first, "POST", "/projects/server-fixture/preview", batch("Native client A", "native-A"));
    auto b = response(second, "POST", "/projects/server-fixture/preview", batch("Native client B", "native-B"));
    check(a.ok() && b.ok(), "native previews failed");
    bool observed = false;
    second.follow(
        [&](const QJsonObject &events) {
            for (const auto &e : events["events"].toArray())
                if (e.toObject()["kind"] == "project-changed")
                    observed = true;
        },
        [](const QString &) {});
    check(response(first, "POST", "/projects/server-fixture/apply", {{"previewId", a.json["previewId"]}}).ok(),
          "native apply failed");
    wait([&] { return observed; });
    check(response(second, "POST", "/projects/server-fixture/apply", {{"previewId", b.json["previewId"]}}).status ==
              409,
          "native stale preview overwrote winner");
    second.stopFollowing();
    auto files = std::make_shared<JsonProjectStore>();
    DesktopWorkspace workspace(files, nullptr);
    ServerDialog dialog(workspace);
    dialog.show();
    child<QLineEdit>(dialog, "serverUrl")->setText(url.toString());
    child<QLineEdit>(dialog, "serverUser")->setText("editor");
    child<QLineEdit>(dialog, "serverPassword")->setText("PPM integration password 123");
    child<QPushButton>(dialog, "serverLogin")->click();
    auto *projects = child<QComboBox>(dialog, "serverProjects");
    wait([&] { return projects->count() == 1; });
    check(child<QLineEdit>(dialog, "serverPassword")->text().isEmpty(), "password remained in editor");
    child<QPushButton>(dialog, "serverOpen")->click();
    auto *table = child<QTableWidget>(dialog, "serverTasks");
    wait([&] { return table->rowCount() == 2; });
    table->item(0, 0)->setText("Pending native draft");
    snapshot = response(first, "GET", "/projects/server-fixture");
    auto remote = response(first, "POST", "/projects/server-fixture/preview", batch("Remote changed", "remote-change"));
    check(remote.ok(), "remote preview");
    check(response(first, "POST", "/projects/server-fixture/apply", {{"previewId", remote.json["previewId"]}}).ok(),
          "remote apply");
    auto *status = child<QLabel>(dialog, "serverStatus");
    wait([&] { return status->text().contains("보존"); });
    check(table->item(0, 0)->text() == "Pending native draft", "realtime update erased local draft");
    child<QPushButton>(dialog, "serverPreview")->click();
    wait([&] { return status->text().contains("Source changed"); });
    check(!child<QPushButton>(dialog, "serverApply")->isEnabled(), "stale draft enabled apply");
    child<QPushButton>(dialog, "serverReload")->click();
    wait([&] { return table->item(0, 0)->text() == "Remote changed"; });
    table->item(0, 0)->setText("Reviewed native change");
    child<QPushButton>(dialog, "serverPreview")->click();
    wait([&] { return child<QPushButton>(dialog, "serverApply")->isEnabled(); });
    check(child<QPlainTextEdit>(dialog, "serverReceipt")->toPlainText().contains("beforeName"),
          "preview not reviewable");
    QTest::qWait(30);
    QDir().mkpath("server-qa");
    dialog.grab().save("server-qa/server-collaboration-ui.png");
    child<QPushButton>(dialog, "serverApply")->click();
    wait([&] { return table->isEnabled() && child<QLabel>(dialog, "serverSource")->text().contains("4"); });
    check(response(first, "GET", "/projects/server-fixture")
                  .json["summary"]
                  .toObject()["tasks"]
                  .toArray()[0]
                  .toObject()["name"] == "Reviewed native change",
          "GUI server commit missing");
    child<QPushButton>(dialog, "serverLogout")->click();
    wait([&] { return table->rowCount() == 0; });
    check(status->text().contains("로그아웃"), "logout UI failed");
    const auto secureReady = temp.filePath("secure-ready.json");
    QProcess secureProcess;
    secureProcess.start(PPM_SERVER_TEST_PYTHON, {PPM_SERVER_FIXTURE, "--engine", PPM_SERVER_TEST_ENGINE, "--database",
                                                 temp.filePath("secure.sqlite"), "--ready", secureReady, "--tls"});
    check(secureProcess.waitForStarted(), "TLS fixture failed to start");
    Cleanup secureCleanup{secureProcess};
    wait([&] { return QFile::exists(secureReady) || secureProcess.state() == QProcess::NotRunning; });
    QFile secureFile(secureReady);
    check(secureFile.open(QIODevice::ReadOnly), secureProcess.readAllStandardError().constData());
    const QUrl secureUrl(QJsonDocument::fromJson(secureFile.readAll()).object().value("url").toString());
    ServerClient secureClient;
    secureClient.setEndpoint(secureUrl);
    bool completed = false;
    ServerClient::Result untrusted;
    secureClient.login("editor", "PPM integration password 123", [&](auto r) {
        untrusted = std::move(r);
        completed = true;
    });
    wait([&] { return completed; });
    check(!untrusted.ok() && !secureClient.authenticated() &&
              (untrusted.error.contains("SSL", Qt::CaseInsensitive) ||
               untrusted.error.contains("certificate", Qt::CaseInsensitive)),
          "native TLS accepted an untrusted certificate or did not report TLS failure");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    try {
        tests();
        std::cout << "server native client and UI passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
