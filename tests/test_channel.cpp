#include <backend/api/Backend.h>
#include <backend/api/EventTopic.h>
#include <backend/api/RpcRouter.h>
#include <backend/api/WebSocketRpcTransport.h>
#include <guita/channel/v1/channel.pb.h>
#include <guita/v1/invoice_service.pb.h>
#include <support/TestDatabase.h>
#include <httplib.h>
#include <pqxx/pqxx>
#include <QtTest>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace v1 = guita::v1;
namespace channel = guita::channel::v1;
using namespace std::chrono_literals;

namespace {
constexpr const char *kToken = "channel-token";
constexpr const char *kList = "guita.v1.InvoiceService/ListInvoices";
constexpr const char *kCreate = "guita.v1.InvoiceService/CreateInvoice";
constexpr const char *kWatch = "guita.v1.InvoiceService/WatchInvoices";

std::string createRequest(const std::string &customer, std::int64_t cents) {
    v1::CreateInvoiceRequest request;
    request.set_customer(customer);
    request.set_total_cents(cents);
    return request.SerializeAsString();
}

// A client of the channel, frame by frame, with cpp-httplib's WebSocket client
class Client {
public:
    explicit Client(int port) : m_socket("ws://127.0.0.1:" + std::to_string(port) + "/api/channel") {
        m_socket.set_read_timeout(5s);
        m_socket.set_tcp_nodelay(true);
        m_connected = bool(m_socket.connect());
    }

    bool connected() const { return m_connected; }

    bool send(const channel::ClientFrame &frame) {
        const auto bytes = frame.SerializeAsString();
        return m_socket.send(bytes.data(), bytes.size());
    }
    bool sendText(const std::string &text) { return m_socket.send(text); }
    bool sendBytes(const std::string &bytes) { return m_socket.send(bytes.data(), bytes.size()); }

    bool hello(const std::string &token = kToken) {
        channel::ClientFrame frame;
        frame.mutable_hello()->set_authorization("Bearer " + token);
        return send(frame);
    }

    // Hello, answered by a welcome
    bool open() {
        if (!m_connected || !hello()) return false;
        const auto frame = read();
        return frame && frame->frame_case() == channel::ServerFrame::kWelcome;
    }

    bool call(std::uint64_t id, const std::string &procedure, const std::string &payload = {}) {
        channel::ClientFrame frame;
        frame.mutable_call()->set_correlation_id(id);
        frame.mutable_call()->set_procedure(procedure);
        frame.mutable_call()->set_payload(payload);
        return send(frame);
    }

    bool cancel(std::uint64_t id) {
        channel::ClientFrame frame;
        frame.mutable_cancel()->set_correlation_id(id);
        return send(frame);
    }

    // The next frame; none when the connection is closed or nothing arrives in time
    std::optional<channel::ServerFrame> read() {
        std::string message;
        const auto result = m_socket.read(message);
        channel::ServerFrame frame;
        if (result != httplib::ws::Binary || !frame.ParseFromString(message)) return std::nullopt;
        return frame;
    }

    // Whether the server closed the connection (reading fails rather than timing out)
    bool closedByServer() {
        std::string message;
        while (true) {
            const auto result = m_socket.read(message);
            if (result == httplib::ws::Fail) return true;
            if (result == httplib::ws::Timeout) return false;
        }
    }

    void close() { m_socket.close(); }

private:
    httplib::ws::WebSocketClient m_socket;
    bool m_connected = false;
};

// A server of the channel alone, on a router with stub procedures: a fast and
// a slow call (customer "slow" takes 500 ms) and a stream of events published
// by the test
class StubServer {
public:
    explicit StubServer(ChannelOptions options)
        : m_router(tokenAuthorizer(kToken)),
          m_channels(m_router, tokenAuthorizer(kToken), options) {
        m_router.add<v1::CreateInvoiceRequest, v1::CreateInvoiceResponse>(
            kCreate, [](const v1::CreateInvoiceRequest &request) {
                if (request.customer() == "slow") std::this_thread::sleep_for(500ms);
                v1::CreateInvoiceResponse response;
                response.mutable_invoice()->set_customer(request.customer());
                return response;
            });
        m_router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>(
            kWatch, [this](const v1::WatchInvoicesRequest &, std::function<void(const v1::WatchInvoicesResponse &)> send) {
                v1::WatchInvoicesResponse started;
                started.mutable_started();
                return events.subscribe(std::move(send), std::move(started));
            });
        m_server.set_tcp_nodelay(true);
        m_server.set_write_timeout(1, 0);
        m_channels.addRoute(m_server);
        m_port = m_server.bind_to_any_port("127.0.0.1");
        m_listener = std::thread([this] { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    ~StubServer() {
        m_channels.closeAll();
        m_server.stop();
        m_listener.join();
    }

    int port() const { return m_port; }
    std::size_t openChannels() const { return m_channels.openChannels(); }

    EventTopic<v1::WatchInvoicesResponse> events;

private:
    RpcRouter m_router;
    WebSocketRpcTransport m_channels;
    httplib::Server m_server;
    std::thread m_listener;
    int m_port = 0;
};

bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}
}

// The persistent channel over WebSocket: hello, correlated and concurrent
// calls with the same results as HTTP, subscriptions, protocol errors and the
// server's limits. Frames are written and read with the contract's code;
// the server is a real Backend for invoices, or the channel alone on stub
// procedures for timing.
class ChannelTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_db = std::make_unique<TestDatabase>("channel");
        m_backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(m_backend->start());
    }

    void hasTheDocumentedDefaults() {
        const ChannelOptions options;
        QCOMPARE(options.helloTimeout, std::chrono::milliseconds(10'000));
        QCOMPARE(options.maxChannels, std::size_t(64));
        QCOMPARE(options.maxInFlight, std::size_t(64));
        QCOMPARE(options.maxQueuedBytes, std::size_t(1024 * 1024));
        QCOMPARE(HttpOptions().maxChannels, std::size_t(64));
    }

    void answersCallsAfterAHello() {
        Client client(m_backend->port());
        QVERIFY(client.open());
        QVERIFY(client.call(41, kCreate, createRequest("Acme S.L.", 121000)));
        auto frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(41));
        v1::CreateInvoiceResponse created;
        QVERIFY(created.ParseFromString(frame->reply().payload()));
        QCOMPARE(created.invoice().customer(), std::string("Acme S.L."));

        QVERIFY(client.call(42, kList));
        frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(42));
        QCOMPARE(frame->reply().result_case(), channel::Reply::kPayload);
        v1::ListInvoicesResponse listed;
        QVERIFY(listed.ParseFromString(frame->reply().payload()));
        QVERIFY(listed.invoices_size() >= 1);
    }

    void rejectsAWrongToken_data() {
        QTest::addColumn<QString>("token");
        QTest::newRow("wrong") << "wrong";
        QTest::newRow("empty") << "";
    }
    void rejectsAWrongToken() {
        QFETCH(QString, token);
        Client client(m_backend->port());
        QVERIFY(client.hello(token.toStdString()));
        const auto frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->frame_case(), channel::ServerFrame::kGoodbye);
        QCOMPARE(frame->goodbye().error().code(), std::string("unauthenticated"));
        QVERIFY(client.closedByServer());
    }

    void closesWhenTheFirstFrameIsNoHello() {
        const int before = invoices();
        Client client(m_backend->port());
        QVERIFY(client.call(1, kCreate, createRequest("No hello", 1)));
        const auto frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->frame_case(), channel::ServerFrame::kGoodbye);
        QVERIFY(client.closedByServer());
        QCOMPARE(invoices(), before);
    }

    void closesWhenNoHelloArrives() {
        StubServer server(ChannelOptions{.helloTimeout = 300ms});
        Client client(server.port());
        QVERIFY(client.connected());
        const auto start = std::chrono::steady_clock::now();
        QVERIFY(client.closedByServer());
        QVERIFY(std::chrono::steady_clock::now() - start < 2s);
    }

    void reportsTheErrorsHttpReports_data() {
        QTest::addColumn<QString>("procedure");
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("message");
        QTest::newRow("validation") << kCreate << QByteArray::fromStdString(createRequest("", 1))
                                    << "invalid_argument" << "Customer is required";
        QTest::newRow("malformed") << kCreate << QByteArray("\xff\xff\xff\xff")
                                   << "invalid_argument" << "Malformed guita.v1.CreateInvoiceRequest";
        QTest::newRow("unknown procedure") << "guita.v1.InvoiceService/DeleteInvoice" << QByteArray()
                                           << "unimplemented" << "Unknown procedure guita.v1.InvoiceService/DeleteInvoice";
    }
    void reportsTheErrorsHttpReports() {
        QFETCH(QString, procedure);
        QFETCH(QByteArray, payload);
        QFETCH(QString, code);
        QFETCH(QString, message);
        Client client(m_backend->port());
        QVERIFY(client.open());
        QVERIFY(client.call(7, procedure.toStdString(), payload.toStdString()));
        const auto frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(7));
        QCOMPARE(frame->reply().result_case(), channel::Reply::kError);
        QCOMPARE(QString::fromStdString(frame->reply().error().code()), code);
        QCOMPARE(QString::fromStdString(frame->reply().error().message()), message);
    }

    void answersConcurrentCallsAsTheyFinish() {
        StubServer server(ChannelOptions{});
        Client client(server.port());
        QVERIFY(client.open());
        QVERIFY(client.call(1, kCreate, createRequest("slow", 1)));
        QVERIFY(client.call(2, kCreate, createRequest("fast", 1)));
        const auto first = client.read();
        const auto second = client.read();
        QVERIFY(first && second);
        QCOMPARE(first->reply().correlation_id(), std::uint64_t(2));
        QCOMPARE(second->reply().correlation_id(), std::uint64_t(1));
    }

    void rejectsACorrelationIdInUse() {
        StubServer server(ChannelOptions{});
        Client client(server.port());
        QVERIFY(client.open());
        QVERIFY(client.call(5, kCreate, createRequest("slow", 1)));
        QVERIFY(client.call(5, kCreate, createRequest("fast", 1)));
        const auto rejected = client.read();
        QVERIFY(rejected);
        QCOMPARE(rejected->reply().correlation_id(), std::uint64_t(5));
        QCOMPARE(rejected->reply().error().code(), std::string("invalid_argument"));
        const auto answered = client.read();
        QVERIFY(answered);
        QCOMPARE(answered->reply().result_case(), channel::Reply::kPayload);
    }

    void deliversEventsOfOtherChannels() {
        Client watcher(m_backend->port()), creator(m_backend->port());
        QVERIFY(watcher.open());
        QVERIFY(creator.open());

        QVERIFY(watcher.call(9, kWatch));
        auto frame = watcher.read();
        QVERIFY(frame);
        QCOMPARE(frame->message().correlation_id(), std::uint64_t(9));
        v1::WatchInvoicesResponse event;
        QVERIFY(event.ParseFromString(frame->message().payload()));
        QCOMPARE(event.event_case(), v1::WatchInvoicesResponse::kStarted);

        QVERIFY(creator.call(1, kCreate, createRequest("Watched", 5)));
        QVERIFY(creator.read());
        frame = watcher.read();
        QVERIFY(frame);
        QCOMPARE(frame->frame_case(), channel::ServerFrame::kMessage);
        QCOMPARE(frame->message().correlation_id(), std::uint64_t(9));
        QVERIFY(event.ParseFromString(frame->message().payload()));
        QCOMPARE(event.created().customer(), std::string("Watched"));

        // Cancelled: the next invoice sends nothing, so the next frame is the
        // reply to a later call
        QVERIFY(watcher.cancel(9));
        QVERIFY(watcher.call(10, kList));
        QVERIFY(watcher.read());
        QVERIFY(creator.call(2, kCreate, createRequest("Unwatched", 5)));
        QVERIFY(creator.read());
        QVERIFY(watcher.call(11, kList));
        frame = watcher.read();
        QVERIFY(frame);
        QCOMPARE(frame->frame_case(), channel::ServerFrame::kReply);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(11));
    }

    void reportsSubscriptionErrors() {
        Client client(m_backend->port());
        QVERIFY(client.open());
        QVERIFY(client.call(3, "guita.v1.InvoiceService/WatchNothing"));
        const auto frame = client.read();
        QVERIFY(frame);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(3));
        QCOMPARE(frame->reply().error().code(), std::string("unimplemented"));
    }

    void closesOnFramesThatAreNot_data() {
        QTest::addColumn<bool>("text");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("text") << true << QByteArray("hello");
        QTest::newRow("garbage") << false << QByteArray("\xff\xff\xff\xff");
        QTest::newRow("second hello") << false
                                      << QByteArray::fromStdString([] {
                                             channel::ClientFrame frame;
                                             frame.mutable_hello()->set_authorization("Bearer x");
                                             return frame.SerializeAsString();
                                         }());
    }
    void closesOnFramesThatAreNot() {
        QFETCH(bool, text);
        QFETCH(QByteArray, bytes);
        Client client(m_backend->port());
        QVERIFY(client.open());
        QVERIFY(text ? client.sendText(bytes.toStdString()) : client.sendBytes(bytes.toStdString()));
        QVERIFY(client.closedByServer());
    }

    void limitsTheChannels() {
        Backend backend(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{.maxChannels = 2},
        });
        QVERIFY(backend.start());
        auto first = std::make_unique<Client>(backend.port());
        Client second(backend.port());
        QVERIFY(first->open());
        QVERIFY(second.open());

        Client third(backend.port());
        QVERIFY(third.closedByServer());

        // HTTP is still served
        httplib::Client http("127.0.0.1", backend.port());
        const auto res = http.Post(std::string("/api/") + kList, {{"Authorization", std::string("Bearer ") + kToken}},
                                   std::string(), "application/proto");
        QVERIFY(res);
        QCOMPARE(res->status, 200);

        // A closed channel frees its place
        first->close();
        first.reset();
        QVERIFY(waitFor([&] {
            Client again(backend.port());
            return again.open();
        }, 3s));
    }

    void closesAClientThatDoesNotRead() {
        StubServer server(ChannelOptions{.maxQueuedBytes = 64 * 1024});
        Client idle(server.port()), active(server.port());
        QVERIFY(idle.open());
        QVERIFY(active.open());
        // Subscribed once the greeting arrives (publishing before it would reach
        // no one); then it never reads again
        QVERIFY(idle.call(1, kWatch));
        const auto started = idle.read();
        QVERIFY(started);
        QCOMPARE(started->frame_case(), channel::ServerFrame::kMessage);

        v1::WatchInvoicesResponse big;
        big.mutable_created()->set_customer(std::string(60 * 1024, 'x'));
        auto slowest = std::chrono::steady_clock::duration::zero();
        for (int i = 0; i < 400 && server.openChannels() == 2; ++i) {
            const auto start = std::chrono::steady_clock::now();
            server.events.publish(big);
            slowest = std::max(slowest, std::chrono::steady_clock::now() - start);
        }
        // Publishing never waited for the idle client, which is closed
        QVERIFY(slowest < 100ms);
        QVERIFY(waitFor([&] { return server.openChannels() == 1; }, 15s));

        QVERIFY(active.call(2, kCreate, createRequest("fast", 1)));
        const auto frame = active.read();
        QVERIFY(frame);
        QCOMPARE(frame->reply().correlation_id(), std::uint64_t(2));
    }

    void stopsWithOpenChannels() {
        auto backend = std::make_unique<Backend>(BackendOptions{
            .connectionString = m_db->connectionString(),
            .authorizer = tokenAuthorizer(kToken),
            .http = HttpOptions{},
        });
        QVERIFY(backend->start());
        std::vector<std::unique_ptr<Client>> clients;
        for (int i = 0; i < 3; ++i) {
            clients.push_back(std::make_unique<Client>(backend->port()));
            QVERIFY(clients.back()->open());
            QVERIFY(clients.back()->call(1, kWatch));
            QVERIFY(clients.back()->read());
        }
        const auto start = std::chrono::steady_clock::now();
        backend->stop();
        QVERIFY(std::chrono::steady_clock::now() - start < 1s);
        for (auto &client : clients)
            QVERIFY(client->closedByServer());
    }

    void cleanupTestCase() {
        m_backend.reset();
        m_db.reset();
    }

private:
    int invoices() {
        pqxx::connection connection(m_db->connectionString());
        pqxx::nontransaction tx(connection);
        return tx.query_value<int>("SELECT count(*) FROM invoices");
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Backend> m_backend;
};

QTEST_GUILESS_MAIN(ChannelTest)
#include "test_channel.moc"
