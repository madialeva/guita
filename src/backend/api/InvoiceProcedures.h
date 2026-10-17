#pragma once
#include <backend/api/RpcRouter.h>
#include <backend/core/InvoiceService.h>

// guita.v1.InvoiceService on the router: converts between the contract's
// messages and the domain structs of core, which knows nothing of the contract.
void addInvoiceProcedures(RpcRouter &router, InvoiceService &service);
