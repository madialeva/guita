#include <backend/api/InvoiceRoutes.h>
#include <backend/api/Http.h>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr const char *kPath = "/api/invoices";

// JSON shapes of the API (field names are the member names)
struct InvoiceJson {
    long long id;
    std::string customer;
    std::string customerTaxId;
    Cents totalCents;
};

struct CreateInvoiceJson {
    std::string customer;
    std::optional<std::string> customerTaxId;
    Cents totalCents = 0;
};

InvoiceJson toJson(const Invoice &i) {
    return {i.id, i.customer, i.customerTaxId.value_or(""), i.totalCents};
}
}

// Called concurrently from the HTTP server thread pool: the service is thread-safe
void addInvoiceRoutes(httplib::Server &server, InvoiceService &service, const Authorizer &authorizer) {
    server.Get(kPath, http::guarded(authorizer, [&service](const httplib::Request &, httplib::Response &response) {
        std::vector<InvoiceJson> invoices;
        for (const auto &i : service.list())
            invoices.push_back(toJson(i));
        http::sendJson(response, 200, invoices);
    }));

    server.Post(kPath, http::guarded(authorizer, [&service](const httplib::Request &request, httplib::Response &response) {
        auto in = http::jsonBody<CreateInvoiceJson>(request);
        const auto created = service.create({std::move(in.customer), std::move(in.customerTaxId), in.totalCents});
        http::sendJson(response, 201, toJson(created));
    }));

    http::rejectOtherMethods(server, kPath, "GET, POST");
}
