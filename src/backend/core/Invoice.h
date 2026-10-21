#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Amounts are integer cents: never floating point for money. The database
// stores them as numeric(12,2); InvoiceStore converts between both.
using Cents = std::int64_t;

struct NewInvoice {
    std::string customer;
    std::optional<std::string> customerTaxId;
    Cents totalCents = 0;
    // Idempotency key (a UUID): creating again with the same key returns the
    // invoice created the first time instead of another one
    std::optional<std::string> requestId;
};

struct Invoice {
    long long id = 0;
    int version = 0;          // optimistic locking: UPDATE ... WHERE id = $1 AND version = $2
    std::string customer;
    std::optional<std::string> customerTaxId;
    Cents totalCents = 0;
};
