#include <backend/api/EventTopic.h>
#include <backend/api/RpcRouter.h>
#include <guita/v1/invoice_service.pb.h>
#include <QtTest>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace v1 = guita::v1;

// RpcRouter without any transport: dispatch, binary messages, authorization,
// error codes and the contract check at registration, for unary and stream
// procedures; and EventTopic, which stream procedures subscribe to. Stub
// handlers on the contract's methods, no database.
namespace {
// Collects the messages of a subscription from any thread; they are decoded
// in the test's thread, where Qt Test may check them
struct Collector {
    void operator()(std::string payload) {
        std::lock_guard lock(mutex);
        payloads.push_back(std::move(payload));
    }
    std::vector<v1::WatchInvoicesResponse> taken() {
        std::lock_guard lock(mutex);
        std::vector<v1::WatchInvoicesResponse> messages(payloads.size());
        for (size_t i = 0; i < payloads.size(); ++i)
            if (!messages[i].ParseFromString(payloads[i])) messages[i].Clear();
        return messages;
    }
    std::mutex mutex;
    std::vector<std::string> payloads;
};

v1::WatchInvoicesResponse created(long long id) {
    v1::WatchInvoicesResponse event;
    event.mutable_created()->set_id(id);
    return event;
}
}

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
        m_events = std::make_unique<EventTopic<v1::WatchInvoicesResponse>>();
        m_router->addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>(
            kWatch, [this](const v1::WatchInvoicesRequest &, std::function<void(const v1::WatchInvoicesResponse &)> send) {
                v1::WatchInvoicesResponse started;
                started.mutable_started();
                return m_events->subscribe(std::move(send), std::move(started));
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

    void streamsMessagesInOrder() {
        auto collector = std::make_shared<Collector>();
        const auto subscription = subscribe(collector);
        QCOMPARE(subscription.result().code, RpcCode::Ok);
        QVERIFY(subscription.active());
        for (long long id = 1; id <= 3; ++id)
            m_events->publish(created(id));

        const auto messages = collector->taken();
        QCOMPARE(messages.size(), size_t(4));
        QCOMPARE(messages[0].event_case(), v1::WatchInvoicesResponse::kStarted);
        for (int i = 1; i <= 3; ++i)
            QCOMPARE(messages[i].created().id(), i);
    }

    void endsTheSubscriptionWhenReleased() {
        auto collector = std::make_shared<Collector>();
        {
            auto subscription = subscribe(collector);
            m_events->publish(created(1));
            subscription.reset();
            QVERIFY(!subscription.active());
            m_events->publish(created(2));
        }
        QCOMPARE(collector->taken().size(), size_t(2));      // started and invoice 1

        auto other = std::make_shared<Collector>();
        { const auto destroyed = subscribe(other); }
        m_events->publish(created(3));
        QCOMPARE(other->taken().size(), size_t(1));          // only started
    }

    void rejectsSubscriptionsWithoutAuthorization() {
        auto collector = std::make_shared<Collector>();
        const auto subscription = m_router->subscribe({kWatch, {}, "Bearer wrong"},
                                                      [collector](std::string m) { (*collector)(std::move(m)); });
        QCOMPARE(subscription.result().code, RpcCode::Unauthenticated);
        QVERIFY(!subscription.active());
        m_events->publish(created(1));
        QVERIFY(collector->taken().empty());
    }

    void keepsUnaryAndStreamProceduresApart() {
        const auto unary = call(kWatch, {});
        QCOMPARE(unary.code, RpcCode::Unimplemented);
        QVERIFY(unary.message.find("is a stream") != std::string::npos);

        const auto stream = m_router->subscribe({kList, {}, "Bearer secret"}, [](std::string) {});
        QCOMPARE(stream.result().code, RpcCode::Unimplemented);
        QCOMPARE(stream.result().message, std::string("Procedure ") + kList + " is not a stream");
        QVERIFY(!stream.active());

        const auto unknown = m_router->subscribe({"guita.v1.InvoiceService/WatchNothing", {}, "Bearer secret"},
                                                 [](std::string) {});
        QCOMPARE(unknown.result().code, RpcCode::Unimplemented);
        QCOMPARE(m_executed.load(), 0);
    }

    void rejectsMalformedStreamRequests() {
        const auto subscription = m_router->subscribe({kWatch, "\xff\xff\xff\xff", "Bearer secret"}, [](std::string) {});
        QCOMPARE(subscription.result().code, RpcCode::InvalidArgument);
        QCOMPARE(subscription.result().message, std::string("Malformed guita.v1.WatchInvoicesRequest"));
        QVERIFY(!subscription.active());
    }

    void translatesStreamHandlerExceptions() {
        RpcRouter router(tokenAuthorizer(""));
        router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>(
            kWatch, [](const v1::WatchInvoicesRequest &request, auto) -> std::shared_ptr<void> {
                if (request.ByteSizeLong() == 0) throw std::invalid_argument("Nothing to watch");
                throw std::runtime_error("broken");
            });
        auto subscription = router.subscribe({kWatch, {}, {}}, [](std::string) {});
        QCOMPARE(subscription.result().code, RpcCode::InvalidArgument);
        QCOMPARE(subscription.result().message, std::string("Nothing to watch"));
        // Field 15 of a later version: a non-empty request
        subscription = router.subscribe({kWatch, std::string("\x78\x01", 2), {}}, [](std::string) {});
        QCOMPARE(subscription.result().code, RpcCode::Internal);
        QCOMPARE(subscription.result().message, std::string("Internal error"));
    }

    void refusesStreamsOutsideTheContract() {
        RpcRouter router(tokenAuthorizer(""));
        const auto noop = [](const v1::WatchInvoicesRequest &, auto) { return std::shared_ptr<void>(); };
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>("guita.v1.InvoiceService/ListInvoices", noop)));
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
            (router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>("guita.v2.InvoiceService/WatchInvoices", noop)));
        router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>(kWatch, noop);
    }

    void topicGreetsBeforeAnyPublishedMessage() {
        // Publishing while subscribing: every subscription starts with its
        // greeting, then only invoices, in order
        std::thread publisher([&] {
            for (long long id = 1; id <= 20'000; ++id)
                m_events->publish(created(id));
        });
        std::vector<std::shared_ptr<Collector>> collectors;
        std::vector<RpcSubscription> subscriptions;
        for (int i = 0; i < 50; ++i) {
            collectors.push_back(std::make_shared<Collector>());
            subscriptions.push_back(subscribe(collectors.back()));
            std::this_thread::yield();
        }
        publisher.join();
        subscriptions.clear();

        for (const auto &collector : collectors) {
            const auto messages = collector->taken();
            QVERIFY(!messages.empty());
            QCOMPARE(messages[0].event_case(), v1::WatchInvoicesResponse::kStarted);
            for (size_t i = 2; i < messages.size(); ++i)
                QCOMPARE(messages[i].created().id(), messages[i - 1].created().id() + 1);
        }
    }

    void topicUnsubscribesWhilePublishing() {
        // Once a handle is released, its subscriber is never called again
        EventTopic<v1::WatchInvoicesResponse> topic;
        std::atomic<bool> stop{false};
        std::atomic<int> late{0};
        std::vector<std::thread> publishers;
        for (int i = 0; i < 4; ++i)
            publishers.emplace_back([&] { while (!stop) topic.publish(created(1)); });

        std::vector<std::thread> subscribers;
        for (int i = 0; i < 4; ++i) {
            subscribers.emplace_back([&] {
                for (int j = 0; j < 500; ++j) {
                    auto released = std::make_shared<std::atomic<bool>>(false);
                    auto handle = topic.subscribe([released, &late](const v1::WatchInvoicesResponse &) {
                        if (*released) ++late;
                    });
                    handle.reset();
                    *released = true;
                }
            });
        }
        for (auto &thread : subscribers) thread.join();
        stop = true;
        for (auto &thread : publishers) thread.join();
        QCOMPARE(late.load(), 0);
    }

    void topicSurvivesFailingSubscribers() {
        EventTopic<v1::WatchInvoicesResponse> topic;
        int delivered = 0;
        const auto failing = topic.subscribe([](const v1::WatchInvoicesResponse &) { throw std::runtime_error("full"); });
        const auto working = topic.subscribe([&](const v1::WatchInvoicesResponse &) { ++delivered; });
        topic.publish(created(1));
        QCOMPARE(delivered, 1);
    }

private:
    static constexpr const char *kList = "guita.v1.InvoiceService/ListInvoices";
    static constexpr const char *kCreate = "guita.v1.InvoiceService/CreateInvoice";
    static constexpr const char *kWatch = "guita.v1.InvoiceService/WatchInvoices";

    RpcSubscription subscribe(const std::shared_ptr<Collector> &collector) {
        return m_router->subscribe({kWatch, {}, "Bearer secret"},
                                   [collector](std::string message) { (*collector)(std::move(message)); });
    }

    RpcResult call(const std::string &procedure, const std::string &payload) {
        return m_router->call({procedure, payload, "Bearer secret"});
    }

    std::unique_ptr<EventTopic<v1::WatchInvoicesResponse>> m_events;
    std::unique_ptr<RpcRouter> m_router;
    std::atomic<int> m_executed{0};
};

QTEST_GUILESS_MAIN(RpcRouterTest)
#include "test_rpcrouter.moc"
