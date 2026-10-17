#include <client/InvoiceRepository.h>
#include <guita/v1/invoice_service.pb.h>

namespace v1 = guita::v1;

namespace {
constexpr auto kList = "guita.v1.InvoiceService/ListInvoices";
constexpr auto kCreate = "guita.v1.InvoiceService/CreateInvoice";

// The contract's strings are UTF-8
InvoiceRow toRow(const v1::Invoice &invoice) {
    return {double(invoice.id()), QString::fromStdString(invoice.customer()), invoice.has_customer_tax_id(),
            QString::fromStdString(invoice.customer_tax_id()), double(invoice.total_cents())};
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
    request.set_customer(customer.toStdString());
    if (!customerTaxId.isEmpty())
        request.set_customer_tax_id(customerTaxId.toStdString());
    request.set_total_cents(qint64(totalCents));

    m_connection->call<v1::CreateInvoiceResponse>(kCreate, request, this,
        [this](const v1::CreateInvoiceResponse &response) { emit created(toRow(response.invoice())); },
        [this](const QString &error) { emit failed(error); });
}

void InvoiceRepository::list() {
    if (!m_connection) return;
    m_connection->call<v1::ListInvoicesResponse>(kList, v1::ListInvoicesRequest(), this,
        [this](const v1::ListInvoicesResponse &response) {
            QList<InvoiceRow> rows;
            rows.reserve(response.invoices_size());
            for (const auto &invoice : response.invoices())
                rows.append(toRow(invoice));
            emit listed(rows);
        },
        [this](const QString &error) { emit failed(error); });
}
