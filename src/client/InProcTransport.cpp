#include <client/InProcTransport.h>
#include <backend/api/Backend.h>

InProcTransport::InProcTransport(const Backend &backend, QObject *parent)
    : QObject(parent), m_backend(backend) {}

InProcTransport::~InProcTransport() {
    // Results posted by finished calls are discarded with this object's events
    m_pool.waitForDone();
}

void InProcTransport::call(const QString &procedure, const QByteArray &request, QObject *context, Callback done) {
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

void InProcTransport::finish(quint64 id, const Reply &reply) {
    const auto pending = m_pending.take(id);
    if (pending.context && pending.done)
        pending.done(reply);
}
