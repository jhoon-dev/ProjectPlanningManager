#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
#include <functional>
#include <set>
namespace ppm {
class ServerClient final : public QObject {
public:
    struct Result {
        int status = 0;
        QJsonObject json;
        QByteArray bytes;
        QString error;
        bool ok() const {
            return error.isEmpty() && status >= 200 && status < 300;
        }
    };
    using Callback = std::function<void(Result)>;
    explicit ServerClient(QObject *parent = nullptr);
    ~ServerClient() override;
    void setEndpoint(const QUrl &);
    QUrl endpoint() const {
        return endpoint_;
    }
    void login(const QString &, const QString &, Callback);
    void logout(Callback = {});
    void request(const QByteArray &method, const QString &path, const QJsonObject &, Callback);
    void upload(const QByteArray &, Callback);
    void download(const QString &projectId, Callback);
    void follow(std::function<void(const QJsonObject &)>, std::function<void(const QString &)>);
    void stopFollowing();
    bool authenticated() const {
        return !token_.isEmpty();
    }
    static QString projectPath(const QString &);

private:
    QNetworkAccessManager network_;
    QUrl endpoint_;
    QByteArray token_;
    std::uint64_t generation_ = 0;
    qint64 cursor_ = 0;
    bool following_ = false, polling_ = false;
    std::function<void(const QJsonObject &)> changes_;
    std::function<void(const QString &)> streamError_;
    void send(const QByteArray &, const QString &, QByteArray, QByteArray, Callback, bool poll = false);
    void poll();
};
} // namespace ppm
