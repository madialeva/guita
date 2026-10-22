#pragma once
#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>
#include <utility>

// A TCP proxy on loopback between a client and a server, run by the test's
// event loop, that cuts connections at will: it can hold back what the
// server sends (so that a request reaches the server and its reply does not
// reach the client), cut every connection through it, and refuse new ones
// (closing them at once) as an unreachable server would.
class CuttingProxy : public QObject {
public:
    explicit CuttingProxy(quint16 serverPort, QObject *parent = nullptr)
        : QObject(parent), m_serverPort(serverPort) {
        m_clock.start();
        m_listener.listen(QHostAddress::LocalHost);
        QObject::connect(&m_listener, &QTcpServer::newConnection, this, [this] { accept(); });
    }

    quint16 port() const { return m_listener.serverPort(); }

    // While true, what the server sends is kept instead of forwarded; cut() drops it
    void holdDownstream(bool hold) {
        m_hold = hold;
        if (hold) return;
        for (const auto &pair : m_pairs)
            if (pair->client && !pair->held.isEmpty()) pair->client->write(std::exchange(pair->held, {}));
    }

    // Closes every connection through the proxy, dropping what was held
    void cut() {
        for (const auto &pair : std::exchange(m_pairs, {})) {
            pair->held.clear();
            if (pair->client) pair->client->abort();
            if (pair->server) pair->server->abort();
        }
    }

    // While true, new connections are closed as soon as they arrive
    void setRefusing(bool refusing) { m_refusing = refusing; }

    // Milliseconds since the proxy started, of every connection it received
    const QList<qint64> &connectionTimes() const { return m_connectionTimes; }

private:
    struct Pair {
        QPointer<QTcpSocket> client;
        QPointer<QTcpSocket> server;
        QByteArray pending;      // from the client, before the server connection is up
        QByteArray held;         // from the server, while holding
    };

    void accept() {
        while (auto *client = m_listener.nextPendingConnection()) {
            m_connectionTimes.append(m_clock.elapsed());
            client->setParent(this);
            if (m_refusing) {
                client->abort();
                client->deleteLater();
                continue;
            }
            auto pair = std::make_shared<Pair>();
            pair->client = client;
            pair->server = new QTcpSocket(this);
            client->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            m_pairs.append(pair);

            QObject::connect(pair->server, &QTcpSocket::connected, this, [pair] {
                pair->server->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                pair->server->write(std::exchange(pair->pending, {}));
            });
            QObject::connect(client, &QTcpSocket::readyRead, this, [pair] {
                const auto data = pair->client->readAll();
                if (pair->server && pair->server->state() == QAbstractSocket::ConnectedState)
                    pair->server->write(data);
                else
                    pair->pending += data;
            });
            QObject::connect(pair->server, &QTcpSocket::readyRead, this, [this, pair] {
                const auto data = pair->server->readAll();
                if (m_hold) pair->held += data;
                else if (pair->client) pair->client->write(data);
            });
            const auto close = [this, pair] {
                m_pairs.removeOne(pair);
                if (pair->client) pair->client->disconnectFromHost();
                if (pair->server) pair->server->disconnectFromHost();
            };
            QObject::connect(client, &QTcpSocket::disconnected, this, close);
            QObject::connect(pair->server, &QTcpSocket::disconnected, this, close);
            pair->server->connectToHost(QHostAddress::LocalHost, m_serverPort);
        }
    }

    const quint16 m_serverPort;
    QTcpServer m_listener;
    QList<std::shared_ptr<Pair>> m_pairs;
    QList<qint64> m_connectionTimes;
    QElapsedTimer m_clock;
    bool m_hold = false;
    bool m_refusing = false;
};
