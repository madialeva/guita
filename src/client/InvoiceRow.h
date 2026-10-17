#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QString>

// An invoice as the views see it. Qt Protobuf's int64 fields are not
// accessible from QML, so the id and the total travel as doubles, which hold
// whole numbers exactly up to 2^53 (totals never exceed 10^12 cents, and ids
// grow one by one).
struct InvoiceRow {
    Q_GADGET
    QML_VALUE_TYPE(invoiceRow)
    Q_PROPERTY(double id MEMBER id FINAL)
    Q_PROPERTY(QString customer MEMBER customer FINAL)
    Q_PROPERTY(bool hasCustomerTaxId MEMBER hasCustomerTaxId FINAL)
    Q_PROPERTY(QString customerTaxId MEMBER customerTaxId FINAL)
    Q_PROPERTY(double totalCents MEMBER totalCents FINAL)

public:
    double id = 0;
    QString customer;
    bool hasCustomerTaxId = false;
    QString customerTaxId;
    double totalCents = 0;
};
