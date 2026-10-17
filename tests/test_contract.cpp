#include <guita/v1/invoice_service.pb.h>
#include <support/ProtoSamples.h>
#include <QtTest>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

// The contract's code, the same on every side: it writes the reference bytes
// of tests/support/ProtoSamples.h (worked out by hand) and reads them back,
// keeps optional fields apart (absent, empty, present), holds int64 at its
// limits, and skips but keeps the fields of a later version.
namespace v1 = guita::v1;
using namespace protoSamples;

namespace {
QByteArray qb(std::string_view bytes) { return QByteArray(bytes.data(), qsizetype(bytes.size())); }

v1::CreateInvoiceRequest create(std::optional<std::string> taxId, std::int64_t cents = kTotalCents) {
    v1::CreateInvoiceRequest request;
    request.set_customer(std::string(kCustomer));
    if (taxId) request.set_customer_tax_id(*taxId);
    request.set_total_cents(cents);
    return request;
}

void fill(v1::Invoice &invoice, std::int64_t id, std::optional<std::string> taxId, std::int64_t cents) {
    invoice.set_id(id);
    invoice.set_customer(std::string(kCustomer));
    if (taxId) invoice.set_customer_tax_id(*taxId);
    invoice.set_total_cents(cents);
}
}

class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void writesTheReferenceBytes() {
        QCOMPARE(qb(toHex(create(std::nullopt).SerializeAsString())), qb(kCreateInvoiceRequest));
        QCOMPARE(qb(toHex(create(std::string(kTaxId)).SerializeAsString())), qb(kCreateInvoiceRequestWithTaxId));
        QCOMPARE(qb(toHex(create(std::string()).SerializeAsString())), qb(kCreateInvoiceRequestEmptyTaxId));
        QCOMPARE(qb(toHex(create(std::nullopt, -1).SerializeAsString())), qb(kCreateInvoiceRequestNegative));

        v1::CreateInvoiceResponse created;
        fill(*created.mutable_invoice(), 42, std::string(kTaxId), kTotalCents);
        QCOMPARE(qb(toHex(created.SerializeAsString())), qb(kCreateInvoiceResponse));

        v1::ListInvoicesResponse listed;
        fill(*listed.add_invoices(), 1, std::nullopt, 0);
        fill(*listed.add_invoices(), std::numeric_limits<std::int64_t>::max(), std::string(kTaxId), kMaxCents);
        QCOMPARE(qb(toHex(listed.SerializeAsString())), qb(kListInvoicesResponseLimits));

        // An empty message is no bytes at all
        QVERIFY(v1::ListInvoicesRequest().SerializeAsString().empty());
    }

    void readsTheReferenceBytes() {
        v1::CreateInvoiceRequest absent, empty, present, negative;
        QVERIFY(absent.ParseFromString(fromHex(kCreateInvoiceRequest)));
        QVERIFY(empty.ParseFromString(fromHex(kCreateInvoiceRequestEmptyTaxId)));
        QVERIFY(present.ParseFromString(fromHex(kCreateInvoiceRequestWithTaxId)));
        QVERIFY(negative.ParseFromString(fromHex(kCreateInvoiceRequestNegative)));
        QCOMPARE(qb(absent.customer()), qb(kCustomer));
        QVERIFY(!absent.has_customer_tax_id());
        QVERIFY(empty.has_customer_tax_id());
        QVERIFY(empty.customer_tax_id().empty());
        QCOMPARE(qb(present.customer_tax_id()), qb(kTaxId));
        QCOMPARE(present.total_cents(), kTotalCents);
        QCOMPARE(negative.total_cents(), std::int64_t(-1));

        v1::ListInvoicesResponse listed;
        QVERIFY(listed.ParseFromString(fromHex(kListInvoicesResponseLimits)));
        QCOMPARE(listed.invoices_size(), 2);
        QCOMPARE(listed.invoices(0).id(), std::int64_t(1));
        QVERIFY(!listed.invoices(0).has_customer_tax_id());
        QCOMPARE(listed.invoices(0).total_cents(), std::int64_t(0));
        QCOMPARE(listed.invoices(1).id(), std::numeric_limits<std::int64_t>::max());
        QCOMPARE(listed.invoices(1).total_cents(), kMaxCents);
    }

    void roundTrip_data() {
        QTest::addColumn<qint64>("id");
        QTest::addColumn<std::optional<std::string>>("taxId");
        QTest::addColumn<qint64>("totalCents");
        QTest::newRow("smallest") << qint64(1) << std::optional<std::string>() << qint64(0);
        QTest::newRow("int64 limits") << std::numeric_limits<qint64>::max() << std::optional<std::string>("B12345678")
                                      << std::numeric_limits<qint64>::min();
        QTest::newRow("empty tax ID") << qint64(5) << std::optional<std::string>("") << qint64(5);
        QTest::newRow("non-ASCII text") << qint64(6) << std::optional<std::string>("ñ€") << qint64(kMaxCents);
    }
    void roundTrip() {
        QFETCH(qint64, id);
        QFETCH(std::optional<std::string>, taxId);
        QFETCH(qint64, totalCents);

        v1::ListInvoicesResponse listed;
        fill(*listed.add_invoices(), id, taxId, totalCents);
        v1::ListInvoicesResponse decoded;
        QVERIFY(decoded.ParseFromString(listed.SerializeAsString()));
        const auto &invoice = decoded.invoices(0);
        QCOMPARE(qint64(invoice.id()), id);
        QCOMPARE(invoice.has_customer_tax_id(), taxId.has_value());
        if (taxId) QCOMPARE(qb(invoice.customer_tax_id()), qb(*taxId));
        QCOMPARE(qint64(invoice.total_cents()), totalCents);
    }

    void skipsAndKeepsFieldsOfALaterVersion() {
        // An invoice of a later version: field 5 (a due date) and field 15 (a varint)
        const auto later = fromHex(kCreateInvoiceResponse.substr(4)) + std::string("\x2a\x0a" "2026-12-31", 12)
                         + std::string("\x78\x01", 2);
        v1::Invoice invoice;
        QVERIFY(invoice.ParseFromString(later));
        QCOMPARE(invoice.id(), std::int64_t(42));
        QCOMPARE(qb(invoice.customer()), qb(kCustomer));
        QCOMPARE(invoice.total_cents(), kTotalCents);
        // Written back unchanged, so a relay does not lose them
        QCOMPARE(qb(toHex(invoice.SerializeAsString())), qb(toHex(later)));
    }

    void rejectsMalformedBytes() {
        v1::CreateInvoiceRequest request;
        QVERIFY(!request.ParseFromString(fromHex(kCreateInvoiceRequest).substr(0, 5)));   // truncated string
        QVERIFY(!request.ParseFromString(std::string("\xff\xff\xff\xff", 4)));
    }
};

QTEST_GUILESS_MAIN(ContractTest)
#include "test_contract.moc"
