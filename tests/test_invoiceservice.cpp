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
            QCOMPARE(QString::fromUtf8(e.what()), error + QStringLiteral(" (deliberately broken)"));
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
