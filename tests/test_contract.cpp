#include <guita/v1/invoice_service.pb.h>
#include <guita/v1/invoice_service.qpb.h>
#include <QProtobufSerializer>
#include <QtTest>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

// The two generators of the contract (protoc for the backend, Qt Protobuf for
// the client) must write and read exactly the same bytes.
class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void invoiceFromBackendToClient_data() { invoices(); }
    void invoiceFromBackendToClient() {
        QFETCH(qint64, id);
        QFETCH(QString, customer);
        QFETCH(std::optional<QString>, taxId);
        QFETCH(qint64, totalCents);

        guita::v1::Invoice backend;
        backend.set_id(id);
        backend.set_customer(customer.toStdString());
        if (taxId) backend.set_customer_tax_id(taxId->toStdString());
        backend.set_total_cents(totalCents);

        qtc::guita::v1::Invoice client;
        QVERIFY(client.deserialize(&m_serializer, QByteArray::fromStdString(backend.SerializeAsString())));
        QCOMPARE(client.id_proto(), id);
        QCOMPARE(client.customer(), customer);
        QCOMPARE(client.hasCustomerTaxId(), taxId.has_value());
        if (taxId) QCOMPARE(client.customerTaxId(), *taxId);
        QCOMPARE(client.totalCents(), totalCents);
    }

    void invoiceFromClientToBackend_data() { invoices(); }
    void invoiceFromClientToBackend() {
        QFETCH(qint64, id);
        QFETCH(QString, customer);
        QFETCH(std::optional<QString>, taxId);
        QFETCH(qint64, totalCents);

        qtc::guita::v1::Invoice client;
        client.setId_proto(id);
        client.setCustomer(customer);
        if (taxId) client.setCustomerTaxId(*taxId);
        client.setTotalCents(totalCents);

        guita::v1::Invoice backend;
        QVERIFY(backend.ParseFromString(client.serialize(&m_serializer).toStdString()));
        QCOMPARE(backend.id(), id);
        QCOMPARE(QString::fromStdString(backend.customer()), customer);
        QCOMPARE(backend.has_customer_tax_id(), taxId.has_value());
        if (taxId) QCOMPARE(QString::fromStdString(backend.customer_tax_id()), *taxId);
        QCOMPARE(backend.total_cents(), totalCents);
    }

    void requestFromClientToBackend() {
        qtc::guita::v1::CreateInvoiceRequest client;
        client.setCustomer(QStringLiteral("Acme S.L."));
        client.setTotalCents(999'999'999'999);

        guita::v1::CreateInvoiceRequest backend;
        QVERIFY(backend.ParseFromString(client.serialize(&m_serializer).toStdString()));
        QCOMPARE(backend.customer(), std::string("Acme S.L."));
        QVERIFY(!backend.has_customer_tax_id());
        QCOMPARE(backend.total_cents(), 999'999'999'999);
    }

    void listFromBackendToClient() {
        guita::v1::ListInvoicesResponse backend;
        for (int i = 1; i <= 1000; ++i) {
            auto *invoice = backend.add_invoices();
            invoice->set_id(i);
            invoice->set_customer("Customer " + std::to_string(i));
            if (i % 2) invoice->set_customer_tax_id("B" + std::to_string(i));
            invoice->set_total_cents(i * 100 + 5);
        }

        qtc::guita::v1::ListInvoicesResponse client;
        QVERIFY(client.deserialize(&m_serializer, QByteArray::fromStdString(backend.SerializeAsString())));
        QCOMPARE(client.invoices().size(), 1000);
        for (int i = 1; i <= 1000; ++i) {
            const auto &invoice = client.invoices().at(i - 1);
            QCOMPARE(invoice.id_proto(), i);
            QCOMPARE(invoice.customer(), QStringLiteral("Customer %1").arg(i));
            QCOMPARE(invoice.hasCustomerTaxId(), bool(i % 2));
            QCOMPARE(invoice.totalCents(), i * 100 + 5);
        }
    }

    void emptyListBothWays() {
        qtc::guita::v1::ListInvoicesResponse client;
        guita::v1::ListInvoicesResponse backend;
        QVERIFY(backend.ParseFromString(client.serialize(&m_serializer).toStdString()));
        QCOMPARE(backend.invoices_size(), 0);
        QVERIFY(client.deserialize(&m_serializer, QByteArray::fromStdString(backend.SerializeAsString())));
        QCOMPARE(client.invoices().size(), 0);
    }

private:
    static void invoices() {
        QTest::addColumn<qint64>("id");
        QTest::addColumn<QString>("customer");
        QTest::addColumn<std::optional<QString>>("taxId");
        QTest::addColumn<qint64>("totalCents");

        constexpr auto max = std::numeric_limits<std::int64_t>::max();
        constexpr auto min = std::numeric_limits<std::int64_t>::min();
        QTest::newRow("tax ID") << qint64(1) << "Acme S.L." << std::optional<QString>("B12345678") << qint64(121000);
        QTest::newRow("no tax ID") << qint64(2) << "Ñandú & Cía" << std::optional<QString>() << qint64(5);
        QTest::newRow("empty tax ID is present") << qint64(3) << "Empty" << std::optional<QString>("") << qint64(0);
        QTest::newRow("int64 maximum") << qint64(max) << "Max" << std::optional<QString>() << qint64(max);
        QTest::newRow("int64 minimum") << qint64(min) << "Min" << std::optional<QString>() << qint64(min);
    }

    QProtobufSerializer m_serializer;
};

QTEST_GUILESS_MAIN(ContractTest)
#include "test_contract.moc"
