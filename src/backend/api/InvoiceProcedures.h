#pragma once
#include <backend/api/EventTopic.h>
#include <backend/api/RpcRouter.h>
#include <backend/core/InvoiceService.h>
#include <guita/v1/invoice_service.pb.h>

// The events of guita.v1.InvoiceService/WatchInvoices
using InvoiceEvents = EventTopic<guita::v1::WatchInvoicesResponse>;

// The InvoiceService notification that publishes each created invoice on events
InvoiceService::CreatedHandler publishCreatedInvoices(InvoiceEvents &events);

// guita.v1.InvoiceService on the router: converts between the contract's
// messages and the domain structs of core, which knows nothing of the contract.
void addInvoiceProcedures(RpcRouter &router, InvoiceService &service, InvoiceEvents &events);
