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

// Called concurrently by the transports: InvoiceService is thread-safe
void addInvoiceProcedures(RpcRouter &router, InvoiceService &service) {
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
                               request.total_cents()};
            v1::CreateInvoiceResponse response;
            toMessage(service.create(std::move(invoice)), *response.mutable_invoice());
            return response;
        });
}
