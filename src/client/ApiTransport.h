#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

// How ApiConnection reaches the backend: HTTP (HttpTransport) or in process
// (InProcTransport, standalone mode). One asynchronous operation: send the
// encoded request of a procedure and get, later and in the UI thread, the
// encoded response or an error message. The rest of the client never knows
// which transport carries the call.
class ApiTransport {
public:
    struct Reply {
        bool ok = false;
        QByteArray payload;     // encoded response message when ok
        QString error;          // the backend's error message otherwise
    };
    using Callback = std::function<void(const Reply &)>;

    virtual ~ApiTransport() = default;

    // done runs in the UI thread, and not at all if context is destroyed first.
    // procedure: "guita.v1.InvoiceService/CreateInvoice"
    virtual void call(const QString &procedure, const QByteArray &request,
                      QObject *context, Callback done) = 0;
};
