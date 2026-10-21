#pragma once
#include <client/ApiTransport.h>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <chrono>
#include <map>
#include <string>

// The web and connected modes: the persistent channel of the backend over
// WebSocket (frames of guita.channel.v1), with calls and subscriptions over
// one connection. It is opened with the first call or subscription and
// authenticated with a hello frame; calls made meanwhile wait for the welcome.
//
// When the connection is lost it reconnects, waiting longer each time
// (initialBackoff, doubled up to maxBackoff, ±20 %), while calls or
// subscriptions remain. Calls not sent yet go out after the welcome; calls in
// flight are sent again only if they are Repeat::Safe, and the others fail;
// subscriptions are opened again, so their messages start over. A call
// without reply after callTimeout fails. A rejected hello fails what is
// pending and does not reconnect until the next call or subscription.
//
// Everything happens in the UI thread, without blocking waits (WASM).
class WebSocketTransport final : public QObject, public ApiTransport {
    Q_OBJECT
public:
    struct Options {
        std::chrono::milliseconds callTimeout{30'000};
        std::chrono::milliseconds initialBackoff{500};
        std::chrono::milliseconds maxBackoff{30'000};
    };

    // url: ws://host:port/api/channel; token: the Bearer token, empty for none
    WebSocketTransport(QUrl url, QByteArray token, Options options, QObject *parent = nullptr);
    WebSocketTransport(QUrl url, QByteArray token) : WebSocketTransport(std::move(url), std::move(token), Options{}) {}
    ~WebSocketTransport() override;

    void call(const QString &procedure, const QByteArray &request, QObject *context, Callback done,
              Repeat repeat) override;
    void subscribe(const QString &procedure, const QByteArray &request, QObject *context,
                   MessageCallback onMessage, Callback onEnd) override;
    void setAvailabilityHandler(AvailabilityCallback handler) override { m_availability = std::move(handler); }

    // Connections opened so far
    int connectionAttempts() const { return m_attempts; }

private:
    enum class State { Closed, Connecting, Greeting, Open };

    struct Call {
        QString procedure;
        QByteArray request;
        QPointer<QObject> context;
        Callback done;
        Repeat repeat = Repeat::Never;
        bool sent = false;
    };

    struct Subscription {
        QString procedure;
        QByteArray request;
        QPointer<QObject> context;
        MessageCallback onMessage;
        Callback onEnd;
        bool sent = false;
    };

    void connectIfNeeded();
    void greet();
    void receive(const QByteArray &message);
    void closed();
    void sendCall(quint64 id, const QString &procedure, const QByteArray &request);
    void complete(quint64 id, const Reply &reply);
    void endSubscription(quint64 id, const Reply &reply);
    void cancel(quint64 id);
    void setAvailable(bool available);
    void send(const std::string &frame);

    const QUrl m_url;
    const QByteArray m_token;
    const Options m_options;
    QWebSocket m_socket;
    QTimer m_reconnect;
    State m_state = State::Closed;
    std::chrono::milliseconds m_backoff;
    // By correlation ID, which grows: waiting calls go out in the order they were made
    std::map<quint64, Call> m_calls;
    std::map<quint64, Subscription> m_subscriptions;
    quint64 m_nextId = 0;
    int m_attempts = 0;
    bool m_available = true;
    AvailabilityCallback m_availability;
};
