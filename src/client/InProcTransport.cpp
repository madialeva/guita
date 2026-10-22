#include <client/InProcTransport.h>
#include <backend/api/Backend.h>
#include <string>
#include <utility>

InProcTransport::InProcTransport(const Backend &backend, QObject *parent)
    : QObject(parent), m_backend(backend) {}

InProcTransport::~InProcTransport() {
    // Once released, no subscription delivers anything more; messages
    // already posted are discarded with this object's events
    m_subscriptions.clear();
    m_pool.waitForDone();
}

void InProcTransport::call(const QString &procedure, const QByteArray &request, QObject *context, Callback done,
                           Repeat) {
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, std::move(done)});

    RpcCall rpc{procedure.toStdString(), request.toStdString(), {}};
    m_pool.start([this, id, rpc = std::move(rpc)] {
        const auto result = m_backend.call(rpc);
        Reply reply{result.code == RpcCode::Ok,
                    QByteArray::fromStdString(result.payload),
                    QString::fromStdString(result.message)};
        QMetaObject::invokeMethod(this, [this, id, reply = std::move(reply)] { finish(id, reply); },
                                  Qt::QueuedConnection);
    });
}

void InProcTransport::subscribe(const QString &procedure, const QByteArray &request, QObject *context,
                                MessageCallback onMessage, Callback onEnd) {
    const auto id = ++m_nextId;
    // In the thread that publishes; the first message may come right now
    auto handle = m_backend.subscribe({procedure.toStdString(), request.toStdString(), {}},
        [this, id](std::string payload) {
            QMetaObject::invokeMethod(this, [this, id, payload = QByteArray::fromStdString(payload)] {
                deliver(id, payload);
            }, Qt::QueuedConnection);
        });
    if (!handle.active()) {
        // Reported later, like every other result
        Reply reply{false, {}, QString::fromStdString(handle.result().message)};
        QMetaObject::invokeMethod(this, [context = QPointer(context), onEnd = std::move(onEnd), reply] {
            if (context) onEnd(reply);
        }, Qt::QueuedConnection);
        return;
    }
    m_subscriptions.emplace(id, Subscription{context, std::move(onMessage), std::move(handle)});
    connect(context, &QObject::destroyed, this, [this, id] { m_subscriptions.erase(id); });
}

void InProcTransport::finish(quint64 id, const Reply &reply) {
    const auto pending = m_pending.take(id);
    if (pending.context && pending.done)
        pending.done(reply);
}

void InProcTransport::deliver(quint64 id, const QByteArray &payload) {
    const auto subscription = m_subscriptions.find(id);
    if (subscription == m_subscriptions.end()) return;
    if (!subscription->second.context) {
        m_subscriptions.erase(subscription);
        return;
    }
    // A copy: the callback may end subscriptions, this one included
    const auto onMessage = subscription->second.onMessage;
    onMessage(payload);
}
