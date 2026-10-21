#include <client/WebSocketTransport.h>
#include <guita/channel/v1/channel.pb.h>
#include <QRandomGenerator>
#include <algorithm>
#include <utility>
#include <vector>

namespace channel = guita::channel::v1;

WebSocketTransport::WebSocketTransport(QUrl url, QByteArray token, Options options, QObject *parent)
    : QObject(parent), m_url(std::move(url)), m_token(std::move(token)), m_options(options),
      m_backoff(options.initialBackoff) {
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &WebSocketTransport::connectIfNeeded);
    connect(&m_socket, &QWebSocket::connected, this, &WebSocketTransport::greet);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &WebSocketTransport::receive);
    // A failed attempt may end without disconnected(): the state says it
    connect(&m_socket, &QWebSocket::disconnected, this, &WebSocketTransport::closed);
    connect(&m_socket, &QWebSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState) closed();
    });
}

WebSocketTransport::~WebSocketTransport() {
    // The socket is destroyed after this object's members: nothing may come back
    m_reconnect.stop();
    QObject::disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}

void WebSocketTransport::call(const QString &procedure, const QByteArray &request, QObject *context, Callback done,
                              Repeat repeat) {
    const auto id = ++m_nextId;
    m_calls.emplace(id, Call{procedure, request, context, std::move(done), repeat});
    QTimer::singleShot(m_options.callTimeout, this, [this, id] { complete(id, {false, {}, tr("Backend unavailable")}); });
    if (m_state == State::Open) {
        sendCall(id, procedure, request);
        m_calls.at(id).sent = true;
    } else {
        connectIfNeeded();
    }
}

void WebSocketTransport::subscribe(const QString &procedure, const QByteArray &request, QObject *context,
                                   MessageCallback onMessage, Callback onEnd) {
    const auto id = ++m_nextId;
    m_subscriptions.emplace(id, Subscription{procedure, request, context, std::move(onMessage), std::move(onEnd)});
    connect(context, &QObject::destroyed, this, [this, id] { cancel(id); });
    if (m_state == State::Open) {
        sendCall(id, procedure, request);
        m_subscriptions.at(id).sent = true;
    } else {
        connectIfNeeded();
    }
}

void WebSocketTransport::connectIfNeeded() {
    // While waiting to reconnect, new calls wait too
    if (m_state != State::Closed || m_reconnect.isActive()) return;
    if (m_calls.empty() && m_subscriptions.empty()) return;
    m_state = State::Connecting;
    ++m_attempts;
    m_socket.open(m_url);
}

void WebSocketTransport::greet() {
    m_state = State::Greeting;
    channel::ClientFrame frame;
    frame.mutable_hello()->set_authorization(m_token.isEmpty() ? std::string() : "Bearer " + m_token.toStdString());
    send(frame.SerializeAsString());
}

void WebSocketTransport::receive(const QByteArray &message) {
    channel::ServerFrame frame;
    if (!frame.ParseFromArray(message.constData(), int(message.size()))) {
        m_socket.close(QWebSocketProtocol::CloseCodeProtocolError);
        return;
    }
    switch (frame.frame_case()) {
    case channel::ServerFrame::kWelcome:
        m_state = State::Open;
        m_backoff = m_options.initialBackoff;
        setAvailable(true);
        for (auto &[id, call] : m_calls) {
            if (call.sent) continue;
            sendCall(id, call.procedure, call.request);
            call.sent = true;
        }
        for (auto &[id, subscription] : m_subscriptions) {
            if (subscription.sent) continue;
            sendCall(id, subscription.procedure, subscription.request);
            subscription.sent = true;
        }
        return;
    case channel::ServerFrame::kReply: {
        const auto &reply = frame.reply();
        const Reply result = reply.result_case() == channel::Reply::kError
            ? Reply{false, {}, QString::fromStdString(reply.error().message())}
            : Reply{true, QByteArray::fromStdString(reply.payload()), {}};
        if (m_calls.contains(reply.correlation_id()))
            complete(reply.correlation_id(), result);
        else if (m_subscriptions.contains(reply.correlation_id()))
            endSubscription(reply.correlation_id(), result);
        return;
    }
    case channel::ServerFrame::kMessage: {
        const auto subscription = m_subscriptions.find(frame.message().correlation_id());
        if (subscription == m_subscriptions.end() || !subscription->second.context) return;
        // A copy: the callback may end subscriptions, this one included
        const auto onMessage = subscription->second.onMessage;
        onMessage(QByteArray::fromStdString(frame.message().payload()));
        return;
    }
    case channel::ServerFrame::kGoodbye: {
        // A rejected hello: what is pending fails, and nothing remains to reconnect for
        if (frame.goodbye().error().code() != "unauthenticated") return;
        const Reply rejected{false, {}, QString::fromStdString(frame.goodbye().error().message())};
        std::vector<quint64> ids;
        for (const auto &[id, call] : m_calls) ids.push_back(id);
        for (const auto id : ids) complete(id, rejected);
        ids.clear();
        for (const auto &[id, subscription] : m_subscriptions) ids.push_back(id);
        for (const auto id : ids) endSubscription(id, rejected);
        return;
    }
    default:
        return;    // a frame of a later version
    }
}

void WebSocketTransport::closed() {
    if (m_state == State::Closed) return;
    m_state = State::Closed;
    setAvailable(false);

    std::vector<quint64> lost;
    for (auto &[id, call] : m_calls) {
        if (!call.sent) continue;
        if (call.repeat == Repeat::Safe)
            call.sent = false;           // sent again after the next welcome
        else
            lost.push_back(id);
    }
    for (const auto id : lost)
        complete(id, {false, {}, tr("Connection to the backend lost")});
    for (auto &[id, subscription] : m_subscriptions)
        subscription.sent = false;

    if (m_calls.empty() && m_subscriptions.empty()) return;
    // ±20 %, so that clients cut off together do not come back together
    const double jitter = 0.8 + 0.4 * QRandomGenerator::global()->generateDouble();
    m_reconnect.start(std::chrono::milliseconds(qint64(double(m_backoff.count()) * jitter)));
    m_backoff = std::min(m_backoff * 2, m_options.maxBackoff);
}

void WebSocketTransport::sendCall(quint64 id, const QString &procedure, const QByteArray &request) {
    channel::ClientFrame frame;
    frame.mutable_call()->set_correlation_id(id);
    frame.mutable_call()->set_procedure(procedure.toStdString());
    frame.mutable_call()->set_payload(request.toStdString());
    send(frame.SerializeAsString());
}

void WebSocketTransport::complete(quint64 id, const Reply &reply) {
    auto node = m_calls.extract(id);
    if (node.empty()) return;          // answered already, or timed out
    if (node.mapped().context && node.mapped().done)
        node.mapped().done(reply);
}

void WebSocketTransport::endSubscription(quint64 id, const Reply &reply) {
    auto node = m_subscriptions.extract(id);
    if (node.empty()) return;
    if (node.mapped().context && node.mapped().onEnd)
        node.mapped().onEnd(reply);
}

void WebSocketTransport::cancel(quint64 id) {
    const auto subscription = m_subscriptions.find(id);
    if (subscription == m_subscriptions.end()) return;
    if (subscription->second.sent && m_state == State::Open) {
        channel::ClientFrame frame;
        frame.mutable_cancel()->set_correlation_id(id);
        send(frame.SerializeAsString());
    }
    m_subscriptions.erase(subscription);
}

void WebSocketTransport::setAvailable(bool available) {
    if (available == m_available) return;
    m_available = available;
    if (m_availability) m_availability(available);
}

void WebSocketTransport::send(const std::string &frame) {
    m_socket.sendBinaryMessage(QByteArray::fromStdString(frame));
}
