#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <optional>

// The isolation of the database suites rests on TestDatabase: each suite gets
// a new, empty database that disappears afterwards.
class TestDatabaseTest : public QObject {
    Q_OBJECT

private slots:
    void createsAnEmptyDatabaseAndDropsIt() {
        std::string name;
        {
            TestDatabase db("testdatabase");
            name = db.name();
            QVERIFY(name.starts_with("guita_test_testdatabase_"));

            pqxx::connection connection(db.connectionString());
            pqxx::nontransaction tx(connection);
            QCOMPARE(tx.query_value<std::string>("SELECT current_database()"), name);
            QCOMPARE(tx.query_value<int>(
                "SELECT count(*) FROM information_schema.tables WHERE table_schema = 'public'"), 0);
        }
        QCOMPARE(databaseExists(name), false);
    }

    void eachInstanceIsIsolated() {
        TestDatabase first("testdatabase");
        TestDatabase second("testdatabase");
        QVERIFY(first.name() != second.name());
    }

private:
    static bool databaseExists(const std::string &name) {
        pqxx::connection server(TestDatabase::serverConnectionString());
        pqxx::nontransaction tx(server);
        return tx.query_value<int>("SELECT count(*) FROM pg_database WHERE datname = $1",
                                   pqxx::params{name}) > 0;
    }
};

QTEST_GUILESS_MAIN(TestDatabaseTest)
#include "test_testdatabase.moc"
