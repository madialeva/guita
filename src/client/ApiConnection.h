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
// transport carries the calls (HTTP, or in process in standalone mode);
// main() configures it before loading the QML.
class ApiConnection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QUrl baseUrl READ baseUrl CONSTANT)
    Q_PROPERTY(Mode mode READ mode CONSTANT)

public:
    enum Mode { Web, Connected, Standalone };
    Q_ENUM(Mode)

    struct Settings {
        Mode mode = Connected;
        QUrl baseUrl;                             // HTTP modes; empty in process
        std::shared_ptr<ApiTransport> transport;
    };

    // Must be called before the QML engine creates the singleton
    static void configure(Settings settings);
    static ApiConnection *create(QQmlEngine *, QJSEngine *);

    QUrl baseUrl() const { return m_settings.baseUrl; }
    Mode mode() const { return m_settings.mode; }

    // Calls a procedure of the contract ("guita.v1.InvoiceService/CreateInvoice"):
    // encodes the request and later decodes the response into Response for
    // onResponse, or reports the error to onError; both run in the UI thread
    // and only while context is alive.
    template <class Response>
    void call(const QString &procedure, const google::protobuf::MessageLite &request, QObject *context,
              std::function<void(const Response &)> onResponse,
              std::function<void(const QString &)> onError) {
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
            });
    }

private:
    // Not default-constructible: QML must use create()
    explicit ApiConnection(Settings settings, QObject *parent = nullptr);

    Settings m_settings;
};
