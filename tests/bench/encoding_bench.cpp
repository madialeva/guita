#include <guita/channel/v1/channel.pb.h>
#include <guita/v1/invoice_service.pb.h>
#include <support/ProtoSamples.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <algorithm>
#include <functional>
#include <vector>

// Size and coding time of an invoice list as JSON (the canonical proto3 JSON
// of the contract), compressed JSON and binary Protobuf with the contract's
// code, the same on the backend and the client. Decoding compares a JSON
// client (to QVariantList) with the binary one. Runs natively and in
// WebAssembly, after checking the reference bytes of tests/support/ProtoSamples.h.
namespace v1 = guita::v1;
namespace channel = guita::channel::v1;

namespace {
v1::ListInvoicesResponse makeInvoices(int count) {
    v1::ListInvoicesResponse response;
    for (int i = 1; i <= count; ++i) {
        auto *invoice = response.add_invoices();
        invoice->set_id(i);
        invoice->set_customer(QStringLiteral("Customer %1 S.L.").arg(i).toStdString());
        if (i % 2) invoice->set_customer_tax_id(QStringLiteral("B%1").arg(i, 8, 10, QLatin1Char('0')).toStdString());
        invoice->set_total_cents(qint64(i) * 1237 % 99'999'999);
    }
    return response;
}

QJsonDocument toJson(const v1::ListInvoicesResponse &response) {
    QJsonArray invoices;
    for (const auto &invoice : response.invoices()) {
        QJsonObject object{{"id", QString::number(invoice.id())},
                           {"customer", QString::fromStdString(invoice.customer())},
                           {"totalCents", QString::number(invoice.total_cents())}};
        if (invoice.has_customer_tax_id())
            object.insert("customerTaxId", QString::fromStdString(invoice.customer_tax_id()));
        invoices.append(object);
    }
    return QJsonDocument(QJsonObject{{"invoices", invoices}});
}

// Median time of fn in milliseconds, over batches of `inner` calls (browser
// timers are coarse)
double medianMs(int repetitions, int inner, const std::function<void()> &fn) {
    std::vector<double> samples;
    QElapsedTimer timer;
    for (int i = 0; i < repetitions; ++i) {
        timer.start();
        for (int j = 0; j < inner; ++j) fn();
        samples.push_back(timer.nsecsElapsed() / 1e6 / inner);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

qsizetype compressedSize(const QByteArray &data) {
    return qCompress(data).size() - 4;   // without qCompress's length prefix: zlib stream
}

// The reference bytes, read and written again, must come out unchanged: this
// platform writes and reads exactly what the other ones do
template <class Message>
bool unchanged(std::string_view hex) {
    const auto bytes = protoSamples::fromHex(hex);
    Message message;
    return message.ParseFromString(bytes) && message.SerializeAsString() == bytes;
}

bool checkReferenceBytes() {
    using namespace protoSamples;
    const bool ok = unchanged<v1::CreateInvoiceRequest>(kCreateInvoiceRequest)
                 && unchanged<v1::CreateInvoiceRequest>(kCreateInvoiceRequestWithTaxId)
                 && unchanged<v1::CreateInvoiceRequest>(kCreateInvoiceRequestEmptyTaxId)
                 && unchanged<v1::CreateInvoiceRequest>(kCreateInvoiceRequestNegative)
                 && unchanged<v1::CreateInvoiceResponse>(kCreateInvoiceResponse)
                 && unchanged<v1::ListInvoicesResponse>(kListInvoicesResponseLimits)
                 && unchanged<v1::CreateInvoiceRequest>(kCreateInvoiceRequestWithRequestId)
                 && unchanged<v1::WatchInvoicesResponse>(kWatchInvoicesStarted)
                 && unchanged<v1::WatchInvoicesResponse>(kWatchInvoicesCreated)
                 && unchanged<channel::ClientFrame>(kClientHello)
                 && unchanged<channel::ClientFrame>(kClientCall)
                 && unchanged<channel::ClientFrame>(kClientCancel)
                 && unchanged<channel::ServerFrame>(kServerWelcome)
                 && unchanged<channel::ServerFrame>(kServerReply)
                 && unchanged<channel::ServerFrame>(kServerReplyError)
                 && unchanged<channel::ServerFrame>(kServerStreamMessage)
                 && unchanged<channel::ServerFrame>(kServerGoodbye);
    if (ok)
        qInfo("Protobuf reference bytes: identical");
    else
        qCritical("Protobuf reference bytes differ");
    return ok;
}
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (!checkReferenceBytes())
        return 1;
    volatile qsizetype sink = 0;

    qInfo("%-8s %-10s %10s %12s %10s %10s", "invoices", "format", "bytes", "compressed", "encode ms", "decode ms");
    for (const int count : {1000, 10000}) {
        const auto response = makeInvoices(count);
        const int repetitions = 15;
        const int inner = count == 1000 ? 10 : 2;

        const auto json = toJson(response).toJson(QJsonDocument::Compact);
        const double jsonEncode = medianMs(repetitions, inner, [&] { sink = toJson(response).toJson(QJsonDocument::Compact).size(); });
        const double jsonDecode = medianMs(repetitions, inner, [&] {
            sink = QJsonDocument::fromJson(json).object().value("invoices").toArray().toVariantList().size();
        });

        const auto binary = QByteArray::fromStdString(response.SerializeAsString());
        const double binaryEncode = medianMs(repetitions, inner, [&] { sink = qsizetype(response.SerializeAsString().size()); });
        const double binaryDecode = medianMs(repetitions, inner, [&] {
            v1::ListInvoicesResponse decoded;
            sink = decoded.ParseFromArray(binary.constData(), int(binary.size())) ? decoded.invoices_size() : -1;
        });

        qInfo("%-8d %-10s %10lld %12lld %10.3f %10.3f", count, "json", qlonglong(json.size()),
              qlonglong(compressedSize(json)), jsonEncode, jsonDecode);
        qInfo("%-8d %-10s %10lld %12lld %10.3f %10.3f", count, "protobuf", qlonglong(binary.size()),
              qlonglong(compressedSize(binary)), binaryEncode, binaryDecode);
    }
    return 0;
}
