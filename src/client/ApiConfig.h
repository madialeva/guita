#pragma once
#include <QUrl>
#ifdef Q_OS_WASM
#include <emscripten/val.h>
#endif

// Default API base URL for the web and connected modes.
inline QUrl defaultApiBaseUrl() {
#ifdef Q_OS_WASM
    // Same origin as the page that serves the .wasm
    auto origin = emscripten::val::global("location")["origin"].as<std::string>();
    return QUrl(QString::fromStdString(origin) + "/api/");
#else
    return QUrl(qEnvironmentVariable("API_URL", "http://127.0.0.1:8090/api/"));
#endif
}
