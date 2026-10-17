#include <backend/api/Backend.h>
#include <backend/core/Database.h>
#include <backend/core/Migrations.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

// Connection pool, transaction boundaries and the fail-fast check of the
// prepared statements at startup.
class DatabaseTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { m_testDb = std::make_unique<TestDatabase>("database"); }

    void poolServesMoreThreadsThanConnections() {
        constexpr std::size_t kPoolSize = 2;
        constexpr int kThreads = 16;
        Database db(m_testDb->connectionString(), kPoolSize, nullptr);

        std::atomic<int> done{0};
        std::atomic<int> maxConnections{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&] {
                db.inTransaction([&](pqxx::work &tx) {
                    tx.exec("SELECT pg_sleep(0.02)");
                    const int open = tx.query_value<int>(
                        "SELECT count(*) FROM pg_stat_activity WHERE datname = current_database()");
                    int seen = maxConnections;
                    while (open > seen && !maxConnections.compare_exchange_weak(seen, open)) {}
                });
                ++done;
            });
        }
        for (auto &thread : threads)
            thread.join();

        QCOMPARE(done.load(), kThreads);
        // Never more connections than the pool size, and the pool did use them all
        QCOMPARE(maxConnections.load(), int(kPoolSize));
    }

    void rollsBackWhenTheTransactionThrows() {
        Database db(m_testDb->connectionString(), 1, nullptr);
        db.inTransaction([](pqxx::work &tx) { tx.exec("CREATE TABLE rollback_probe (n integer)"); });

        QVERIFY_THROWS_EXCEPTION(std::runtime_error, db.inTransaction([](pqxx::work &tx) {
            tx.exec("INSERT INTO rollback_probe VALUES (1)");
            throw std::runtime_error("business failure");
        }));

        QCOMPARE(db.inTransaction([](pqxx::work &tx) {
            return tx.query_value<int>("SELECT count(*) FROM rollback_probe");
        }), 0);
    }

    void warmUpFailsWithAStatementThatDoesNotMatchTheSchema() {
        Database db(m_testDb->connectionString(), 1, [](pqxx::connection &connection) {
            connection.prepare("broken", "SELECT missing_column FROM missing_table");
        });
        QVERIFY_THROWS_EXCEPTION(pqxx::sql_error, db.warmUp());
    }

    void backendRefusesToStartWhenTheSchemaDoesNotMatch() {
        // Migration 1 recorded as applied, but its table is missing: the
        // statements of the stores cannot be prepared
        TestDatabase mismatched("database_mismatch");
        {
            pqxx::connection connection(mismatched.connectionString());
            pqxx::work tx(connection);
            tx.exec("CREATE TABLE schema_version (version integer PRIMARY KEY)");
            tx.exec("INSERT INTO schema_version VALUES (1)");
            tx.commit();
        }
        QVERIFY_THROWS_EXCEPTION(pqxx::sql_error, Backend(BackendOptions{
            .connectionString = mismatched.connectionString(),
            .authorizer = tokenAuthorizer("unused"),
            .http = HttpOptions{},
        }));
    }

    void cleanupTestCase() { m_testDb.reset(); }

private:
    std::unique_ptr<TestDatabase> m_testDb;
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "test_database.moc"
