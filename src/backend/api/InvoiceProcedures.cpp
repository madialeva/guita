#include <backend/api/InvoiceProcedures.h>
#include <guita/v1/invoice_service.pb.h>
#include <optional>

namespace v1 = guita::v1;

namespace {
void toMessage(const Invoice &invoice, v1::Invoice &message) {
    message.set_id(invoice.id);
    message.set_customer(invoice.customer);
    if (invoice.customerTaxId)
        message.set_customer_tax_id(*invoice.customerTaxId);
    message.set_total_cents(invoice.totalCents);
}
}

InvoiceService::CreatedHandler publishCreatedInvoices(InvoiceEvents &events) {
    return [&events](const Invoice &invoice) {
        v1::WatchInvoicesResponse event;
        toMessage(invoice, *event.mutable_created());
        events.publish(event);
    };
}

// Called concurrently by the transports: InvoiceService and the topic are thread-safe
void addInvoiceProcedures(RpcRouter &router, InvoiceService &service, InvoiceEvents &events) {
    router.add<v1::ListInvoicesRequest, v1::ListInvoicesResponse>(
        "guita.v1.InvoiceService/ListInvoices", [&service](const v1::ListInvoicesRequest &) {
            v1::ListInvoicesResponse response;
            for (const auto &invoice : service.list())
                toMessage(invoice, *response.add_invoices());
            return response;
        });

    router.add<v1::CreateInvoiceRequest, v1::CreateInvoiceResponse>(
        "guita.v1.InvoiceService/CreateInvoice", [&service](const v1::CreateInvoiceRequest &request) {
            NewInvoice invoice{request.customer(),
                               request.has_customer_tax_id()
                                   ? std::optional<std::string>(request.customer_tax_id())
                                   : std::nullopt,
                               request.total_cents(),
                               request.request_id().empty()
                                   ? std::nullopt
                                   : std::optional<std::string>(request.request_id())};
            v1::CreateInvoiceResponse response;
            toMessage(service.create(std::move(invoice)), *response.mutable_invoice());
            return response;
        });

    // "started" first, under the topic's lock, so that no invoice can arrive
    // before it; from then on, every committed invoice
    router.addStream<v1::WatchInvoicesRequest, v1::WatchInvoicesResponse>(
        "guita.v1.InvoiceService/WatchInvoices",
        [&events](const v1::WatchInvoicesRequest &, std::function<void(const v1::WatchInvoicesResponse &)> send) {
            v1::WatchInvoicesResponse started;
            started.mutable_started();
            return events.subscribe(std::move(send), std::move(started));
        });
}
