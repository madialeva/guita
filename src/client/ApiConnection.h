#pragma once
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

class QJsonDocument;
class QNetworkReply;

// Connection to the backend REST API, shared by every repository. The client
// always talks HTTP/JSON and does not know whether the backend is remote or
// embedded in the process; main() configures it before loading the QML.
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
        QUrl baseUrl;
        QByteArray token;    // Bearer token; empty = no Authorization header
        Mode mode = Connected;
    };

    // Must be called before the QML engine creates the singleton
    static void configure(Settings settings);
    static ApiConnection *create(QQmlEngine *, QJSEngine *);

    QUrl baseUrl() const { return m_settings.baseUrl; }
    Mode mode() const { return m_settings.mode; }

    QNetworkReply *get(const QString &path);
    QNetworkReply *post(const QString &path, const QJsonDocument &body);

    // Empty when the reply succeeded; otherwise the backend {"error": ...}
    // message, or Qt's generic one when the body carries none.
    static QString errorMessage(QNetworkReply *reply, const QByteArray &body);

private:
    // Not default-constructible: QML must use create()
    explicit ApiConnection(Settings settings, QObject *parent = nullptr);
    QNetworkRequest request(const QString &path) const;

    Settings m_settings;
    QNetworkAccessManager m_nam;
};
