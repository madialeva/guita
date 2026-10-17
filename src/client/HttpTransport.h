#pragma once
#include <client/ApiTransport.h>
#include <QNetworkAccessManager>
#include <QUrl>

// The web and connected modes: Connect's unary RPC over HTTP, binary Protobuf,
// POST <baseUrl><procedure> with the Bearer token
class HttpTransport final : public ApiTransport {
public:
    HttpTransport(QUrl baseUrl, QByteArray token) : m_baseUrl(std::move(baseUrl)), m_token(std::move(token)) {}

    void call(const QString &procedure, const QByteArray &request, QObject *context, Callback done) override;

private:
    QNetworkAccessManager m_nam;
    QUrl m_baseUrl;
    QByteArray m_token;     // empty: no Authorization header
};
