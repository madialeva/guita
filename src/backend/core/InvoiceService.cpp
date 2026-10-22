#include <backend/core/InvoiceService.h>
#include <backend/core/InvoiceStore.h>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
// numeric(12,2): up to 9 999 999 999.99
constexpr Cents kMaxTotalCents = 999'999'999'999;

// The canonical text of a UUID: 8-4-4-4-12 hexadecimal digits, any case
bool isUuid(std::string_view text) {
    if (text.size() != 36) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !std::isxdigit(static_cast<unsigned char>(text[i])))
            return false;
    }
    return true;
}

bool sameInvoice(const Invoice &existing, const NewInvoice &invoice) {
    return existing.customer == invoice.customer && existing.customerTaxId == invoice.customerTaxId
        && existing.totalCents == invoice.totalCents;
}
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
    if (invoice.requestId && !isUuid(*invoice.requestId))
        throw std::invalid_argument("request_id must be a UUID");

    auto [result, created] = m_db.inTransaction([&](pqxx::work &tx) {
        InvoiceStore store(tx);
        if (auto created = store.insert(invoice))
            return std::pair(std::move(*created), true);
        // The key was used before: the same request again gets the same
        // invoice, a different one is an error
        auto existing = store.byRequestId(*invoice.requestId);
        if (!existing)
            throw std::runtime_error("Invoice of request_id " + *invoice.requestId + " not found");
        if (!sameInvoice(*existing, invoice))
            throw std::invalid_argument("request_id was already used for a different invoice");
        return std::pair(std::move(*existing), false);
    });
    // Committed: only now may others learn of it
    if (created && m_onCreated)
        m_onCreated(result);
    return result;
}

std::vector<Invoice> InvoiceService::list() {
    return m_db.inTransaction([](pqxx::work &tx) { return InvoiceStore(tx).list(); });
}
