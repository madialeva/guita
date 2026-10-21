#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcCall.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// The network transports of the backend over cpp-httplib: the RPC procedures
// over HTTP, the persistent channel over WebSocket at /api/channel and, with a
// docroot, the web client's static files
struct HttpOptions {
    std::string address = "127.0.0.1";
    int port = 0;                    // 0: free port chosen by the OS
    std::string docroot;             // static files (WASM build); empty: API only
    bool accessLog = false;          // one line per request on stderr
    std::size_t maxChannels = 64;    // persistent channels open at once
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

    // A subscription to a stream procedure of the contract (server events):
    // onMessage gets each encoded message in the thread that produces it (the
    // first one possibly before subscribe() returns), and must not block.
    // Thread-safe.
    RpcSubscription subscribe(const RpcCall &call, std::function<void(std::string)> onMessage) const;

    // Network transports, when configured. start() does not block: the server
    // runs on its own threads. Without HTTP, start() does nothing and port()
    // is 0. stop() closes the open channels first, so it does not wait for
    // their clients.
    bool start();
    int  port() const;               // actual port (useful when 0 was requested)
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
