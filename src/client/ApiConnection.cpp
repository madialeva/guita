#include <client/ApiConnection.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace {
ApiConnection::Settings &pendingSettings() {
    static ApiConnection::Settings settings;
    return settings;
}
}

void ApiConnection::configure(Settings settings) {
    pendingSettings() = std::move(settings);
}

ApiConnection *ApiConnection::create(QQmlEngine *, QJSEngine *) {
    return new ApiConnection(pendingSettings());
}

ApiConnection::ApiConnection(Settings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)) {}

QNetworkRequest ApiConnection::request(const QString &path) const {
    QNetworkRequest req(m_settings.baseUrl.resolved(QUrl(path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_settings.token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_settings.token);
    return req;
}

QNetworkReply *ApiConnection::get(const QString &path) {
    return m_nam.get(request(path));
}

QNetworkReply *ApiConnection::post(const QString &path, const QJsonDocument &body) {
    return m_nam.post(request(path), body.toJson(QJsonDocument::Compact));
}

QString ApiConnection::errorMessage(QNetworkReply *reply, const QByteArray &body) {
    if (reply->error() == QNetworkReply::NoError)
        return {};
    const auto message = QJsonDocument::fromJson(body).object().value("error").toString();
    return message.isEmpty() ? reply->errorString() : message;
}
