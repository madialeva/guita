#pragma once
#include <QUrl>
#ifdef Q_OS_WASM
#include <emscripten/val.h>
#include <string>
#endif

// URL of the backend's persistent channel in the web and connected modes.
inline QUrl defaultChannelUrl() {
#ifdef Q_OS_WASM
    // Same origin as the page that serves the .wasm: wss behind https
    const auto location = emscripten::val::global("location");
    const auto secure = location["protocol"].as<std::string>() == "https:";
    const auto host = location["host"].as<std::string>();
    return QUrl(QString::fromStdString((secure ? "wss://" : "ws://") + host + "/api/channel"));
#else
    // API_URL is the API's base URL over HTTP: the channel lives beside its procedures
    auto url = QUrl(qEnvironmentVariable("API_URL", "http://127.0.0.1:8090/api/")).resolved(QUrl("channel"));
    url.setScheme(url.scheme() == QLatin1String("https") ? "wss" : "ws");
    return url;
#endif
}
