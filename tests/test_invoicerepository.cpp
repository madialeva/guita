#include <backend/api/Backend.h>
#include <client/ApiConnection.h>
#include <client/InProcTransport.h>
#include <client/InvoiceRepository.h>
#include <client/WebSocketTransport.h>
#include <guita/v1/invoice_service.pb.h>
#include <support/Sockets.h>
#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <QSignalSpy>
#include <QtTest>
#include <chrono>
#include <memory>

namespace v1 = guita::v1;
using namespace std::chrono_literals;

// The client's access to the API (ApiConnection + InvoiceRepository) with both
// transports: the persistent channel against a backend with HTTP and a token,
// and in process against a backend without HTTP, as in standalone mode. Each
// transport gets a database of its own, so both runs see the same data and
// must give the same results and events.
class InvoiceRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_channelDb = std::make_unique<TestDatabase>("invoicerepository_channel");
        m_channelBackend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_channelDb->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_channelBackend->start());

        const int listening = listeningTcpSockets();
        m_inProcDb = std::make_unique<TestDatabase>("invoicerepository_inproc");
        m_inProcBackend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_inProcDb->connectionString(),
            .authorizer = tokenAuthorizer(""),
        });
        // The standalone backend adds no listening socket
        QCOMPARE(listeningTcpSockets(), listening);

        m_channel.reset(connect(ApiConnection::Connected, std::make_shared<WebSocketTransport>(channelUrl(), kToken)));
        m_inProc.reset(connect(ApiConnection::Standalone, std::make_shared<InProcTransport>(*m_inProcBackend)));
    }

    void listsNoInvoicesAtFirst_data() { transports(); }
    void listsNoInvoicesAtFirst() {
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
        QCOMPARE(listed.first().first().value<QList<InvoiceRow>>().size(), 0);
    }

    void createsAndListsAnInvoice_data() { transports(); }
    void createsAndListsAnInvoice() {
        InvoiceRepository repository;
        repository.setConnection(connection());

        QSignalSpy created(&repository, &InvoiceRepository::created);
        repository.create(QStringLiteral("Acme S.L."), QStringLiteral("B12345678"), 999'999'999'999.0);
        QVERIFY(created.wait());
        const auto invoice = created.first().first().value<InvoiceRow>();
        QCOMPARE(invoice.id, 1.0);
        QCOMPARE(invoice.customer, QStringLiteral("Acme S.L."));
        QVERIFY(invoice.hasCustomerTaxId);
        QCOMPARE(invoice.customerTaxId, QStringLiteral("B12345678"));
        QCOMPARE(invoice.totalCents, 999'999'999'999.0);     // exact in a double

        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
        const auto invoices = listed.first().first().value<QList<InvoiceRow>>();
        QCOMPARE(invoices.size(), 1);
        QCOMPARE(invoices.first().customer, QStringLiteral("Acme S.L."));
    }

    void createsWithANewRequestIdEachTime_data() { transports(); }
    void createsWithANewRequestIdEachTime() {
        // The same data twice is two invoices: each create() has its own key
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy created(&repository, &InvoiceRepository::created);
        repository.create(QStringLiteral("Twice"), QString(), 300);
        repository.create(QStringLiteral("Twice"), QString(), 300);
        QVERIFY(QTest::qWaitFor([&] { return created.size() == 2; }, 5000));

        pqxx::connection db(database());
        pqxx::nontransaction tx(db);
        QCOMPARE(tx.query_value<int>("SELECT count(DISTINCT request_id) FROM invoices WHERE customer = 'Twice'"), 2);
    }

    void sendsAnEmptyTaxIdAsAbsent_data() { transports(); }
    void sendsAnEmptyTaxIdAsAbsent() {
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy created(&repository, &InvoiceRepository::created);
        repository.create(QStringLiteral("No tax ID"), QString(), 5);
        QVERIFY(created.wait());
        QVERIFY(!created.first().first().value<InvoiceRow>().hasCustomerTaxId);
    }

    void reportsTheBackendValidationMessage_data() { transports(); }
    void reportsTheBackendValidationMessage() {
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.create(QString(), QString(), 100);
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Customer is required"));
    }

    void receivesInvoicesCreatedByOthers_data() { transports(); }
    void receivesInvoicesCreatedByOthers() {
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy watching(&repository, &InvoiceRepository::watching);
        QSignalSpy invoiceCreated(&repository, &InvoiceRepository::invoiceCreated);
        repository.watch();
        QVERIFY(watching.wait());
        QCOMPARE(invoiceCreated.size(), 0);

        // Another client creates an invoice, straight on the backend
        QFETCH(bool, inProcess);
        v1::CreateInvoiceRequest request;
        request.set_customer("Someone else");
        request.set_total_cents(4200);
        const auto result = (inProcess ? m_inProcBackend : m_channelBackend)
                                ->call({"guita.v1.InvoiceService/CreateInvoice", request.SerializeAsString(),
                                        inProcess ? std::string() : std::string("Bearer ") + kToken});
        QCOMPARE(result.code, RpcCode::Ok);

        QVERIFY(invoiceCreated.wait());
        const auto invoice = invoiceCreated.first().first().value<InvoiceRow>();
        QCOMPARE(invoice.customer, QStringLiteral("Someone else"));
        QCOMPARE(invoice.totalCents, 4200.0);
        QCOMPARE(watching.size(), 1);

        // Watching twice changes nothing
        repository.watch();
        QTest::qWait(100);
        QCOMPARE(watching.size(), 1);
    }

    void isAvailable_data() { transports(); }
    void isAvailable() {
        InvoiceRepository repository;
        repository.setConnection(connection());
        QSignalSpy listed(&repository, &InvoiceRepository::listed);
        repository.list();
        QVERIFY(listed.wait());
        QVERIFY(connection()->available());
    }

    void reportsAWrongTokenWithoutReconnecting() {
        auto transport = std::make_shared<WebSocketTransport>(channelUrl(), "wrong-token");
        std::unique_ptr<ApiConnection> wrong(connect(ApiConnection::Connected, transport));
        InvoiceRepository repository;
        repository.setConnection(wrong.get());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.list();
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Unauthorized"));
        QTRY_VERIFY(!wrong->available());     // once the server closes

        // Longer than the first wait before reconnecting: nothing happens
        QTest::qWait(1000);
        QCOMPARE(transport->connectionAttempts(), 1);
        // The next call tries again
        repository.list();
        QVERIFY(failed.wait());
        QCOMPARE(transport->connectionAttempts(), 2);
    }

    void reportsAnUnreachableBackend() {
        // Port 1 on loopback: nothing listens there
        auto transport = std::make_shared<WebSocketTransport>(QUrl("ws://127.0.0.1:1/api/channel"), kToken,
            WebSocketTransport::Options{.callTimeout = 1s, .initialBackoff = 100ms, .maxBackoff = 200ms});
        std::unique_ptr<ApiConnection> unreachable(connect(ApiConnection::Connected, transport));
        InvoiceRepository repository;
        repository.setConnection(unreachable.get());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        QSignalSpy availableChanged(unreachable.get(), &ApiConnection::availableChanged);
        repository.list();
        QVERIFY(availableChanged.wait());
        QVERIFY(!unreachable->available());
        QVERIFY(failed.wait(3000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Backend unavailable"));
        // It kept trying meanwhile
        QVERIFY(transport->connectionAttempts() > 1);
    }

    void dropsResultsForDestroyedRepositories_data() { transports(); }
    void dropsResultsForDestroyedRepositories() {
        // A repository destroyed before its call ends, or while watching, gets no callback
        auto repository = std::make_unique<InvoiceRepository>();
        repository->setConnection(connection());
        repository->watch();
        repository->list();
        repository.reset();

        InvoiceRepository other;
        other.setConnection(connection());
        QSignalSpy listed(&other, &InvoiceRepository::listed);
        QSignalSpy created(&other, &InvoiceRepository::created);
        other.list();
        QVERIFY(listed.wait());
        other.create(QStringLiteral("After"), QString(), 1);
        QVERIFY(created.wait());
    }

    void cleanupTestCase() {
        m_channel.reset();
        m_inProc.reset();           // waits for in-process calls before the backend goes
        m_inProcBackend.reset();
        m_channelBackend.reset();
        m_inProcDb.reset();
        m_channelDb.reset();
    }

private:
    static constexpr const char *kToken = "client-test-token";

    QUrl channelUrl() const {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1/api/channel").arg(m_channelBackend->port()));
    }

    // ApiConnection is created like the QML engine does: configure() + create()
    static ApiConnection *connect(ApiConnection::Mode mode, std::shared_ptr<ApiTransport> transport) {
        ApiConnection::configure({mode, {}, std::move(transport)});
        return ApiConnection::create(nullptr, nullptr);
    }

    static void transports() {
        QTest::addColumn<bool>("inProcess");
        QTest::newRow("channel") << false;
        QTest::newRow("in process") << true;
    }

    ApiConnection *connection() const {
        QFETCH(bool, inProcess);
        return inProcess ? m_inProc.get() : m_channel.get();
    }

    std::string database() const {
        QFETCH(bool, inProcess);
        return inProcess ? m_inProcDb->connectionString() : m_channelDb->connectionString();
    }

    std::unique_ptr<TestDatabase> m_channelDb, m_inProcDb;
    std::unique_ptr<Backend> m_channelBackend, m_inProcBackend;
    std::unique_ptr<ApiConnection> m_channel, m_inProc;
};

QTEST_GUILESS_MAIN(InvoiceRepositoryTest)
#include "test_invoicerepository.moc"
