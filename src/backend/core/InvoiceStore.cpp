#include <backend/core/InvoiceStore.h>
#include <optional>
#include <string>
#include <tuple>

namespace {
const pqxx::prepped kInsert{"invoices_insert"};
const pqxx::prepped kList{"invoices_list"};

// Columns read back from every query, with the total converted to cents
const std::string kColumns = "id, version, customer, customer_tax_id, (total * 100)::bigint";

using Row = std::tuple<long long, int, std::string, std::optional<std::string>, Cents>;

Invoice toInvoice(Row row) {
    auto [id, version, customer, customerTaxId, totalCents] = std::move(row);
    return {id, version, std::move(customer), std::move(customerTaxId), totalCents};
}
}

void InvoiceStore::prepare(pqxx::connection &connection) {
    connection.prepare(kInsert,
        "INSERT INTO invoices (version, customer, customer_tax_id, total) "
        "VALUES (0, $1, $2, $3::numeric / 100) RETURNING " + kColumns);
    connection.prepare(kList,
        "SELECT " + kColumns + " FROM invoices ORDER BY id");
}

Invoice InvoiceStore::insert(const NewInvoice &invoice) {
    const auto row = m_tx.exec(kInsert, pqxx::params{
        invoice.customer, invoice.customerTaxId, invoice.totalCents}).one_row();
    return toInvoice(row.as<long long, int, std::string, std::optional<std::string>, Cents>());
}

std::vector<Invoice> InvoiceStore::list() {
    std::vector<Invoice> invoices;
    for (auto row : m_tx.query<long long, int, std::string, std::optional<std::string>, Cents>(kList))
        invoices.push_back(toInvoice(std::move(row)));
    return invoices;
}
