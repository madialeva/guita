#include <backend/api/Backend.h>
#include <client/ApiConnection.h>
#include <client/InvoiceRepository.h>
#include <support/TestDatabase.h>
#include <QSignalSpy>
#include <QtTest>
#include <memory>

// The client's API access (ApiConnection + InvoiceRepository) against an
// embedded backend with a per-run token, exactly as in standalone mode.
class InvoiceRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("invoicerepository");
        m_backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_backend->start());
        m_connection.reset(connect(m_backend->port(), kToken));
        m_repository.setConnection(m_connection.get());
    }

    void listsNoInvoicesAtFirst() {
        QSignalSpy listed(&m_repository, &InvoiceRepository::listed);
        m_repository.list();
        QVERIFY(listed.wait());
        QCOMPARE(listed.first().first().toList().size(), 0);
    }

    void createsAndListsAnInvoice() {
        QSignalSpy created(&m_repository, &InvoiceRepository::created);
        m_repository.create({{"customer", "Acme S.L."}, {"customerTaxId", "B12345678"},
                             {"totalCents", qint64(123450)}});
        QVERIFY(created.wait());
        const auto invoice = created.first().first().toMap();
        QVERIFY(invoice.value("id").toLongLong() > 0);
        QCOMPARE(invoice.value("totalCents").toLongLong(), 123450);

        QSignalSpy listed(&m_repository, &InvoiceRepository::listed);
        m_repository.list();
        QVERIFY(listed.wait());
        const auto invoices = listed.first().first().toList();
        QCOMPARE(invoices.size(), 1);
        QCOMPARE(invoices.first().toMap().value("customer").toString(), QStringLiteral("Acme S.L."));
    }

    void reportsTheBackendValidationMessage() {
        QSignalSpy failed(&m_repository, &InvoiceRepository::failed);
        m_repository.create({{"customer", ""}, {"totalCents", 100}});
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Customer is required"));
    }

    void reportsAWrongToken() {
        std::unique_ptr<ApiConnection> wrong(connect(m_backend->port(), "wrong-token"));
        InvoiceRepository repository;
        repository.setConnection(wrong.get());

        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.list();
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Unauthorized"));
    }

    void reportsAnUnreachableBackend() {
        // Port 1 on loopback: nothing listens there
        std::unique_ptr<ApiConnection> unreachable(connect(1, kToken));
        InvoiceRepository repository;
        repository.setConnection(unreachable.get());

        QSignalSpy failed(&repository, &InvoiceRepository::failed);
        repository.list();
        QVERIFY(failed.wait());
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    void cleanupTestCase() {
        m_repository.setConnection(nullptr);
        m_connection.reset();
        m_backend.reset();
        m_db.reset();
    }

private:
    static constexpr const char *kToken = "client-test-token";

    // ApiConnection is created like the QML engine does: configure() + create()
    static ApiConnection *connect(int port, const QByteArray &token) {
        ApiConnection::configure({QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(port)),
                                  token, ApiConnection::Standalone});
        return ApiConnection::create(nullptr, nullptr);
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Backend> m_backend;
    std::unique_ptr<ApiConnection> m_connection;
    InvoiceRepository m_repository;
};

QTEST_GUILESS_MAIN(InvoiceRepositoryTest)
#include "test_invoicerepository.moc"
