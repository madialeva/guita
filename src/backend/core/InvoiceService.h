#pragma once
#include <backend/core/Database.h>
#include <backend/core/Invoice.h>
#include <functional>
#include <utility>
#include <vector>

// Business services. Thread-safe: every call runs in its own transaction on a
// pooled connection. Validation errors throw std::invalid_argument.
class InvoiceService {
public:
    // Called with every invoice created, once its transaction is committed
    // (never for a repeated request nor a failed one), in the caller's thread
    using CreatedHandler = std::function<void(const Invoice &)>;

    explicit InvoiceService(Database &db, CreatedHandler onCreated = {})
        : m_db(db), m_onCreated(std::move(onCreated)) {}
    // With a request ID (a UUID), a repeated creation returns the invoice
    // created the first time; the same key with different data is rejected
    Invoice create(NewInvoice invoice);
    std::vector<Invoice> list();

private:
    Database &m_db;
    CreatedHandler m_onCreated;
};
