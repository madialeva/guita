#include <backend/api/HttpRpcTransport.h>
#include <backend/api/RpcRouter.h>
#include <guita/v1/invoice_service.pb.h>
#include <httplib.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThreadPool>
#include <algorithm>
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
// keep-alive, and a WebSocket frame carrying the same bytes.
namespace v1 = guita::v1;
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
    const RpcCall call{kProcedure, RpcCodec::Binary, request, {}};

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
    server.WebSocket("/ws", [&router](const httplib::Request &, httplib::ws::WebSocket &socket) {
        std::string message;
        while (socket.read(message) != httplib::ws::ReadResult::Fail) {
            const auto result = router.call({kProcedure, RpcCodec::Binary, message, {}});
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

    server.stop();
    listener.join();
    return 0;
}
