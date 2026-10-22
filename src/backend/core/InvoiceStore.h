#pragma once
#include <backend/core/Invoice.h>
#include <pqxx/pqxx>
#include <optional>
#include <string>
#include <vector>

// Persistence of invoices: the only place with their SQL. Works inside the
// caller's transaction; the service decides where the transaction begins and ends.
class InvoiceStore {
public:
    explicit InvoiceStore(pqxx::work &tx) : m_tx(tx) {}

    // Registers the prepared statements on a new connection
    static void prepare(pqxx::connection &connection);

    // Inserts the invoice. With a request ID already used, inserts nothing and
    // returns std::nullopt: the invoice of that key is read with byRequestId()
    std::optional<Invoice> insert(const NewInvoice &invoice);
    std::optional<Invoice> byRequestId(const std::string &requestId);
    std::vector<Invoice> list();

private:
    pqxx::work &m_tx;
};
