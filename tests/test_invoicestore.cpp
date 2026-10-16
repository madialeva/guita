#include <backend/core/Database.h>
#include <backend/core/InvoiceStore.h>
#include <backend/core/Migrations.h>
#include <backend/core/Statements.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <memory>

// SQL of invoices: exact money conversion between cents and numeric(12,2),
// nullable tax ID and listing order.
class InvoiceStoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_testDb = std::make_unique<TestDatabase>("invoicestore");
        pqxx::connection connection(m_testDb->connectionString());
        applyMigrations(connection);
        m_db = std::make_unique<Database>(m_testDb->connectionString(), 2, prepareStatements);
    }

    void init() {
        m_db->inTransaction([](pqxx::work &tx) { tx.exec("TRUNCATE invoices RESTART IDENTITY"); });
    }

    void keepsCentsExact_data() {
        QTest::addColumn<qint64>("cents");
        QTest::addColumn<QString>("numeric");

        QTest::newRow("zero") << qint64(0) << "0.00";
        QTest::newRow("five cents") << qint64(5) << "0.05";
        QTest::newRow("with decimals") << qint64(123450) << "1234.50";
        QTest::newRow("maximum") << qint64(999'999'999'999) << "9999999999.99";
    }

    void keepsCentsExact() {
        QFETCH(qint64, cents);
        QFETCH(QString, numeric);

        const auto saved = insert({"Acme", std::nullopt, cents});
        QCOMPARE(saved.totalCents, cents);

        const auto stored = m_db->inTransaction([&](pqxx::work &tx) {
            return tx.query_value<std::string>("SELECT total::text FROM invoices WHERE id = $1",
                                               pqxx::params{saved.id});
        });
        QCOMPARE(QString::fromStdString(stored), numeric);

        const auto listed = list();
        QCOMPARE(listed.size(), size_t(1));
        QCOMPARE(listed.front().totalCents, cents);
    }

    void storesAMissingTaxIdAsNull() {
        const auto saved = insert({"Acme", std::nullopt, 100});
        QVERIFY(!saved.customerTaxId);

        const bool isNull = m_db->inTransaction([&](pqxx::work &tx) {
            return tx.query_value<bool>("SELECT customer_tax_id IS NULL FROM invoices WHERE id = $1",
                                        pqxx::params{saved.id});
        });
        QVERIFY(isNull);
    }

    void listsInCreationOrder() {
        insert({"First", std::string("B11111111"), 100});
        insert({"Second", std::nullopt, 200});
        insert({"Third", std::string("B33333333"), 300});

        const auto invoices = list();
        QCOMPARE(invoices.size(), size_t(3));
        QCOMPARE(invoices[0].customer, std::string("First"));
        QCOMPARE(invoices[0].customerTaxId, std::optional<std::string>("B11111111"));
        QCOMPARE(invoices[1].customer, std::string("Second"));
        QVERIFY(!invoices[1].customerTaxId);
        QCOMPARE(invoices[2].customer, std::string("Third"));
        QVERIFY(invoices[0].id < invoices[1].id && invoices[1].id < invoices[2].id);
        for (const auto &invoice : invoices)
            QCOMPARE(invoice.version, 0);
    }

    void cleanupTestCase() {
        m_db.reset();
        m_testDb.reset();
    }

private:
    Invoice insert(const NewInvoice &invoice) {
        return m_db->inTransaction([&](pqxx::work &tx) { return InvoiceStore(tx).insert(invoice); });
    }

    std::vector<Invoice> list() {
        return m_db->inTransaction([](pqxx::work &tx) { return InvoiceStore(tx).list(); });
    }

    std::unique_ptr<TestDatabase> m_testDb;
    std::unique_ptr<Database> m_db;
};

QTEST_GUILESS_MAIN(InvoiceStoreTest)
#include "test_invoicestore.moc"
