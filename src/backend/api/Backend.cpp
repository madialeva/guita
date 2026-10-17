#include <backend/api/Backend.h>
#include <backend/api/HttpRpcTransport.h>
#include <backend/api/InvoiceProcedures.h>
#include <backend/api/RpcRouter.h>
#include <backend/core/Database.h>
#include <backend/core/InvoiceService.h>
#include <backend/core/Migrations.h>
#include <backend/core/Statements.h>
#include <httplib.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
// One connection per worker is plenty for request/response handlers
constexpr std::size_t kPoolSize = 8;
// Largest accepted request body
constexpr std::size_t kMaxRequestBody = 16 * 1024 * 1024;
}

struct Backend::Impl {
    explicit Impl(BackendOptions o)
        : options(std::move(o)),
          db(options.connectionString, kPoolSize, prepareStatements),
          invoices(db),
          router(options.authorizer) {
        {
            pqxx::connection connection(options.connectionString);
            applyMigrations(connection);
        }
        db.warmUp();          // after the migrations: the statements need the schema
        addInvoiceProcedures(router, invoices);
        if (options.http)
            setUpHttp(*options.http);
    }

    void setUpHttp(const HttpOptions &o) {
        // RPC is request/response with small messages: without TCP_NODELAY,
        // Nagle's algorithm and delayed ACKs can add ~40 ms to a reply
        http.set_tcp_nodelay(true);
        http.set_payload_max_length(kMaxRequestBody);
        if (!o.docroot.empty() && !http.set_mount_point("/", o.docroot))
            throw std::runtime_error("Docroot not found: " + o.docroot);
        if (o.accessLog) {
            http.set_logger([](const httplib::Request &req, const httplib::Response &res) {
                // Static files are streamed: their body is not in memory
                std::cerr << req.remote_addr << ' ' << req.method << ' ' << req.path << ' ' << res.status;
                if (!res.body.empty()) std::cerr << ' ' << res.body.size();
                std::cerr << '\n';
            });
        }
        addRpcRoutes(http, router);
    }

    BackendOptions  options;
    Database        db;
    InvoiceService  invoices;
    RpcRouter       router;
    httplib::Server http;
    std::thread     listener;
    int             boundPort = 0;
};

Backend::Backend(BackendOptions options) : d(std::make_unique<Impl>(std::move(options))) {}

Backend::~Backend() { stop(); }

RpcResult Backend::call(const RpcCall &call) const { return d->router.call(call); }

bool Backend::start() {
    if (!d->options.http || d->listener.joinable()) return true;
    const auto &o = *d->options.http;
    d->boundPort = o.port == 0 ? d->http.bind_to_any_port(o.address)
                               : (d->http.bind_to_port(o.address, o.port) ? o.port : -1);
    if (d->boundPort <= 0) return false;

    d->listener = std::thread([this] { d->http.listen_after_bind(); });
    d->http.wait_until_ready();
    return true;
}

int Backend::port() const { return d->boundPort; }

void Backend::stop() {
    if (!d->listener.joinable()) return;
    d->http.stop();
    d->listener.join();
}
