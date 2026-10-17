#include <backend/api/Backend.h>
#include <backend/api/HttpRpcTransport.h>
#include <guita/v1/invoice_service.pb.h>
#include <support/Sockets.h>
#include <support/TestDatabase.h>
#include <httplib.h>
#include <pqxx/pqxx>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace v1 = guita::v1;

// The RPC API over HTTP against a real Backend on a free port: binary
// Protobuf only, and the Connect errors, which are JSON and read with Qt,
// independently of the backend's encoder.
class RpcApiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("rpcapi");
        m_backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_backend->start());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_backend->port());
    }

    void listsNoInvoicesAtFirst() {
        const auto res = post(kList, "application/proto", v1::ListInvoicesRequest().SerializeAsString());
        QVERIFY(res);
        QCOMPARE(res->status, 200);
        QCOMPARE(res->get_header_value("Content-Type"), std::string("application/proto"));
        v1::ListInvoicesResponse response;
        QVERIFY(response.ParseFromString(res->body));
        QCOMPARE(response.invoices_size(), 0);
    }

    void createsAndListsInBinary() {
        v1::CreateInvoiceRequest request;
        request.set_customer("Acme S.L.");
        request.set_customer_tax_id("B12345678");
        request.set_total_cents(121000);

        auto res = post(kCreate, "application/proto", request.SerializeAsString());
        QVERIFY(res);
        QCOMPARE(res->status, 200);
        v1::CreateInvoiceResponse created;
        QVERIFY(created.ParseFromString(res->body));
        QVERIFY(created.invoice().id() > 0);
        QCOMPARE(created.invoice().customer(), std::string("Acme S.L."));
        QCOMPARE(created.invoice().customer_tax_id(), std::string("B12345678"));
        QCOMPARE(created.invoice().total_cents(), 121000);

        res = post(kList, "application/proto", {});
        QVERIFY(res);
        v1::ListInvoicesResponse listed;
        QVERIFY(listed.ParseFromString(res->body));
        QCOMPARE(listed.invoices_size(), 1);
        QCOMPARE(listed.invoices(0).id(), created.invoice().id());
    }

    void skipsUnknownFields() {
        // Field 15 (varint 1), of a later version of the contract
        const auto res = post(kCreate, "application/proto", create("Later client", 4200) + std::string("\x78\x01", 2));
        QVERIFY(res);
        QCOMPARE(res->status, 200);
        v1::CreateInvoiceResponse created;
        QVERIFY(created.ParseFromString(res->body));
        QCOMPARE(created.invoice().customer(), std::string("Later client"));
        QCOMPARE(created.invoice().total_cents(), 4200);
        QVERIFY(!created.invoice().has_customer_tax_id());
    }

    void storesAnEmptyTaxIdAsAbsent() {
        const auto res = post(kCreate, "application/proto", create("No tax ID", 5, ""));
        QVERIFY(res);
        QCOMPARE(res->status, 200);
        v1::CreateInvoiceResponse created;
        QVERIFY(created.ParseFromString(res->body));
        QVERIFY(!created.invoice().has_customer_tax_id());

        pqxx::connection connection(m_db->connectionString());
        pqxx::nontransaction tx(connection);
        QVERIFY(tx.query_value<bool>("SELECT customer_tax_id IS NULL FROM invoices WHERE id = $1",
                                     pqxx::params{created.invoice().id()}));
    }

    void acceptsTheConnectProtocolHeader() {
        httplib::Headers headers{{"Authorization", std::string("Bearer ") + kToken},
                                 {"Connect-Protocol-Version", "1"}};
        const auto res = m_client->Post(std::string("/api/") + kList, headers, std::string(), "application/proto");
        QVERIFY(res);
        QCOMPARE(res->status, 200);
    }

    void reportsErrors_data() {
        QTest::addColumn<QString>("procedure");
        QTest::addColumn<QString>("contentType");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("authorized");
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");

        const auto proto = QStringLiteral("application/proto");
        const auto bytes = [](const std::string &s) { return QByteArray::fromStdString(s); };
        QTest::newRow("no token") << kList << proto << QByteArray() << false << 401 << "unauthenticated";
        QTest::newRow("validation") << kCreate << proto << bytes(create("", 1)) << true << 400 << "invalid_argument";
        QTest::newRow("negative total") << kCreate << proto << bytes(create("A", -1)) << true << 400 << "invalid_argument";
        QTest::newRow("malformed") << kCreate << proto << QByteArray("\xff\xff\xff\xff") << true << 400 << "invalid_argument";
        QTest::newRow("json") << kCreate << "application/json" << QByteArray(R"({"customer":"A","totalCents":"1"})") << true << 415 << "unimplemented";
        QTest::newRow("other content type") << kCreate << "application/x-www-form-urlencoded" << QByteArray("customer=A") << true << 415 << "unimplemented";
        QTest::newRow("unknown procedure") << "guita.v1.InvoiceService/DeleteInvoice" << proto << QByteArray() << true << 404 << "unimplemented";
    }

    void reportsErrors() {
        QFETCH(QString, procedure);
        QFETCH(QString, contentType);
        QFETCH(QByteArray, body);
        QFETCH(bool, authorized);
        QFETCH(int, status);
        QFETCH(QString, code);

        httplib::Headers headers;
        if (authorized) headers.emplace("Authorization", std::string("Bearer ") + kToken);
        const auto res = m_client->Post("/api/" + procedure.toStdString(), headers,
                                        body.toStdString(), contentType.toStdString());
        QVERIFY(res);
        QCOMPARE(res->status, status);
        QCOMPARE(res->get_header_value("Content-Type"), std::string("application/json"));
        QCOMPARE(json(res).object().value("code").toString(), code);
    }

    void reportsTheValidationMessage() {
        const auto res = post(kCreate, "application/proto", create("", 1));
        QVERIFY(res);
        QCOMPARE(json(res).object().value("message").toString(), QStringLiteral("Customer is required"));
    }

    void writesErrorBodiesAsJson_data() {
        QTest::addColumn<QString>("message");
        QTest::newRow("plain") << "Customer is required";
        QTest::newRow("quotes and backslashes") << R"(A "quoted" \ path)";
        QTest::newRow("control characters") << "line\nbreak\ttab\x01";
        QTest::newRow("not ASCII") << "Factura de José · 12 €";
    }
    void writesErrorBodiesAsJson() {
        QFETCH(QString, message);
        QJsonParseError error;
        const auto body = QByteArray::fromStdString(connectErrorBody(RpcCode::InvalidArgument, message.toStdString()));
        const auto document = QJsonDocument::fromJson(body, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(document.object().value("code").toString(), QStringLiteral("invalid_argument"));
        QCOMPARE(document.object().value("message").toString(), message);

        // Without a message, only the code
        const auto bare = QJsonDocument::fromJson(QByteArray::fromStdString(connectErrorBody(RpcCode::Internal, {})));
        QCOMPARE(bare.object().keys(), QStringList{"code"});
    }

    void acceptsOnlyPost() {
        const auto res = m_client->Get(std::string("/api/") + kList, auth());
        QVERIFY(res);
        QCOMPARE(res->status, 405);
        QCOMPARE(res->get_header_value("Allow"), std::string("POST"));
    }

    void noLongerServesTheRestRoutes() {
        auto res = m_client->Get("/api/invoices", auth());
        QVERIFY(res);
        QCOMPARE(res->status, 404);
        res = m_client->Post("/api/invoices", auth(), R"({"customer":"A","totalCents":1})", "application/json");
        QVERIFY(res);
        QCOMPARE(res->status, 404);
    }

    void servesInProcessWithoutListening() {
        // The standalone backend: no HTTP options, calls through Backend::call()
        const int listening = listeningTcpSockets();
        QCOMPARE(listening, 1);                 // the HTTP backend of this suite
        Backend inProcess(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(""),
        });
        QVERIFY(inProcess.start());
        QCOMPARE(inProcess.port(), 0);
        QCOMPARE(listeningTcpSockets(), listening);

        const auto result = inProcess.call({kList, {}, {}});
        QCOMPARE(result.code, RpcCode::Ok);
        v1::ListInvoicesResponse listed;
        QVERIFY(listed.ParseFromString(result.payload));
        QVERIFY(listed.invoices_size() > 0);    // the invoices created through HTTP
    }

    void cleanupTestCase() {
        m_client.reset();
        m_backend.reset();
        m_db.reset();
    }

private:
    static constexpr const char *kToken = "rpc-token";
    static constexpr const char *kList = "guita.v1.InvoiceService/ListInvoices";
    static constexpr const char *kCreate = "guita.v1.InvoiceService/CreateInvoice";

    static httplib::Headers auth() { return {{"Authorization", std::string("Bearer ") + kToken}}; }

    httplib::Result post(const char *procedure, const char *contentType, const std::string &body) {
        return m_client->Post(std::string("/api/") + procedure, auth(), body, contentType);
    }

    // A CreateInvoiceRequest; taxId absent unless given
    static std::string create(const std::string &customer, std::int64_t totalCents,
                              std::optional<std::string> taxId = std::nullopt) {
        v1::CreateInvoiceRequest request;
        request.set_customer(customer);
        if (taxId) request.set_customer_tax_id(*taxId);
        request.set_total_cents(totalCents);
        return request.SerializeAsString();
    }

    static QJsonDocument json(const httplib::Result &res) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(res->body));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Backend> m_backend;
    std::unique_ptr<httplib::Client> m_client;
};

QTEST_GUILESS_MAIN(RpcApiTest)
#include "test_rpcapi.moc"
