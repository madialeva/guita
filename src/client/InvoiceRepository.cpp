#include <client/InvoiceRepository.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

void InvoiceRepository::setConnection(ApiConnection *connection) {
    if (m_connection == connection) return;
    m_connection = connection;
    emit connectionChanged();
}

void InvoiceRepository::create(const QVariantMap &invoice) {
    if (!m_connection) return;
    auto *reply = m_connection->post("invoices", QJsonDocument(QJsonObject::fromVariantMap(invoice)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto body = reply->readAll();
        if (const auto error = ApiConnection::errorMessage(reply, body); !error.isEmpty()) {
            emit failed(error);
            return;
        }
        emit created(QJsonDocument::fromJson(body).object().toVariantMap());
    });
}

void InvoiceRepository::list() {
    if (!m_connection) return;
    auto *reply = m_connection->get("invoices");
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto body = reply->readAll();
        if (const auto error = ApiConnection::errorMessage(reply, body); !error.isEmpty()) {
            emit failed(error);
            return;
        }
        emit listed(QJsonDocument::fromJson(body).array().toVariantList());
    });
}
