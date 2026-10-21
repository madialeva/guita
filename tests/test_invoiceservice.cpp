#include <backend/core/Database.h>
#include <backend/core/InvoiceService.h>
#include <QtTest>
#include <stdexcept>

// Business validation of InvoiceService. It happens before any database
// access, so the Database points nowhere and is never connected.
class InvoiceServiceTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsInvalidInvoices_data() {
        QTest::addColumn<QString>("customer");
        QTest::addColumn<qint64>("totalCents");
        QTest::addColumn<QString>("error");

        QTest::newRow("no customer") << "" << qint64(100) << "Customer is required";
        QTest::newRow("negative total") << "Acme" << qint64(-1) << "Total must not be negative";
        QTest::newRow("above numeric(12,2)") << "Acme" << qint64(1'000'000'000'000) << "Total is too large";
    }

    void rejectsInvalidInvoices() {
        QFETCH(QString, customer);
        QFETCH(qint64, totalCents);
        QFETCH(QString, error);

        try {
            m_service.create({customer.toStdString(), std::nullopt, totalCents});
            QFAIL("create() accepted an invalid invoice");
        } catch (const std::invalid_argument &e) {
            QCOMPARE(QString::fromUtf8(e.what()), error);
        }
    }

    void rejectsMalformedRequestIds_data() {
        QTest::addColumn<QString>("requestId");
        QTest::newRow("empty") << "";
        QTest::newRow("not hexadecimal") << "123e4567-e89b-12d3-a456-42661417400g";
        QTest::newRow("without dashes") << "123e4567e89b12d3a456426614174000";
        QTest::newRow("braces") << "{123e4567-e89b-12d3-a456-426614174000}";
        QTest::newRow("too short") << "123e4567-e89b-12d3-a456-42661417400";
        QTest::newRow("dash misplaced") << "123e456-7e89b-12d3-a456-426614174000";
    }
    void rejectsMalformedRequestIds() {
        QFETCH(QString, requestId);
        try {
            m_service.create({"Acme", std::nullopt, 100, requestId.toStdString()});
            QFAIL("create() accepted a malformed request ID");
        } catch (const std::invalid_argument &e) {
            QCOMPARE(QString::fromUtf8(e.what()), QStringLiteral("request_id must be a UUID"));
        }
    }

    void acceptsWellFormedRequestIds_data() {
        QTest::addColumn<QString>("requestId");
        QTest::newRow("lower case") << "123e4567-e89b-12d3-a456-426614174000";
        QTest::newRow("upper case") << "123E4567-E89B-12D3-A456-426614174000";
    }
    void acceptsWellFormedRequestIds() {
        // Validation passes, so the call reaches the (unreachable) database
        QFETCH(QString, requestId);
        try {
            m_service.create({"Acme", std::nullopt, 100, requestId.toStdString()});
            QFAIL("create() reached no database");
        } catch (const std::invalid_argument &e) {
            QFAIL(e.what());
        } catch (const std::exception &) {
        }
    }

private:
    // Unreachable on purpose: a validation that touched the database would
    // fail with a connection error instead of std::invalid_argument
    Database m_db{"host=invalid.invalid connect_timeout=1", 1, nullptr};
    InvoiceService m_service{m_db};
};

QTEST_GUILESS_MAIN(InvoiceServiceTest)
#include "test_invoiceservice.moc"
