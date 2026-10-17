#include <guita/v1/invoice_service.qpb.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProtobufSerializer>
#include <QString>
#include <QVariantList>
#include <algorithm>
#include <functional>
#include <vector>

// Size and client-side coding time of an invoice list, as JSON (the canonical
// proto3 JSON the API also serves), compressed JSON and binary Protobuf.
// Decoding compares the former client path (JSON to QVariantList) with the
// current one (binary to Qt Protobuf messages). Runs natively and in WebAssembly.
namespace v1 = qtc::guita::v1;

namespace {
v1::ListInvoicesResponse makeInvoices(int count) {
    QList<v1::Invoice> invoices;
    invoices.reserve(count);
    for (int i = 1; i <= count; ++i) {
        v1::Invoice invoice;
        invoice.setId_proto(i);
        invoice.setCustomer(QStringLiteral("Customer %1 S.L.").arg(i));
        if (i % 2) invoice.setCustomerTaxId(QStringLiteral("B%1").arg(i, 8, 10, QLatin1Char('0')));
        invoice.setTotalCents(qint64(i) * 1237 % 99'999'999);
        invoices.append(invoice);
    }
    v1::ListInvoicesResponse response;
    response.setInvoices(invoices);
    return response;
}

QJsonDocument toJson(const v1::ListInvoicesResponse &response) {
    QJsonArray invoices;
    for (const auto &invoice : response.invoices()) {
        QJsonObject object{{"id", QString::number(invoice.id_proto())},
                           {"customer", invoice.customer()},
                           {"totalCents", QString::number(invoice.totalCents())}};
        if (invoice.hasCustomerTaxId())
            object.insert("customerTaxId", invoice.customerTaxId());
        invoices.append(object);
    }
    return QJsonDocument(QJsonObject{{"invoices", invoices}});
}

// Median time of fn in milliseconds
double medianMs(int repetitions, const std::function<void()> &fn) {
    std::vector<double> samples;
    QElapsedTimer timer;
    for (int i = 0; i < repetitions; ++i) {
        timer.start();
        fn();
        samples.push_back(timer.nsecsElapsed() / 1e6);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

qsizetype compressedSize(const QByteArray &data) {
    return qCompress(data).size() - 4;   // without qCompress's length prefix: zlib stream
}
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QProtobufSerializer serializer;
    volatile qsizetype sink = 0;

    qInfo("%-8s %-10s %10s %12s %10s %10s", "invoices", "format", "bytes", "compressed", "encode ms", "decode ms");
    for (const int count : {1000, 10000}) {
        const auto response = makeInvoices(count);
        const int repetitions = count == 1000 ? 50 : 15;

        const auto json = toJson(response).toJson(QJsonDocument::Compact);
        const double jsonEncode = medianMs(repetitions, [&] { sink = toJson(response).toJson(QJsonDocument::Compact).size(); });
        const double jsonDecode = medianMs(repetitions, [&] {
            sink = QJsonDocument::fromJson(json).object().value("invoices").toArray().toVariantList().size();
        });

        const auto binary = response.serialize(&serializer);
        const double binaryEncode = medianMs(repetitions, [&] { sink = response.serialize(&serializer).size(); });
        const double binaryDecode = medianMs(repetitions, [&] {
            v1::ListInvoicesResponse decoded;
            decoded.deserialize(&serializer, binary);
            sink = decoded.invoices().size();
        });

        qInfo("%-8d %-10s %10lld %12lld %10.2f %10.2f", count, "json", qlonglong(json.size()),
              qlonglong(compressedSize(json)), jsonEncode, jsonDecode);
        qInfo("%-8d %-10s %10lld %12lld %10.2f %10.2f", count, "protobuf", qlonglong(binary.size()),
              qlonglong(compressedSize(binary)), binaryEncode, binaryDecode);
    }
    return 0;
}
