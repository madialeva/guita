#pragma once
#include <backend/api/Authorizer.h>
#include <backend/core/InvoiceService.h>
#include <httplib.h>

// /api/invoices
//   GET  -> 200 [{id, customer, customerTaxId, totalCents}, ...]
//   POST {customer, customerTaxId, totalCents} -> 201 {id, customer, customerTaxId, totalCents}
void addInvoiceRoutes(httplib::Server &server, InvoiceService &service, const Authorizer &authorizer);
