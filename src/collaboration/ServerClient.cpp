#include "collaboration/ServerClient.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <stdexcept>
namespace jobs {
ServerClient::ServerClient(QObject *parent) : QObject(parent), network_(this) {}
ServerClient::~ServerClient() {
    following_ = false;
}
void ServerClient::setEndpoint(const QUrl &url) {
    const auto host = url.host().toLower();
    const bool local = QHostAddress(host).isLoopback() || host == "localhost";
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (!url.path().isEmpty() && url.path() != "/") || (url.scheme() != "https" && !(url.scheme() == "http" && local)))
        throw std::runtime_error("HTTPS 서버 주소 또는 loopback HTTP 주소를 입력하세요.");
    following_ = false;
    polling_ = false;
    ++generation_;
    for (auto *reply : network_.findChildren<QNetworkReply *>())
        reply->abort();
    token_.clear();
    cursor_ = 0;
    endpoint_ = url;
}
QString ServerClient::projectPath(const QString &id) {
    return "/projects/" + QString::fromLatin1(QUrl::toPercentEncoding(id));
}
void ServerClient::send(const QByteArray &method, const QString &path, QByteArray body, QByteArray contentType,
                        Callback callback, bool pollRequest) {
    if (endpoint_.isEmpty()) {
        if (callback) {
            Result result;
            result.error = "서버 주소를 먼저 설정하세요.";
            callback(std::move(result));
        }
        return;
    }
    const auto generation = generation_;
    QNetworkRequest request(endpoint_.resolved(QUrl("/api/v1" + path)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(pollRequest ? 35000 : 45000);
    request.setRawHeader("Accept", "application/json");
    if (!token_.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token_);
    if (!contentType.isEmpty())
        request.setRawHeader("Content-Type", contentType);
    auto *reply = network_.sendCustomRequest(request, method, body);
    auto bytes = std::make_shared<QByteArray>();
    auto exceeded = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, exceeded] {
        bytes->append(reply->readAll());
        if (bytes->size() > 64 * 1024 * 1024) {
            *exceeded = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, exceeded, callback = std::move(callback), generation] {
                bytes->append(reply->readAll());
                Result result;
                result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.bytes = std::move(*bytes);
                const auto json = QJsonDocument::fromJson(result.bytes);
                if (json.isObject())
                    result.json = json.object();
                if (*exceeded || result.bytes.size() > 64 * 1024 * 1024)
                    result.error = "서버 응답이 64 MiB를 초과했습니다.";
                else if (result.status < 200 || result.status >= 300 || reply->error() != QNetworkReply::NoError) {
                    result.error = result.json.value("error").toObject().value("message").toString();
                    if (result.error.isEmpty())
                        result.error = reply->errorString();
                }
                reply->deleteLater();
                if (generation != generation_)
                    return;
                if (result.status == 401) {
                    token_.clear();
                    following_ = false;
                }
                if (callback)
                    callback(std::move(result));
            });
}
void ServerClient::request(const QByteArray &method, const QString &path, const QJsonObject &json, Callback callback) {
    const bool body = method != "GET" && method != "DELETE";
    send(method, path, body ? QJsonDocument(json).toJson(QJsonDocument::Compact) : QByteArray{},
         body ? QByteArray("application/json") : QByteArray{}, std::move(callback));
}
void ServerClient::login(const QString &user, const QString &password, Callback callback) {
    request("POST", "/login", {{"username", user}, {"password", password}},
            [this, callback = std::move(callback)](Result result) {
                if (result.ok()) {
                    token_ = result.json.value("token").toString().toUtf8();
                    cursor_ = 0;
                }
                // Do not expose the bearer secret to views, logs, or saved receipts.
                result.json.remove("token");
                result.bytes.clear();
                if (callback)
                    callback(std::move(result));
            });
}
void ServerClient::logout(Callback callback) {
    stopFollowing();
    request("POST", "/logout", {}, [this, callback = std::move(callback)](Result result) {
        token_.clear();
        polling_ = false;
        ++generation_;
        if (callback)
            callback(std::move(result));
    });
}
void ServerClient::upload(const QByteArray &native, Callback callback) {
    send("POST", "/projects", native, "application/octet-stream", std::move(callback));
}
void ServerClient::download(const QString &id, Callback callback) {
    send("GET", projectPath(id) + "/native", {}, {}, std::move(callback));
}
void ServerClient::follow(std::function<void(const QJsonObject &)> changed,
                          std::function<void(const QString &)> error) {
    changes_ = std::move(changed);
    streamError_ = std::move(error);
    following_ = true;
    poll();
}
void ServerClient::stopFollowing() {
    following_ = false;
}
void ServerClient::poll() {
    if (!following_ || polling_ || token_.isEmpty())
        return;
    polling_ = true;
    const auto generation = generation_;
    send(
        "GET", QString("/events?after=%1&wait=25").arg(cursor_), {}, {},
        [this, generation](Result result) {
            polling_ = false;
            if (generation != generation_)
                return;
            if (result.status == 401 && streamError_)
                streamError_("세션이 만료되거나 해제되었습니다. 다시 로그인하세요.");
            if (!following_)
                return;
            if (result.ok()) {
                cursor_ = qint64(result.json.value("cursor").toDouble());
                if (changes_)
                    changes_(result.json);
            } else if (streamError_)
                streamError_(result.error);
            if (following_)
                QTimer::singleShot(result.ok() ? 10 : 2000, this, [this] { poll(); });
        },
        true);
}
} // namespace jobs
