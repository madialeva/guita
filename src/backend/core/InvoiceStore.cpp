#include <backend/core/InvoiceStore.h>
#include <optional>
#include <string>
#include <tuple>

namespace {
const pqxx::prepped kInsert{"invoices_insert"};
const pqxx::prepped kByRequestId{"invoices_by_request_id"};
const pqxx::prepped kList{"invoices_list"};

// Columns read back from every query, with the total converted to cents
const std::string kColumns = "id, version, customer, customer_tax_id, (total * 100)::bigint";

using Row = std::tuple<long long, int, std::string, std::optional<std::string>, Cents>;

Invoice toInvoice(Row row) {
    auto [id, version, customer, customerTaxId, totalCents] = std::move(row);
    return {id, version, std::move(customer), std::move(customerTaxId), totalCents};
}

std::optional<Invoice> toInvoice(const std::optional<pqxx::row> &row) {
    if (!row) return std::nullopt;
    return toInvoice(row->as<long long, int, std::string, std::optional<std::string>, Cents>());
}
}

void InvoiceStore::prepare(pqxx::connection &connection) {
    // A request ID already used inserts nothing (NULLs never conflict). A
    // concurrent insert of the same key waits for the other transaction and
    // then inserts nothing, so the key's invoice is read afterwards
    connection.prepare(kInsert,
        "INSERT INTO invoices (version, customer, customer_tax_id, total, request_id) "
        "VALUES (0, $1, $2, $3::numeric / 100, $4::uuid) "
        "ON CONFLICT (request_id) DO NOTHING RETURNING " + kColumns);
    connection.prepare(kByRequestId,
        "SELECT " + kColumns + " FROM invoices WHERE request_id = $1::uuid");
    connection.prepare(kList,
        "SELECT " + kColumns + " FROM invoices ORDER BY id");
}

std::optional<Invoice> InvoiceStore::insert(const NewInvoice &invoice) {
    return toInvoice(m_tx.exec(kInsert, pqxx::params{
        invoice.customer, invoice.customerTaxId, invoice.totalCents, invoice.requestId}).opt_row());
}

std::optional<Invoice> InvoiceStore::byRequestId(const std::string &requestId) {
    return toInvoice(m_tx.exec(kByRequestId, pqxx::params{requestId}).opt_row());
}

std::vector<Invoice> InvoiceStore::list() {
    std::vector<Invoice> invoices;
    for (auto row : m_tx.query<long long, int, std::string, std::optional<std::string>, Cents>(kList))
        invoices.push_back(toInvoice(std::move(row)));
    return invoices;
}
