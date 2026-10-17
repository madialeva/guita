#include <client/HttpTransport.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

void HttpTransport::call(const QString &procedure, const QByteArray &request, QObject *context, Callback done) {
    QNetworkRequest req(m_baseUrl.resolved(QUrl(procedure)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/proto"));
    req.setRawHeader("Connect-Protocol-Version", "1");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token);

    auto *reply = m_nam.post(req, request);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        reply->deleteLater();
        const auto body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            done({true, body, {}});
            return;
        }
        // Connect errors carry {"code", "message"}; otherwise Qt's own message
        const auto message = QJsonDocument::fromJson(body).object().value("message").toString();
        done({false, {}, message.isEmpty() ? reply->errorString() : message});
    });
}
