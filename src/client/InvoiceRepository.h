#pragma once
#include <client/ApiConnection.h>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

// HTTP access to /api/invoices, identical in every mode. Everything is
// asynchronous (a WASM requirement): results arrive through signals.
class InvoiceRepository : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ApiConnection *connection READ connection WRITE setConnection
               NOTIFY connectionChanged REQUIRED)

public:
    explicit InvoiceRepository(QObject *parent = nullptr) : QObject(parent) {}

    ApiConnection *connection() const { return m_connection; }
    void setConnection(ApiConnection *connection);

    // {customer, customerTaxId, totalCents}
    Q_INVOKABLE void create(const QVariantMap &invoice);
    Q_INVOKABLE void list();

signals:
    void connectionChanged();
    void created(const QVariantMap &invoice);
    void listed(const QVariantList &invoices);
    void failed(const QString &message);

private:
    QPointer<ApiConnection> m_connection;
};
