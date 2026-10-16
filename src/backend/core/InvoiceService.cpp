#include <backend/core/InvoiceService.h>
#include <backend/core/InvoiceStore.h>
#include <stdexcept>

namespace {
// numeric(12,2): up to 9 999 999 999.99
constexpr Cents kMaxTotalCents = 999'999'999'999;
}

Invoice InvoiceService::create(NewInvoice invoice) {
    if (invoice.customer.empty())
        throw std::invalid_argument("Customer is required");
    if (invoice.totalCents < 0)
        throw std::invalid_argument("Total must not be negative");
    if (invoice.totalCents > kMaxTotalCents)
        throw std::invalid_argument("Total is too large");
    if (invoice.customerTaxId && invoice.customerTaxId->empty())
        invoice.customerTaxId.reset();

    return m_db.inTransaction([&](pqxx::work &tx) { return InvoiceStore(tx).insert(invoice); });
}

std::vector<Invoice> InvoiceService::list() {
    return m_db.inTransaction([](pqxx::work &tx) { return InvoiceStore(tx).list(); });
}
