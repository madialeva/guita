#include <backend/api/Backend.h>
#include <client/ApiConnection.h>
#include <client/InvoiceRepository.h>
#include <client/WebSocketTransport.h>
#include <guita/v1/invoice_service.pb.h>
#include <support/CuttingProxy.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QSignalSpy>
#include <QtTest>
#include <chrono>
#include <memory>
#include <string>

namespace v1 = guita::v1;
using namespace std::chrono_literals;

// WebSocketTransport when the connection is lost, with CuttingProxy between it
// and a Backend: a creation with a request ID whose reply is lost is sent
// again and applied once, a call that is not safe to repeat fails, the
// subscription is opened again and starts over, the waits between attempts
// grow up to their limit, and a call fails in time when the server stays
// unreachable. Short waits, so the suite is quick.
class ReconnectionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("reconnection");
        m_backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_backend->start());
    }

    void init() {
        m_proxy = std::make_unique<CuttingProxy>(quint16(m_backend->port()));
        m_transport = std::make_shared<WebSocketTransport>(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/api/channel").arg(m_proxy->port())), kToken, kOptions);
        ApiConnection::configure({ApiConnection::Connected, {}, m_transport});
        m_connection.reset(ApiConnection::create(nullptr, nullptr));
    }

    void cleanup() {
        m_connection.reset();
        m_transport.reset();
        m_proxy.reset();
    }

    void worksThroughTheProxy() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
        QCOMPARE(m_proxy->connectionTimes().size(), 1);
    }

    void sendsAKeyedCreationAgainAndAppliesItOnce() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        QSignalSpy created(&repository, &InvoiceRepository::created);
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        open(repository);

        // The server commits the invoice; its reply is lost with the connection
        m_proxy->holdDownstream(true);
        repository.create(QStringLiteral("Cut once"), QString(), 700);
        QVERIFY(QTest::qWaitFor([&] { return count("Cut once") == 1; }, 5000));
        m_proxy->cut();
        m_proxy->holdDownstream(false);

        QVERIFY(created.wait(5000));
        QVERIFY(failed.isEmpty());
        QCOMPARE(created.first().first().value<InvoiceRow>().id, double(idOf("Cut once")));
        QCOMPARE(count("Cut once"), 1);
        QCOMPARE(m_proxy->connectionTimes().size(), 2);
    }

    void failsACallInFlightThatIsNotSafeToRepeat() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        open(repository);

        v1::CreateInvoiceRequest request;           // no request ID
        request.set_customer("Not repeated");
        request.set_total_cents(1);
        QString error;
        bool answered = false;
        m_proxy->holdDownstream(true);
        m_connection->call<v1::CreateInvoiceResponse>("guita.v1.InvoiceService/CreateInvoice", request, this,
            [&](const v1::CreateInvoiceResponse &) { answered = true; },
            [&](const QString &message) { error = message; });
        QVERIFY(QTest::qWaitFor([&] { return count("Not repeated") == 1; }, 5000));
        m_proxy->cut();
        m_proxy->holdDownstream(false);

        QTRY_COMPARE_WITH_TIMEOUT(error, QStringLiteral("Connection to the backend lost"), 5000);
        QVERIFY(!answered);
        // Reconnected for a later call, and the creation was not sent again
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait(5000));
        QCOMPARE(count("Not repeated"), 1);
    }

    void opensTheSubscriptionAgain() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        QSignalSpy watching(&repository, &InvoiceRepository::watching);
        QSignalSpy invoiceCreated(&repository, &InvoiceRepository::invoiceCreated);
        QSignalSpy availableChanged(m_connection.get(), &ApiConnection::availableChanged);
        repository.watch();
        QVERIFY(watching.wait());

        // Cut off while another client creates an invoice
        m_proxy->setRefusing(true);
        m_proxy->cut();
        QTRY_VERIFY_WITH_TIMEOUT(!m_connection->available(), 5000);
        create("Created while cut off");
        m_proxy->setRefusing(false);

        // Back: started again, so the view reloads and finds the invoice it missed
        QTRY_COMPARE_WITH_TIMEOUT(watching.size(), 2, 5000);
        QVERIFY(m_connection->available());
        QCOMPARE(availableChanged.size(), 2);
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
        bool found = false;
        for (const auto &invoice : listed.first().first().value<QList<InvoiceRow>>())
            found = found || invoice.customer == QStringLiteral("Created while cut off");
        QVERIFY(found);
        QVERIFY(invoiceCreated.isEmpty());

        // And the events after it arrive
        create("Created after");
        QVERIFY(invoiceCreated.wait());
        QCOMPARE(invoiceCreated.first().first().value<InvoiceRow>().customer, QStringLiteral("Created after"));
    }

    void waitsLongerEachTimeUpToALimit() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        QSignalSpy watching(&repository, &InvoiceRepository::watching);
        repository.watch();
        QVERIFY(watching.wait());

        m_proxy->setRefusing(true);
        m_proxy->cut();
        QTRY_VERIFY_WITH_TIMEOUT(m_proxy->connectionTimes().size() >= 7, 10000);
        m_proxy->setRefusing(false);

        // The first attempt comes 100 ms (±20 %) after the cut, and each refusal
        // doubles the wait up to 400 ms: 200, 400, 400, 400 ms between attempts
        const auto &times = m_proxy->connectionTimes();
        const qint64 expected[] = {200, 400, 400, 400};
        for (int i = 0; i < 4; ++i) {
            const auto interval = times[i + 2] - times[i + 1];
            QVERIFY2(interval >= expected[i] * 8 / 10 - 20 && interval <= expected[i] * 12 / 10 + 150,
                     qPrintable(QStringLiteral("attempt %1 waited %2 ms").arg(i + 2).arg(interval)));
        }
        QTRY_COMPARE_WITH_TIMEOUT(watching.size(), 2, 5000);
    }

    void failsInTimeWhenTheServerStaysUnreachable() {
        InvoiceRepository repository;
        repository.setConnection(m_connection.get());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        m_proxy->setRefusing(true);

        QElapsedTimer timer;
        timer.start();
        repository.list();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Backend unavailable"));
        QVERIFY(timer.elapsed() >= 1500);         // callTimeout, trying meanwhile
        QVERIFY(m_proxy->connectionTimes().size() > 1);
        QVERIFY(!m_connection->available());
    }

    void cleanupTestCase() {
        m_backend.reset();
        m_db.reset();
    }

private:
    static constexpr const char *kToken = "reconnection-token";
    static constexpr WebSocketTransport::Options kOptions{
        .callTimeout = 2s, .initialBackoff = 100ms, .maxBackoff = 400ms};

    // Opens the channel with a first call
    void open(InvoiceRepository &repository) {
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
    }

    // Another client creates an invoice, straight on the backend
    void create(const std::string &customer) {
        v1::CreateInvoiceRequest request;
        request.set_customer(customer);
        request.set_total_cents(1);
        const auto result = m_backend->call({"guita.v1.InvoiceService/CreateInvoice", request.SerializeAsString(),
                                             std::string("Bearer ") + kToken});
        QCOMPARE(result.code, RpcCode::Ok);
    }

    int count(const std::string &customer) {
        pqxx::connection connection(m_db->connectionString());
        pqxx::nontransaction tx(connection);
        return tx.query_value<int>("SELECT count(*) FROM invoices WHERE customer = $1", pqxx::params{customer});
    }

    long long idOf(const std::string &customer) {
        pqxx::connection connection(m_db->connectionString());
        pqxx::nontransaction tx(connection);
        return tx.query_value<long long>("SELECT id FROM invoices WHERE customer = $1", pqxx::params{customer});
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Backend> m_backend;
    std::unique_ptr<CuttingProxy> m_proxy;
    std::shared_ptr<WebSocketTransport> m_transport;
    std::unique_ptr<ApiConnection> m_connection;
};

QTEST_GUILESS_MAIN(ReconnectionTest)
#include "test_reconnection.moc"
