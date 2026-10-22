#include <backend/core/Database.h>
#include <backend/core/InvoiceService.h>
#include <backend/core/Migrations.h>
#include <backend/core/Statements.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Idempotent creation of invoices with a request ID, through InvoiceService
// against PostgreSQL: a repeated request, in series or at the same time, gets
// the invoice of the first one, creates no other and notifies no creation.
class IdempotencyTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_testDb = std::make_unique<TestDatabase>("idempotency");
        pqxx::connection connection(m_testDb->connectionString());
        applyMigrations(connection);
        m_db = std::make_unique<Database>(m_testDb->connectionString(), kThreads, prepareStatements);
        m_db->warmUp();
        m_service = std::make_unique<InvoiceService>(*m_db);
    }

    void init() {
        m_db->inTransaction([](pqxx::work &tx) { tx.exec("TRUNCATE invoices RESTART IDENTITY"); });
    }

    void repeatedCreationReturnsTheSameInvoice() {
        const auto first = m_service->create({"Acme", std::string("B12345678"), 121000, kKey});
        const auto second = m_service->create({"Acme", std::string("B12345678"), 121000, kKey});
        QCOMPARE(second.id, first.id);
        QCOMPARE(second.customer, first.customer);
        QCOMPARE(second.customerTaxId, first.customerTaxId);
        QCOMPARE(second.totalCents, first.totalCents);
        QCOMPARE(count(), 1);
    }

    void simultaneousCreationsCreateOne() {
        std::vector<long long> ids(kThreads, 0);
        std::vector<std::string> errors(kThreads);
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                try {
                    ids[i] = m_service->create({"Acme", std::nullopt, 500, kKey}).id;
                } catch (const std::exception &e) {
                    errors[i] = e.what();
                }
            });
        }
        for (auto &thread : threads) thread.join();

        for (int i = 0; i < kThreads; ++i) {
            QVERIFY2(errors[i].empty(), errors[i].c_str());
            QCOMPARE(ids[i], ids[0]);
        }
        QCOMPARE(count(), 1);
    }

    void rejectsTheKeyWithOtherData() {
        m_service->create({"Acme", std::nullopt, 100, kKey});
        try {
            m_service->create({"Acme", std::nullopt, 200, kKey});
            QFAIL("create() accepted a reused key with other data");
        } catch (const std::invalid_argument &e) {
            QCOMPARE(QString::fromUtf8(e.what()), QStringLiteral("request_id was already used for a different invoice"));
        }
        QCOMPARE(count(), 1);
    }

    void comparesTheNormalizedTaxId() {
        // An empty tax ID is stored as absent, so both requests are the same
        const auto first = m_service->create({"Acme", std::string(), 100, kKey});
        const auto second = m_service->create({"Acme", std::nullopt, 100, kKey});
        QCOMPARE(second.id, first.id);
    }

    void takesTheKeyInAnyCase() {
        const auto first = m_service->create({"Acme", std::nullopt, 100, kKey});
        const auto second = m_service->create({"Acme", std::nullopt, 100, std::string("123E4567-E89B-12D3-A456-426614174000")});
        QCOMPARE(second.id, first.id);
        QCOMPARE(count(), 1);
    }

    void createsEveryInvoiceWithoutKey() {
        const auto first = m_service->create({"Acme", std::nullopt, 100});
        const auto second = m_service->create({"Acme", std::nullopt, 100});
        QVERIFY(second.id != first.id);
        QCOMPARE(count(), 2);
    }

    void notifiesOnlyNewInvoicesOnceCommitted() {
        std::vector<long long> notified;
        int visible = 0;
        pqxx::connection other(m_testDb->connectionString());
        InvoiceService service(*m_db, [&](const Invoice &invoice) {
            notified.push_back(invoice.id);
            // Already committed: another connection sees it
            pqxx::nontransaction tx(other);
            visible += tx.query_value<int>("SELECT count(*) FROM invoices WHERE id = $1", pqxx::params{invoice.id});
        });

        const auto first = service.create({"Acme", std::nullopt, 100, kKey});
        service.create({"Acme", std::nullopt, 100, kKey});
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.create({"Acme", std::nullopt, 200, kKey}));
        const auto unkeyed = service.create({"Acme", std::nullopt, 100});

        QCOMPARE(notified, (std::vector<long long>{first.id, unkeyed.id}));
        QCOMPARE(visible, 2);
    }

    void cleanupTestCase() {
        m_service.reset();
        m_db.reset();
        m_testDb.reset();
    }

private:
    static constexpr int kThreads = 8;
    static inline const std::string kKey = "123e4567-e89b-12d3-a456-426614174000";

    int count() {
        return m_db->inTransaction([](pqxx::work &tx) { return tx.query_value<int>("SELECT count(*) FROM invoices"); });
    }

    std::unique_ptr<TestDatabase> m_testDb;
    std::unique_ptr<Database> m_db;
    std::unique_ptr<InvoiceService> m_service;
};

QTEST_GUILESS_MAIN(IdempotencyTest)
#include "test_idempotency.moc"
