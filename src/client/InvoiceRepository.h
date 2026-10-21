#pragma once
#include <client/ApiConnection.h>
#include <client/InvoiceRow.h>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

// guita.v1.InvoiceService for the views: builds the contract's requests,
// decodes its responses and events and hands the views typed InvoiceRow
// values. Every call is asynchronous (a WASM requirement): results arrive
// through signals.
class InvoiceRepository : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ApiConnection *connection READ connection WRITE setConnection
               NOTIFY connectionChanged REQUIRED)

public:
    explicit InvoiceRepository(QObject *parent = nullptr) : QObject(parent) {}

    ApiConnection *connection() const { return m_connection; }
    void setConnection(ApiConnection *connection);

    // An empty tax ID means none; totalCents is a whole number of cents. Each
    // call carries a new request ID, so it may be sent again after a lost
    // connection without creating the invoice twice
    Q_INVOKABLE void create(const QString &customer, const QString &customerTaxId, double totalCents);
    Q_INVOKABLE void list();
    // Subscribes to the invoices others create, once per repository: watching()
    // first, and again after a lost connection (reload then), and
    // invoiceCreated() for each invoice
    Q_INVOKABLE void watch();

signals:
    void connectionChanged();
    void created(const InvoiceRow &invoice);
    void listed(const QList<InvoiceRow> &invoices);
    void watching();
    void invoiceCreated(const InvoiceRow &invoice);
    void failed(const QString &message);

private:
    QPointer<ApiConnection> m_connection;
    bool m_watching = false;
};
