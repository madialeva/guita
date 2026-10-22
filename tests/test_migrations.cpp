#include <backend/core/Migrations.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <memory>
#include <string>

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
        QCOMPARE(tx.query_value<std::string>(
            "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
            "WHERE attrelid = 'invoices'::regclass AND attname = 'request_id'"), std::string("uuid"));
        QCOMPARE(tx.query_value<bool>(
            "SELECT attnotnull FROM pg_attribute "
            "WHERE attrelid = 'invoices'::regclass AND attname = 'request_id'"), false);
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

    void keepsRequestIdsUnique() {
        {
            // Invoices without a key never conflict
            pqxx::work tx(*m_connection);
            tx.exec("INSERT INTO invoices (version, customer, total) VALUES (0, 'Acme', 1), (0, 'Acme', 1)");
            tx.exec("INSERT INTO invoices (version, customer, total, request_id) "
                    "VALUES (0, 'Acme', 1, '123e4567-e89b-12d3-a456-426614174000')");
            tx.commit();
        }
        pqxx::work tx(*m_connection);
        QVERIFY_THROWS_EXCEPTION(pqxx::unique_violation,
            tx.exec("INSERT INTO invoices (version, customer, total, request_id) "
                    "VALUES (0, 'Other', 2, '123E4567-E89B-12D3-A456-426614174000')"));
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
