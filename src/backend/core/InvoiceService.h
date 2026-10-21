#pragma once
#include <backend/core/Database.h>
#include <backend/core/Invoice.h>
#include <vector>

// Business services. Thread-safe: every call runs in its own transaction on a
// pooled connection. Validation errors throw std::invalid_argument.
class InvoiceService {
public:
    explicit InvoiceService(Database &db) : m_db(db) {}
    // With a request ID (a UUID), a repeated creation returns the invoice
    // created the first time; the same key with different data is rejected
    Invoice create(NewInvoice invoice);
    std::vector<Invoice> list();

private:
    Database &m_db;
};
