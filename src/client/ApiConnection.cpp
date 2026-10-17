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
    : QObject(parent), m_settings(std::move(settings)) {}
