#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcRouter.h>
#include <httplib.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <set>

class ChannelSession;

struct ChannelOptions {
    std::size_t maxChannels = 64;                    // open at once; more are closed with 1013
    std::size_t maxInFlight = 64;                    // unary calls per channel before it stops reading
    std::size_t maxQueuedBytes = 1024 * 1024;        // unsent frames before a channel is closed
    std::size_t workers = 8;                         // threads running unary calls, for all channels
    std::chrono::milliseconds helloTimeout{10'000};  // to send the Hello
};

// The persistent channel of the RPC router over WebSocket, at /api/channel:
// one binary message is one frame of guita.channel.v1. The first frame is a
// Hello with the authorization; then calls, unary or streams, each with a
// correlation ID chosen by the client. Unary calls run on a worker pool,
// several at once per channel, and their replies go out as they finish;
// stream calls open subscriptions whose messages carry the call's ID.
//
// Each channel uses a thread of the HTTP server, which reads (and the ping
// thread of cpp-httplib), and a writer thread that empties its queue of
// outgoing frames: whoever produces a reply or an event only queues it. A
// channel whose queue grows beyond maxQueuedBytes is closed.
class WebSocketRpcTransport {
public:
    WebSocketRpcTransport(const RpcRouter &router, Authorizer authorizer, ChannelOptions options = {});
    // Closes every channel and waits for the calls in flight
    ~WebSocketRpcTransport();

    WebSocketRpcTransport(const WebSocketRpcTransport &) = delete;
    WebSocketRpcTransport &operator=(const WebSocketRpcTransport &) = delete;

    // Serves the channel on the server, at /api/channel
    void addRoute(httplib::Server &server);

    // Closes every open channel (1001, going away) and refuses new ones, so
    // that the server can stop: the channels end within a fraction of a second
    void closeAll();

    std::size_t openChannels() const;

private:
    friend class ChannelSession;

    void serve(httplib::ws::WebSocket &socket);

    const RpcRouter &m_router;
    const Authorizer m_authorizer;
    const ChannelOptions m_options;
    httplib::ThreadPool m_workers;

    mutable std::mutex m_mutex;
    std::set<ChannelSession *> m_sessions;
    std::size_t m_starting = 0;
    bool m_closing = false;
};
