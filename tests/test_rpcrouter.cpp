#include <backend/api/RpcRouter.h>
#include <guita/v1/invoice_service.pb.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace v1 = guita::v1;

// RpcRouter without any transport: dispatch, codecs, authorization and error
// codes. Stub handlers on the contract's methods, no database.
class RpcRouterTest : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_router = std::make_unique<RpcRouter>(tokenAuthorizer("secret"));
        m_executed = 0;
        m_router->add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>(
            kList, [this](const v1::ListInvoicesRequest &) {
                ++m_executed;
                v1::ListInvoicesResponse response;
                auto *invoice = response.add_invoices();
                invoice->set_id(7);
                invoice->set_customer("Acme");
                invoice->set_total_cents(0);
                return response;
            });
        m_router->add<v1::CreateInvoiceRequest, v1::CreateInvoiceResponse>(
            kCreate, [this](const v1::CreateInvoiceRequest &request) {
                ++m_executed;
                if (request.customer().empty()) throw std::invalid_argument("Customer is required");
                if (request.customer() == "boom") throw std::runtime_error("database is down");
                v1::CreateInvoiceResponse response;
                response.mutable_invoice()->set_id(8);
                response.mutable_invoice()->set_customer(request.customer());
                response.mutable_invoice()->set_total_cents(request.total_cents());
                return response;
            });
    }

    void callsWithTheBinaryCodec() {
        v1::CreateInvoiceRequest request;
        request.set_customer("Acme");
        request.set_total_cents(121000);

        const auto result = call(kCreate, RpcCodec::Binary, request.SerializeAsString());
        QCOMPARE(result.code, RpcCode::Ok);
        v1::CreateInvoiceResponse response;
        QVERIFY(response.ParseFromString(result.payload));
        QCOMPARE(response.invoice().id(), 8);
        QCOMPARE(response.invoice().customer(), std::string("Acme"));
        QCOMPARE(response.invoice().total_cents(), 121000);
    }

    void callsWithTheJsonCodec() {
        const auto result = call(kCreate, RpcCodec::Json,
                                 R"({"customer":"Acme","totalCents":"121000","futureField":true})");
        QCOMPARE(result.code, RpcCode::Ok);

        const auto invoice = json(result).object().value("invoice").toObject();
        QCOMPARE(invoice.value("customer").toString(), QStringLiteral("Acme"));
        // Canonical proto3 JSON: int64 as strings, absent optionals omitted
        QCOMPARE(invoice.value("id").toString(), QStringLiteral("8"));
        QCOMPARE(invoice.value("totalCents").toString(), QStringLiteral("121000"));
        QVERIFY(!invoice.contains("customerTaxId"));
    }

    void printsZerosInJson() {
        const auto result = call(kList, RpcCodec::Json, "{}");
        QCOMPARE(result.code, RpcCode::Ok);
        const auto invoice = json(result).object().value("invoices").toArray().first().toObject();
        QCOMPARE(invoice.value("totalCents").toString(), QStringLiteral("0"));
    }

    void rejectsCallsWithoutAuthorization_data() {
        QTest::addColumn<QString>("authorization");
        QTest::newRow("none") << "";
        QTest::newRow("wrong token") << "Bearer wrong";
    }

    void rejectsCallsWithoutAuthorization() {
        QFETCH(QString, authorization);
        const auto result = m_router->call({kList, RpcCodec::Binary, {}, authorization.toStdString()});
        QCOMPARE(result.code, RpcCode::Unauthenticated);
        QCOMPARE(m_executed.load(), 0);
    }

    void reportsUnknownProcedures() {
        QCOMPARE(call("guita.v1.InvoiceService/DeleteInvoice", RpcCodec::Binary, {}).code, RpcCode::Unimplemented);
        QCOMPARE(call("nonsense", RpcCodec::Json, "{}").code, RpcCode::Unimplemented);
    }

    void rejectsMalformedMessages_data() {
        QTest::addColumn<int>("codec");
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("binary") << int(RpcCodec::Binary) << QByteArray("\xff\xff\xff\xff");
        QTest::newRow("json syntax") << int(RpcCodec::Json) << QByteArray("{x");
        QTest::newRow("json fractional int64") << int(RpcCodec::Json) << QByteArray(R"({"totalCents":12.5})");
        QTest::newRow("json wrong type") << int(RpcCodec::Json) << QByteArray(R"({"customer":5})");
    }

    void rejectsMalformedMessages() {
        QFETCH(int, codec);
        QFETCH(QByteArray, payload);
        const auto result = call(kCreate, RpcCodec(codec), payload.toStdString());
        QCOMPARE(result.code, RpcCode::InvalidArgument);
        QCOMPARE(m_executed.load(), 0);
    }

    void translatesHandlerExceptions() {
        auto result = call(kCreate, RpcCodec::Json, R"({"customer":""})");
        QCOMPARE(result.code, RpcCode::InvalidArgument);
        QCOMPARE(result.message, std::string("Customer is required"));

        result = call(kCreate, RpcCodec::Json, R"({"customer":"boom"})");
        QCOMPARE(result.code, RpcCode::Internal);
        QCOMPARE(result.message, std::string("Internal error"));   // details stay in the log
    }

    void refusesProceduresOutsideTheContract() {
        RpcRouter router(tokenAuthorizer(""));
        const auto noop = [](const v1::ListInvoicesRequest &) { return v1::ListInvoicesResponse(); };
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>("guita.v1.InvoiceService/Missing", noop)));
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>("guita.v1.InvoiceService/CreateInvoice", noop)));
    }

    void isThreadSafe() {
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
            threads.emplace_back([&] {
                for (int j = 0; j < 100; ++j)
                    if (call(kList, RpcCodec::Binary, {}).code == RpcCode::Ok) ++ok;
            });
        }
        for (auto &thread : threads)
            thread.join();
        QCOMPARE(ok.load(), 1600);
    }

private:
    static constexpr const char *kList = "guita.v1.InvoiceService/ListInvoices";
    static constexpr const char *kCreate = "guita.v1.InvoiceService/CreateInvoice";

    RpcResult call(const std::string &procedure, RpcCodec codec, const std::string &payload) {
        return m_router->call({procedure, codec, payload, "Bearer secret"});
    }

    static QJsonDocument json(const RpcResult &result) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(result.payload));
    }

    std::unique_ptr<RpcRouter> m_router;
    std::atomic<int> m_executed{0};
};

QTEST_GUILESS_MAIN(RpcRouterTest)
#include "test_rpcrouter.moc"
