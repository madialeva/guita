#include <backend/api/RpcRouter.h>
#include <guita/v1/invoice_service.pb.h>
#include <QtTest>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace v1 = guita::v1;

// RpcRouter without any transport: dispatch, binary messages, authorization,
// error codes and the contract check at registration. Stub handlers on the contract's methods, no database.
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

    void callsAProcedure() {
        v1::CreateInvoiceRequest request;
        request.set_customer("Acme");
        request.set_total_cents(121000);

        const auto result = call(kCreate, request.SerializeAsString());
        QCOMPARE(result.code, RpcCode::Ok);
        v1::CreateInvoiceResponse response;
        QVERIFY(response.ParseFromString(result.payload));
        QCOMPARE(response.invoice().id(), 8);
        QCOMPARE(response.invoice().customer(), std::string("Acme"));
        QVERIFY(!response.invoice().has_customer_tax_id());
        QCOMPARE(response.invoice().total_cents(), 121000);
    }

    void skipsUnknownFields() {
        // Field 15 (varint 1) and field 16 (string "x"), of a later version
        v1::CreateInvoiceRequest request;
        request.set_customer("Acme");
        const auto payload = request.SerializeAsString() + std::string("\x78\x01\x82\x01\x01x", 6);

        const auto result = call(kCreate, payload);
        QCOMPARE(result.code, RpcCode::Ok);
        v1::CreateInvoiceResponse response;
        QVERIFY(response.ParseFromString(result.payload));
        QCOMPARE(response.invoice().customer(), std::string("Acme"));
    }

    void rejectsCallsWithoutAuthorization_data() {
        QTest::addColumn<QString>("authorization");
        QTest::newRow("none") << "";
        QTest::newRow("wrong token") << "Bearer wrong";
    }

    void rejectsCallsWithoutAuthorization() {
        QFETCH(QString, authorization);
        const auto result = m_router->call({kList, {}, authorization.toStdString()});
        QCOMPARE(result.code, RpcCode::Unauthenticated);
        QCOMPARE(m_executed.load(), 0);
    }

    void reportsUnknownProcedures() {
        QCOMPARE(call("guita.v1.InvoiceService/DeleteInvoice", {}).code, RpcCode::Unimplemented);
        QCOMPARE(call("nonsense", {}).code, RpcCode::Unimplemented);
    }

    void rejectsMalformedMessages_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("unterminated varints") << QByteArray("\xff\xff\xff\xff");
        QTest::newRow("truncated string") << QByteArray("\x0a\x05" "ab");
        QTest::newRow("invalid wire type") << QByteArray("\x0f");
    }

    void rejectsMalformedMessages() {
        QFETCH(QByteArray, payload);
        const auto result = call(kCreate, payload.toStdString());
        QCOMPARE(result.code, RpcCode::InvalidArgument);
        QCOMPARE(result.message, std::string("Malformed guita.v1.CreateInvoiceRequest"));
        QCOMPARE(m_executed.load(), 0);
    }

    void translatesHandlerExceptions() {
        auto result = call(kCreate, v1::CreateInvoiceRequest().SerializeAsString());
        QCOMPARE(result.code, RpcCode::InvalidArgument);
        QCOMPARE(result.message, std::string("Customer is required"));

        v1::CreateInvoiceRequest boom;
        boom.set_customer("boom");
        result = call(kCreate, boom.SerializeAsString());
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
        // Another package, or no service at all
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>("guita.v2.InvoiceService/ListInvoices", noop)));
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>("ListInvoices", noop)));
        // The contract's own registration is accepted
        router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>("guita.v1.InvoiceService/ListInvoices", noop);
    }

    void isThreadSafe() {
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
            threads.emplace_back([&] {
                for (int j = 0; j < 100; ++j)
                    if (call(kList, {}).code == RpcCode::Ok) ++ok;
            });
        }
        for (auto &thread : threads)
            thread.join();
        QCOMPARE(ok.load(), 1600);
    }

private:
    static constexpr const char *kList = "guita.v1.InvoiceService/ListInvoices";
    static constexpr const char *kCreate = "guita.v1.InvoiceService/CreateInvoice";

    RpcResult call(const std::string &procedure, const std::string &payload) {
        return m_router->call({procedure, payload, "Bearer secret"});
    }

    std::unique_ptr<RpcRouter> m_router;
    std::atomic<int> m_executed{0};
};

QTEST_GUILESS_MAIN(RpcRouterTest)
#include "test_rpcrouter.moc"
