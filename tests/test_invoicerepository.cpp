#include <backend/api/Backend.h>
#include <client/ApiConnection.h>
#include <client/HttpTransport.h>
#include <client/InProcTransport.h>
#include <client/InvoiceRepository.h>
#include <support/Sockets.h>
#include <support/TestDatabase.h>
#include <QSignalSpy>
#include <QtTest>
#include <memory>

// The client's access to the API (ApiConnection + InvoiceRepository) with both
// transports: HTTP against a backend with HTTP and a token, and in process
// against a backend without HTTP, as in standalone mode. Each transport gets a
// database of its own, so both runs see the same data and must give the same
// results.
class InvoiceRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_httpDb = std::make_unique<TestDatabase>("invoicerepository_http");
        m_httpBackend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_httpDb->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_httpBackend->start());

        const int listening = listeningTcpSockets();
        m_inProcDb = std::make_unique<TestDatabase>("invoicerepository_inproc");
        m_inProcBackend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_inProcDb->connectionString(),
            .authorizer = tokenAuthorizer(""),
        });
        // The standalone backend adds no listening socket
        QCOMPARE(listeningTcpSockets(), listening);

        m_http.reset(connect(ApiConnection::Connected, std::make_shared<HttpTransport>(apiUrl(), kToken)));
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

    void reportsAWrongToken() {
        std::unique_ptr<ApiConnection> wrong(
            connect(ApiConnection::Connected, std::make_shared<HttpTransport>(apiUrl(), "wrong-token")));
        InvoiceRepository repository;
        repository.setConnection(wrong.get());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.list();
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Unauthorized"));
    }

    void reportsAnUnreachableBackend() {
        // Port 1 on loopback: nothing listens there
        std::unique_ptr<ApiConnection> unreachable(connect(
            ApiConnection::Connected, std::make_shared<HttpTransport>(QUrl("http://127.0.0.1:1/api/"), kToken)));
        InvoiceRepository repository;
        repository.setConnection(unreachable.get());
        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.list();
        QVERIFY(failed.wait());
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    void dropsResultsForDestroyedRepositories() {
        // A repository destroyed before its in-process call ends gets no callback
        auto repository = std::make_unique<InvoiceRepository>();
        repository->setConnection(m_inProc.get());
        repository->list();
        repository.reset();

        InvoiceRepository other;
        other.setConnection(m_inProc.get());
        QSignalSpy listed(&other, &InvoiceRepository::listed);
        other.list();
        QVERIFY(listed.wait());
    }

    void cleanupTestCase() {
        m_http.reset();
        m_inProc.reset();           // waits for in-process calls before the backend goes
        m_inProcBackend.reset();
        m_httpBackend.reset();
        m_inProcDb.reset();
        m_httpDb.reset();
    }

private:
    static constexpr const char *kToken = "client-test-token";

    QUrl apiUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(m_httpBackend->port())); }

    // ApiConnection is created like the QML engine does: configure() + create()
    static ApiConnection *connect(ApiConnection::Mode mode, std::shared_ptr<ApiTransport> transport) {
        ApiConnection::configure({mode, {}, std::move(transport)});
        return ApiConnection::create(nullptr, nullptr);
    }

    static void transports() {
        QTest::addColumn<bool>("inProcess");
        QTest::newRow("http") << false;
        QTest::newRow("in process") << true;
    }

    ApiConnection *connection() const {
        QFETCH(bool, inProcess);
        return inProcess ? m_inProc.get() : m_http.get();
    }

    std::unique_ptr<TestDatabase> m_httpDb, m_inProcDb;
    std::unique_ptr<Backend> m_httpBackend, m_inProcBackend;
    std::unique_ptr<ApiConnection> m_http, m_inProc;
};

QTEST_GUILESS_MAIN(InvoiceRepositoryTest)
#include "test_invoicerepository.moc"
