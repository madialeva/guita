#pragma once
#include <backend/api/Authorizer.h>
#include <memory>
#include <string>

struct BackendOptions {
    std::string connectionString;    // libpq connection string
    Authorizer authorizer;
    std::string address = "127.0.0.1";
    int port = 0;                    // 0: free port chosen by the OS
    std::string docroot;             // static files (WASM build); empty: API only
    bool accessLog = false;          // one line per request on stderr
};

// Full backend (database + services + REST API over cpp-httplib), shared by the
// `server` executable and by the desktop client in standalone mode.
//
// The header keeps cpp-httplib and libpqxx out of its includers (pimpl): the
// client only needs this interface to embed the backend.
class Backend {
public:
    // Connects to the database, applies migrations and prepares the
    // statements. Throws on failure.
    explicit Backend(BackendOptions options);
    ~Backend();

    bool start();             // non-blocking: the server runs on its own threads
    int  port() const;        // actual port (useful when 0 was requested)
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
