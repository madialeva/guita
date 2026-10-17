#pragma once
#include <client/ApiTransport.h>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThreadPool>

class Backend;

// The standalone mode: calls the embedded backend in process, without network.
// Backend::call() is synchronous (and may wait for PostgreSQL), so it runs on
// a pool thread; the result comes back to the UI thread through the event
// queue. Pending callbacks live only in the UI thread: the pool threads carry
// just a call id and the result.
class InProcTransport final : public QObject, public ApiTransport {
    Q_OBJECT
public:
    explicit InProcTransport(const Backend &backend, QObject *parent = nullptr);
    // Waits for the calls in flight, so none outlives the transport or the backend
    ~InProcTransport() override;

    void call(const QString &procedure, const QByteArray &request, QObject *context, Callback done) override;

private:
    struct Pending {
        QPointer<QObject> context;
        Callback done;
    };

    void finish(quint64 id, const Reply &reply);

    const Backend &m_backend;
    QThreadPool m_pool;
    QHash<quint64, Pending> m_pending;
    quint64 m_nextId = 0;
};
