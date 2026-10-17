#pragma once
#include <client/ApiConnection.h>
#include <client/InvoiceRow.h>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

// guita.v1.InvoiceService for the views: builds the contract's requests,
// decodes its responses and hands the views typed InvoiceRow values. Every
// call is asynchronous (a WASM requirement): results arrive through signals.
class InvoiceRepository : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ApiConnection *connection READ connection WRITE setConnection
               NOTIFY connectionChanged REQUIRED)

public:
    explicit InvoiceRepository(QObject *parent = nullptr) : QObject(parent) {}

    ApiConnection *connection() const { return m_connection; }
    void setConnection(ApiConnection *connection);

    // An empty tax ID means none; totalCents is a whole number of cents
    Q_INVOKABLE void create(const QString &customer, const QString &customerTaxId, double totalCents);
    Q_INVOKABLE void list();

signals:
    void connectionChanged();
    void created(const InvoiceRow &invoice);
    void listed(const QList<InvoiceRow> &invoices);
    void failed(const QString &message);

private:
    QPointer<ApiConnection> m_connection;
};
