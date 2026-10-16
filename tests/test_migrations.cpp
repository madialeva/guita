#include <backend/core/Migrations.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <memory>

// Numbered migrations applied at startup: from an empty database, idempotent,
// and with the constraints that back the business rules.
class MigrationsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("migrations");
        m_connection = std::make_unique<pqxx::connection>(m_db->connectionString());
    }

    void createsTheSchemaFromScratch() {
        applyMigrations(*m_connection);

        pqxx::nontransaction tx(*m_connection);
        QCOMPARE(tx.query_value<int>("SELECT count(*) FROM schema_version"), 1);
        QCOMPARE(tx.query_value<int>("SELECT max(version) FROM schema_version"), 1);
        QCOMPARE(tx.query_value<std::string>(
            "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
            "WHERE attrelid = 'invoices'::regclass AND attname = 'total'"), std::string("numeric(12,2)"));
        QCOMPARE(tx.query_value<bool>(
            "SELECT attnotnull FROM pg_attribute "
            "WHERE attrelid = 'invoices'::regclass AND attname = 'customer_tax_id'"), false);
    }

    void secondRunChangesNothing() {
        applyMigrations(*m_connection);

        pqxx::nontransaction tx(*m_connection);
        QCOMPARE(tx.query_value<int>("SELECT count(*) FROM schema_version"), 1);
    }

    void rejectsNegativeTotals() {
        pqxx::work tx(*m_connection);
        QVERIFY_THROWS_EXCEPTION(pqxx::check_violation,
            tx.exec("INSERT INTO invoices (version, customer, total) VALUES (0, 'Acme', -0.01)"));
    }

    void cleanupTestCase() {
        m_connection.reset();
        m_db.reset();
    }

private:
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<pqxx::connection> m_connection;
};

QTEST_GUILESS_MAIN(MigrationsTest)
#include "test_migrations.moc"
