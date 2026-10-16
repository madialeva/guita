#include <backend/api/Backend.h>
#include <backend/api/InvoiceRoutes.h>
#include <backend/core/Database.h>
#include <backend/core/InvoiceService.h>
#include <backend/core/Migrations.h>
#include <backend/core/Statements.h>
#include <httplib.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
// One connection per HTTP worker is plenty for request/response handlers
constexpr std::size_t kPoolSize = 8;
// Largest accepted request body
constexpr std::size_t kMaxRequestBody = 16 * 1024 * 1024;
}

struct Backend::Impl {
    explicit Impl(BackendOptions o)
        : options(std::move(o)),
          db(options.connectionString, kPoolSize, prepareStatements),
          invoices(db) {
        {
            pqxx::connection connection(options.connectionString);
            applyMigrations(connection);
        }
        db.warmUp();          // after the migrations: the statements need the schema

        http.set_payload_max_length(kMaxRequestBody);
        if (!options.docroot.empty() && !http.set_mount_point("/", options.docroot))
            throw std::runtime_error("Docroot not found: " + options.docroot);
        if (options.accessLog) {
            http.set_logger([](const httplib::Request &req, const httplib::Response &res) {
                // Static files are streamed: their body is not in memory
                std::cerr << req.remote_addr << ' ' << req.method << ' ' << req.path << ' ' << res.status;
                if (!res.body.empty()) std::cerr << ' ' << res.body.size();
                std::cerr << '\n';
            });
        }
        addInvoiceRoutes(http, invoices, options.authorizer);
    }

    BackendOptions  options;
    Database        db;
    InvoiceService  invoices;
    httplib::Server http;
    std::thread     listener;
    int             boundPort = 0;
};

Backend::Backend(BackendOptions options) : d(std::make_unique<Impl>(std::move(options))) {}

Backend::~Backend() { stop(); }

bool Backend::start() {
    if (d->listener.joinable()) return true;
    const auto &o = d->options;
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
