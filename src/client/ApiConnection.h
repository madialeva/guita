#pragma once
#include <client/ApiTransport.h>
#include <google/protobuf/message_lite.h>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <functional>
#include <memory>

// Connection to the backend, shared by every repository. The client always
// talks to the backend through the RPC contract and never knows which
// transport carries the calls and the events (the persistent channel, or in
// process in standalone mode); main() configures it before loading the QML.
class ApiConnection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QUrl url READ url CONSTANT)
    Q_PROPERTY(Mode mode READ mode CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    enum Mode { Web, Connected, Standalone };
    Q_ENUM(Mode)

    using Repeat = ApiTransport::Repeat;

    struct Settings {
        Mode mode = Connected;
        QUrl url;                                 // the channel's in web and connected; empty in process
        std::shared_ptr<ApiTransport> transport;
    };

    // Must be called before the QML engine creates the singleton
    static void configure(Settings settings);
    static ApiConnection *create(QQmlEngine *, QJSEngine *);
    ~ApiConnection() override;

    QUrl url() const { return m_settings.url; }
    Mode mode() const { return m_settings.mode; }
    // Whether the backend is reachable now; calls made meanwhile wait for it
    bool available() const { return m_available; }

    // Calls a procedure of the contract ("guita.v1.InvoiceService/CreateInvoice"):
    // encodes the request and later decodes the response into Response for
    // onResponse, or reports the error to onError; both run in the UI thread
    // and only while context is alive. repeat says whether the call may be
    // sent again after a lost connection (reads, and operations with a request_id).
    template <class Response>
    void call(const QString &procedure, const google::protobuf::MessageLite &request, QObject *context,
              std::function<void(const Response &)> onResponse,
              std::function<void(const QString &)> onError, Repeat repeat = Repeat::Never) {
        m_settings.transport->call(procedure, QByteArray::fromStdString(request.SerializeAsString()), context,
            [onResponse = std::move(onResponse), onError = std::move(onError)](const ApiTransport::Reply &reply) {
                if (!reply.ok) {
                    onError(reply.error);
                    return;
                }
                Response response;
                if (!response.ParseFromArray(reply.payload.constData(), int(reply.payload.size()))) {
                    onError(tr("Unexpected response from the backend"));
                    return;
                }
                onResponse(response);
            }, repeat);
    }

    // Subscribes to a stream procedure of the contract (server events): each
    // message is decoded into Response for onMessage, in the UI thread, while
    // context is alive; destroying context ends the subscription. onError
    // reports an error that ends it.
    template <class Response>
    void subscribe(const QString &procedure, const google::protobuf::MessageLite &request, QObject *context,
                   std::function<void(const Response &)> onMessage, std::function<void(const QString &)> onError) {
        m_settings.transport->subscribe(procedure, QByteArray::fromStdString(request.SerializeAsString()), context,
            [onMessage = std::move(onMessage)](const QByteArray &payload) {
                Response response;
                // A message this version cannot read is skipped, not the subscription
                if (response.ParseFromArray(payload.constData(), int(payload.size())))
                    onMessage(response);
            },
            [onError = std::move(onError)](const ApiTransport::Reply &reply) { onError(reply.error); });
    }

signals:
    void availableChanged();

private:
    // Not default-constructible: QML must use create()
    explicit ApiConnection(Settings settings, QObject *parent = nullptr);

    Settings m_settings;
    bool m_available = true;
};
