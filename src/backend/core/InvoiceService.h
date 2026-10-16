#pragma once
#include <backend/core/Database.h>
#include <backend/core/Invoice.h>
#include <vector>

// Business services. Thread-safe: every call runs in its own transaction on a
// pooled connection. Validation errors throw std::invalid_argument.
class InvoiceService {
public:
    explicit InvoiceService(Database &db) : m_db(db) {}
    Invoice create(NewInvoice invoice);
    std::vector<Invoice> list();

private:
    Database &m_db;
};
