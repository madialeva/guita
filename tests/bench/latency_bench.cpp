#include <backend/api/HttpRpcTransport.h>
#include <backend/api/RpcRouter.h>
#include <backend/api/WebSocketRpcTransport.h>
#include <client/WebSocketTransport.h>
#include <guita/channel/v1/channel.pb.h>
#include <guita/v1/invoice_service.pb.h>
#include <httplib.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThreadPool>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Round trip of a small call through each transport, on loopback, with the
// same router and a stub procedure (no database, so the transport's cost is
// what is measured): the router called directly, an in-process call with the
// pool hop and the event-queue return that InProcTransport uses, a POST with
// keep-alive (with an empty body and with a small one), a WebSocket frame
// carrying the same bytes, and the persistent channel (envelope, session,
// worker pool and writer thread) with cpp-httplib's client and with the
// client's WebSocketTransport.
namespace v1 = guita::v1;
namespace channel = guita::channel::v1;
using Clock = std::chrono::steady_clock;

namespace {
constexpr const char *kProcedure = "guita.v1.InvoiceService/ListInvoices";

void report(const char *name, std::vector<double> microseconds) {
    std::sort(microseconds.begin(), microseconds.end());
    const auto at = [&](double q) { return microseconds[size_t(q * (microseconds.size() - 1))]; };
    std::printf("%-28s %8zu calls   p50 %8.1f us   p90 %8.1f us   p99 %8.1f us\n",
                name, microseconds.size(), at(0.5), at(0.9), at(0.99));
}

int g_scale = 1;                                      // --quick: a twentieth of the calls

std::vector<double> measure(const char *name, int calls, const std::function<void()> &call) {
    calls /= g_scale;
    std::fprintf(stderr, "measuring %s...\n", name);
    for (int i = 0; i < calls / 10; ++i) call();      // warm-up
    std::vector<double> samples;
    samples.reserve(calls);
    for (int i = 0; i < calls; ++i) {
        const auto start = Clock::now();
        call();
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    return samples;
}
}

int main(int argc, char *argv[]) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);         // results visible as they come
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--quick")))
        g_scale = 20;

    RpcRouter router(tokenAuthorizer(""));
    router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>(kProcedure, [](const v1::ListInvoicesRequest &) {
        v1::ListInvoicesResponse response;
        auto *invoice = response.add_invoices();
        invoice->set_id(42);
        invoice->set_customer("Acme S.L.");
        invoice->set_customer_tax_id("B12345678");
        invoice->set_total_cents(121000);
        return response;
    });
    const std::string request = v1::ListInvoicesRequest().SerializeAsString();
    const RpcCall call{kProcedure, request, {}};

    report("in process, direct", measure("in process, direct", 200000, [&] { (void)router.call(call); }));

    QThreadPool pool;
    QObject receiver;
    report("in process, pool + event", measure("in process, pool + event", 20000, [&] {
        QEventLoop loop;
        pool.start([&] {
            const auto result = router.call(call);
            QMetaObject::invokeMethod(&receiver, [&loop, size = result.payload.size()] { (void)size; loop.quit(); },
                                      Qt::QueuedConnection);
        });
        loop.exec();
    }));

    // TCP_NODELAY everywhere: with Nagle's algorithm and delayed ACKs, a small
    // request/response exchange over TCP waits about 40 ms per side
    httplib::Server server;
    server.set_tcp_nodelay(true);
    addRpcRoutes(server, router);
    WebSocketRpcTransport channels(router, tokenAuthorizer(""));
    channels.addRoute(server);
    server.WebSocket("/ws", [&router](const httplib::Request &, httplib::ws::WebSocket &socket) {
        std::string message;
        while (socket.read(message) != httplib::ws::ReadResult::Fail) {
            const auto result = router.call({kProcedure, message, {}});
            socket.send(result.payload.data(), result.payload.size());
        }
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    std::thread listener([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    {
        httplib::Client client("127.0.0.1", port);
        client.set_keep_alive(true);
        client.set_tcp_nodelay(true);
        report("HTTP POST, keep-alive", measure("HTTP POST, keep-alive", 20000, [&] {
            (void)client.Post(std::string("/api/") + kProcedure, request, "application/proto");
        }));
        // The empty ListInvoicesRequest above has no body; with one, cpp-httplib's
        // client sends headers and body apart and the call costs ~6 us more.
        // Unknown fields (15) make a body the same procedure accepts.
        const std::string withBody("\x78\x01\x78\x01\x78\x01\x78\x01", 8);
        if (const auto res = client.Post(std::string("/api/") + kProcedure, withBody, "application/proto");
            !res || res->status != 200) {
            std::fprintf(stderr, "the call with a body failed\n");
            return 1;
        }
        report("HTTP POST, 8-byte body", measure("HTTP POST, 8-byte body", 20000, [&] {
            (void)client.Post(std::string("/api/") + kProcedure, withBody, "application/proto");
        }));
    }
    {
        httplib::ws::WebSocketClient client("ws://127.0.0.1:" + std::to_string(port) + "/ws");
        client.set_tcp_nodelay(true);
        if (!client.connect()) {
            std::fprintf(stderr, "WebSocket connection failed\n");
            return 1;
        }
        std::string reply;
        report("WebSocket frame", measure("WebSocket frame", 20000, [&] {
            client.send(request.data(), request.size());
            client.read(reply);
        }));
        client.close();
    }
    {
        httplib::ws::WebSocketClient client("ws://127.0.0.1:" + std::to_string(port) + "/api/channel");
        client.set_tcp_nodelay(true);
        channel::ClientFrame hello;
        hello.mutable_hello();
        std::string reply;
        if (!client.connect() || !client.send(hello.SerializeAsString().data(), hello.ByteSizeLong())
            || client.read(reply) != httplib::ws::Binary) {
            std::fprintf(stderr, "channel connection failed\n");
            return 1;
        }
        std::uint64_t id = 0;
        report("channel, cpp-httplib client", measure("channel, cpp-httplib client", 20000, [&] {
            channel::ClientFrame frame;
            frame.mutable_call()->set_correlation_id(++id);
            frame.mutable_call()->set_procedure(kProcedure);
            frame.mutable_call()->set_payload(request);
            const auto bytes = frame.SerializeAsString();
            client.send(bytes.data(), bytes.size());
            client.read(reply);
            channel::ServerFrame answer;
            (void)answer.ParseFromString(reply);
        }));
        client.close();
    }
    {
        // What the client pays: frames, correlation and the event loop
        WebSocketTransport transport(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/channel").arg(port)), {});
        QObject context;
        const auto encoded = QByteArray::fromStdString(request);
        const auto callOnce = [&] {
            QEventLoop loop;
            bool ok = false;
            transport.call(kProcedure, encoded, &context, [&](const ApiTransport::Reply &reply) {
                ok = reply.ok;
                loop.quit();
            }, ApiTransport::Repeat::Safe);
            loop.exec();
            return ok;
        };
        if (!callOnce()) {
            std::fprintf(stderr, "WebSocketTransport call failed\n");
            return 1;
        }
        report("channel, WebSocketTransport", measure("channel, WebSocketTransport", 20000, [&] { (void)callOnce(); }));
    }

    channels.closeAll();
    server.stop();
    listener.join();
    return 0;
}
