#include <client/InvoiceRepository.h>
#include <guita/v1/invoice_service.qpb.h>

namespace v1 = qtc::guita::v1;

namespace {
constexpr auto kList = "guita.v1.InvoiceService/ListInvoices";
constexpr auto kCreate = "guita.v1.InvoiceService/CreateInvoice";

InvoiceRow toRow(const v1::Invoice &invoice) {
    // customerTaxId() requires the optional field to be present
    const bool hasTaxId = invoice.hasCustomerTaxId();
    return {double(invoice.id_proto()), invoice.customer(), hasTaxId,
            hasTaxId ? invoice.customerTaxId() : QString(), double(invoice.totalCents())};
}
}

void InvoiceRepository::setConnection(ApiConnection *connection) {
    if (m_connection == connection) return;
    m_connection = connection;
    emit connectionChanged();
}

void InvoiceRepository::create(const QString &customer, const QString &customerTaxId, double totalCents) {
    if (!m_connection) return;
    v1::CreateInvoiceRequest request;
    request.setCustomer(customer);
    if (!customerTaxId.isEmpty())
        request.setCustomerTaxId(customerTaxId);
    request.setTotalCents(qint64(totalCents));

    m_connection->call<v1::CreateInvoiceResponse>(kCreate, request, this,
        [this](const v1::CreateInvoiceResponse &response) { emit created(toRow(response.invoice())); },
        [this](const QString &error) { emit failed(error); });
}

void InvoiceRepository::list() {
    if (!m_connection) return;
    m_connection->call<v1::ListInvoicesResponse>(kList, v1::ListInvoicesRequest(), this,
        [this](const v1::ListInvoicesResponse &response) {
            QList<InvoiceRow> rows;
            rows.reserve(response.invoices().size());
            for (const auto &invoice : response.invoices())
                rows.append(toRow(invoice));
            emit listed(rows);
        },
        [this](const QString &error) { emit failed(error); });
}
