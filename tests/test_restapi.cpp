#include <backend/api/Backend.h>
#include <support/TestDatabase.h>
#include <httplib.h>
#include <pqxx/pqxx>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <memory>

// The REST contract of /api/invoices against a real Backend on a free port.
// Responses are parsed with Qt, independently of the JSON library of the backend.
class RestApiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("restapi");
        m_backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
        });
        QVERIFY(m_backend->start());
        // Decompresses gzip responses transparently, like a browser
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_backend->port());
    }

    void rejectsRequestsWithoutTheToken() {
        auto res = m_client->Get("/api/invoices");
        QVERIFY(res);
        QCOMPARE(res->status, 401);
        QCOMPARE(json(res).object().value("error").toString(), QStringLiteral("Unauthorized"));

        res = m_client->Get("/api/invoices", httplib::Headers{{"Authorization", "Bearer wrong"}});
        QVERIFY(res);
        QCOMPARE(res->status, 401);
    }

    void createsAnInvoice() {
        auto res = post(R"({"customer":"Acme S.L.","customerTaxId":"B12345678","totalCents":121000})");
        QVERIFY(res);
        QCOMPARE(res->status, 201);
        QCOMPARE(res->get_header_value("Content-Type"), std::string("application/json"));

        const auto invoice = json(res).object();
        QVERIFY(invoice.value("id").toInteger() > 0);
        QCOMPARE(invoice.value("customer").toString(), QStringLiteral("Acme S.L."));
        QCOMPARE(invoice.value("customerTaxId").toString(), QStringLiteral("B12345678"));
        QCOMPARE(invoice.value("totalCents").toInteger(), 121000);
        QCOMPARE(invoice.keys().size(), 4);
    }

    void listsTheInvoices() {
        const auto res = m_client->Get("/api/invoices", auth());
        QVERIFY(res);
        QCOMPARE(res->status, 200);

        const auto invoices = json(res).array();
        QVERIFY(!invoices.isEmpty());
        const auto first = invoices.first().toObject();
        QCOMPARE(first.value("customer").toString(), QStringLiteral("Acme S.L."));
        QCOMPARE(first.value("totalCents").toInteger(), 121000);
    }

    void storesAnEmptyTaxIdAsNull() {
        const auto res = post(R"({"customer":"No tax ID","customerTaxId":"","totalCents":5})");
        QVERIFY(res);
        QCOMPARE(res->status, 201);
        const auto invoice = json(res).object();
        QCOMPARE(invoice.value("customerTaxId").toString(), QString());

        pqxx::connection connection(m_db->connectionString());
        pqxx::nontransaction tx(connection);
        QVERIFY(tx.query_value<bool>("SELECT customer_tax_id IS NULL FROM invoices WHERE id = $1",
                                     pqxx::params{invoice.value("id").toInteger()}));
    }

    void ignoresUnknownFields() {
        const auto res = post(R"({"customer":"Newer client","totalCents":100,"currency":"EUR"})");
        QVERIFY(res);
        QCOMPARE(res->status, 201);
    }

    void rejectsInvalidInvoices_data() {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("error");

        QTest::newRow("no customer") << R"({"totalCents":100})" << "Customer is required";
        QTest::newRow("negative total") << R"({"customer":"Acme","totalCents":-1})" << "Total must not be negative";
        QTest::newRow("total too large") << R"({"customer":"Acme","totalCents":1000000000000})" << "Total is too large";
    }

    void rejectsInvalidInvoices() {
        QFETCH(QString, body);
        QFETCH(QString, error);
        const auto res = post(body.toStdString());
        QVERIFY(res);
        QCOMPARE(res->status, 400);
        QCOMPARE(json(res).object().value("error").toString(), error);
    }

    void rejectsMalformedJson_data() {
        QTest::addColumn<QString>("body");
        QTest::newRow("syntax") << "{x";
        QTest::newRow("total as text") << R"({"customer":"Acme","totalCents":"12"})";
        QTest::newRow("fractional cents") << R"({"customer":"Acme","totalCents":12.5})";
    }

    void rejectsMalformedJson() {
        QFETCH(QString, body);
        const auto res = post(body.toStdString());
        QVERIFY(res);
        QCOMPARE(res->status, 400);
        QVERIFY(json(res).object().value("error").toString().startsWith("Invalid JSON"));
    }

    void rejectsBodiesThatAreNotJson() {
        const auto res = m_client->Post("/api/invoices", auth(), "customer=Acme", "application/x-www-form-urlencoded");
        QVERIFY(res);
        QCOMPARE(res->status, 415);
    }

    void rejectsOtherMethods() {
        const auto res = m_client->Delete("/api/invoices", auth());
        QVERIFY(res);
        QCOMPARE(res->status, 405);
        QCOMPARE(res->get_header_value("Allow"), std::string("GET, POST"));
    }

    void compressesResponsesWithGzip() {
        // A client that keeps the raw body, to see what travels on the wire
        httplib::Client raw("127.0.0.1", m_backend->port());
        raw.set_decompress(false);
        auto headers = auth();
        headers.emplace("Accept-Encoding", "gzip");

        const auto res = raw.Get("/api/invoices", headers);
        QVERIFY(res);
        QCOMPARE(res->status, 200);
        QCOMPARE(res->get_header_value("Content-Encoding"), std::string("gzip"));
        QVERIFY(json(res).isNull());    // compressed: not readable as JSON
    }

    void cleanupTestCase() {
        m_client.reset();
        m_backend.reset();
        m_db.reset();
    }

private:
    static constexpr const char *kToken = "test-token";

    static httplib::Headers auth() { return {{"Authorization", std::string("Bearer ") + kToken}}; }

    httplib::Result post(const std::string &body) {
        return m_client->Post("/api/invoices", auth(), body, "application/json");
    }

    static QJsonDocument json(const httplib::Result &res) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(res->body));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Backend> m_backend;
    std::unique_ptr<httplib::Client> m_client;
};

QTEST_GUILESS_MAIN(RestApiTest)
#include "test_restapi.moc"
