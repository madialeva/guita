#pragma once
#include <backend/core/Invoice.h>
#include <pqxx/pqxx>
#include <vector>

// Persistence of invoices: the only place with their SQL. Works inside the
// caller's transaction; the service decides where the transaction begins and ends.
class InvoiceStore {
public:
    explicit InvoiceStore(pqxx::work &tx) : m_tx(tx) {}

    // Registers the prepared statements on a new connection
    static void prepare(pqxx::connection &connection);

    Invoice insert(const NewInvoice &invoice);
    std::vector<Invoice> list();

private:
    pqxx::work &m_tx;
};
