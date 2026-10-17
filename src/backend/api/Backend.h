#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcCall.h>
#include <memory>
#include <optional>
#include <string>

// The HTTP transport of the backend: the RPC procedures (and, with a docroot,
// the web client's static files) over cpp-httplib
struct HttpOptions {
    std::string address = "127.0.0.1";
    int port = 0;                    // 0: free port chosen by the OS
    std::string docroot;             // static files (WASM build); empty: API only
    bool accessLog = false;          // one line per request on stderr
};

struct BackendOptions {
    std::string connectionString;    // libpq connection string
    Authorizer authorizer;
    std::optional<HttpOptions> http; // none: in-process calls only (standalone mode)
};

// Full backend (database + services + RPC router), shared by the `server`
// executable, which serves it over HTTP, and by the standalone desktop client,
// which calls it in process.
//
// The header keeps cpp-httplib, libpqxx and protobuf out of its includers
// (pimpl): the client only needs this interface to embed the backend.
class Backend {
public:
    // Connects to the database, applies migrations, prepares the statements
    // and registers the procedures. Throws on failure.
    explicit Backend(BackendOptions options);
    ~Backend();

    // A call to a procedure of the contract, in the caller's thread. Thread-safe.
    RpcResult call(const RpcCall &call) const;

    // HTTP transport, when configured. start() does not block: the server runs
    // on its own threads. Without HTTP, start() does nothing and port() is 0.
    bool start();
    int  port() const;               // actual port (useful when 0 was requested)
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
