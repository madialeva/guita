#pragma once
#include <client/ApiTransport.h>
#include <backend/api/Backend.h>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <unordered_map>

// The standalone mode: calls the embedded backend in process, without network.
// Backend::call() is synchronous (and may wait for PostgreSQL), so it runs on
// a pool thread; the result comes back to the UI thread through the event
// queue. Server events come from the thread that produced them, also through
// the event queue. Pending callbacks live only in the UI thread: the other
// threads carry just an id and the result or message.
class InProcTransport final : public QObject, public ApiTransport {
    Q_OBJECT
public:
    explicit InProcTransport(const Backend &backend, QObject *parent = nullptr);
    // Ends the subscriptions and waits for the calls in flight, so none
    // outlives the transport or the backend
    ~InProcTransport() override;

    // Nothing to lose in process: repeat makes no difference
    void call(const QString &procedure, const QByteArray &request, QObject *context, Callback done,
              Repeat repeat) override;
    void subscribe(const QString &procedure, const QByteArray &request, QObject *context,
                   MessageCallback onMessage, Callback onEnd) override;

private:
    struct Pending {
        QPointer<QObject> context;
        Callback done;
    };

    struct Subscription {
        QPointer<QObject> context;
        MessageCallback onMessage;
        RpcSubscription handle;      // released last: no message after it
    };

    void finish(quint64 id, const Reply &reply);
    void deliver(quint64 id, const QByteArray &payload);

    const Backend &m_backend;
    QThreadPool m_pool;
    QHash<quint64, Pending> m_pending;
    std::unordered_map<quint64, Subscription> m_subscriptions;
    quint64 m_nextId = 0;
};
