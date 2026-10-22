#include <client/ApiConnection.h>
#include <utility>

namespace {
ApiConnection::Settings &pendingSettings() {
    static ApiConnection::Settings settings;
    return settings;
}
}

void ApiConnection::configure(Settings settings) {
    pendingSettings() = std::move(settings);
}

ApiConnection *ApiConnection::create(QQmlEngine *, QJSEngine *) {
    // Moved out: the connection owns its transport from now on
    return new ApiConnection(std::exchange(pendingSettings(), {}));
}

ApiConnection::ApiConnection(Settings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)) {
    if (!m_settings.transport) return;
    m_settings.transport->setAvailabilityHandler([this](bool available) {
        if (available == m_available) return;
        m_available = available;
        emit availableChanged();
    });
}

ApiConnection::~ApiConnection() {
    // The transport may outlive the connection
    if (m_settings.transport)
        m_settings.transport->setAvailabilityHandler({});
}
