#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

// How ApiConnection reaches the backend: the persistent channel
// (WebSocketTransport) or in process (InProcTransport, standalone mode). Two
// asynchronous operations: a call (send the encoded request of a procedure
// and get, later and in the UI thread, the encoded response or an error
// message) and a subscription to a stream procedure (server events). The rest
// of the client never knows which transport carries them.
class ApiTransport {
public:
    struct Reply {
        bool ok = false;
        QByteArray payload;     // encoded response message when ok
        QString error;          // the backend's error message otherwise
    };
    using Callback = std::function<void(const Reply &)>;
    using MessageCallback = std::function<void(const QByteArray &)>;
    using AvailabilityCallback = std::function<void(bool)>;

    // Whether a call may be sent again when the connection is lost before its
    // reply: only reads and operations with an idempotency key (request_id).
    // Never by default, so that nothing is applied twice by mistake.
    enum class Repeat { Never, Safe };

    virtual ~ApiTransport() = default;

    // done runs in the UI thread, and not at all if context is destroyed first.
    // procedure: "guita.v1.InvoiceService/CreateInvoice"
    virtual void call(const QString &procedure, const QByteArray &request, QObject *context,
                      Callback done, Repeat repeat) = 0;

    // onMessage gets each encoded message of the stream in the UI thread,
    // while context lives; destroying context ends the subscription. onEnd
    // reports an error that ends it on the backend's side. After a lost
    // connection the subscription is opened again, so its messages start over.
    virtual void subscribe(const QString &procedure, const QByteArray &request, QObject *context,
                           MessageCallback onMessage, Callback onEnd) = 0;

    // Called in the UI thread whenever the backend becomes reachable or not.
    // Transports that cannot lose the backend never call it.
    virtual void setAvailabilityHandler(AvailabilityCallback) {}
};
