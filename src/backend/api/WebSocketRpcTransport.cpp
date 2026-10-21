#include <backend/api/WebSocketRpcTransport.h>
#include <guita/channel/v1/channel.pb.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace channel = guita::channel::v1;
using httplib::ws::CloseStatus;

namespace {
// How often a waiting reader looks whether its channel is closing: closing a
// WebSocket from another thread does not wake a blocked read
constexpr auto kPollInterval = std::chrono::milliseconds(250);
// RFC 6455's "Try Again Later", which CloseStatus does not name
constexpr auto kTryAgainLater = static_cast<CloseStatus>(1013);

std::string replyFrame(std::uint64_t id, const RpcResult &result) {
    channel::ServerFrame frame;
    auto *reply = frame.mutable_reply();
    reply->set_correlation_id(id);
    if (result.code == RpcCode::Ok) {
        reply->set_payload(result.payload);
    } else {
        reply->mutable_error()->set_code(std::string(connectCodeName(result.code)));
        reply->mutable_error()->set_message(result.message);
    }
    return frame.SerializeAsString();
}

std::string goodbyeFrame(RpcCode code, const std::string &message) {
    channel::ServerFrame frame;
    frame.mutable_goodbye()->mutable_error()->set_code(std::string(connectCodeName(code)));
    frame.mutable_goodbye()->mutable_error()->set_message(message);
    return frame.SerializeAsString();
}
}

// One open channel. run() reads frames in the HTTP server's thread until the
// connection ends, and only returns once no subscription, call or writer of
// the session is left, so callbacks may use `this`.
class ChannelSession {
public:
    ChannelSession(WebSocketRpcTransport &transport, httplib::ws::WebSocket &socket)
        : m_transport(transport), m_socket(socket), m_writer([this] { write(); }) {}

    ~ChannelSession() {
        closeAfterFlush(CloseStatus::Normal, {});
        m_writer.join();
    }

    void run();

    // Sends what is queued, then closes the connection; frames queued later are dropped
    void closeAfterFlush(CloseStatus status, std::string reason) {
        {
            std::lock_guard lock(m_queueMutex);
            if (m_closeRequested) return;
            m_closeRequested = true;
            m_closeStatus = status;
            m_closeReason = std::move(reason);
        }
        m_queueChanged.notify_all();
    }

private:
    bool hello();
    void handle(channel::ClientFrame frame);
    void call(const channel::Call &call);
    void finish(std::uint64_t id, const RpcResult &result);
    void queue(std::string frame);
    void write();
    void shutDown();

    WebSocketRpcTransport &m_transport;
    httplib::ws::WebSocket &m_socket;
    std::string m_authorization;

    // Outgoing frames, sent by the writer thread
    std::mutex m_queueMutex;
    std::condition_variable m_queueChanged;
    std::deque<std::string> m_queue;
    std::size_t m_queuedBytes = 0;
    bool m_overflow = false;
    bool m_closeRequested = false;
    CloseStatus m_closeStatus = CloseStatus::Normal;
    std::string m_closeReason;

    // Calls in flight and subscriptions, by correlation ID
    std::mutex m_callsMutex;
    std::condition_variable m_callsChanged;
    std::unordered_set<std::uint64_t> m_inFlight;
    std::unordered_map<std::uint64_t, RpcSubscription> m_subscriptions;

    std::thread m_writer;      // last: starts once everything else exists
};

bool ChannelSession::hello() {
    m_socket.set_read_timeout(kPollInterval);
    const auto deadline = std::chrono::steady_clock::now() + m_transport.m_options.helloTimeout;
    std::string message;
    while (m_socket.is_open()) {
        const auto read = m_socket.read(message);
        if (read == httplib::ws::Timeout) {
            if (std::chrono::steady_clock::now() >= deadline) {
                closeAfterFlush(CloseStatus::PolicyViolation, "No hello");
                return false;
            }
            continue;
        }
        if (read == httplib::ws::Fail) return false;

        channel::ClientFrame frame;
        if (read == httplib::ws::Text || !frame.ParseFromString(message)) {
            closeAfterFlush(read == httplib::ws::Text ? CloseStatus::UnsupportedData : CloseStatus::ProtocolError,
                            "Not a frame");
            return false;
        }
        if (frame.frame_case() != channel::ClientFrame::kHello) {
            queue(goodbyeFrame(RpcCode::InvalidArgument, "The first frame must be a hello"));
            closeAfterFlush(CloseStatus::ProtocolError, "No hello");
            return false;
        }
        if (!m_transport.m_authorizer(frame.hello().authorization())) {
            queue(goodbyeFrame(RpcCode::Unauthenticated, "Unauthorized"));
            closeAfterFlush(CloseStatus::PolicyViolation, "Unauthorized");
            return false;
        }
        m_authorization = frame.hello().authorization();
        channel::ServerFrame welcome;
        welcome.mutable_welcome();
        queue(welcome.SerializeAsString());
        return true;
    }
    return false;
}

void ChannelSession::run() {
    if (hello()) {
        std::string message;
        while (m_socket.is_open()) {
            const auto read = m_socket.read(message);
            if (read == httplib::ws::Timeout) continue;
            if (read == httplib::ws::Fail) break;

            channel::ClientFrame frame;
            if (read == httplib::ws::Text || !frame.ParseFromString(message)) {
                queue(goodbyeFrame(RpcCode::InvalidArgument, "Not a frame of the channel"));
                closeAfterFlush(read == httplib::ws::Text ? CloseStatus::UnsupportedData : CloseStatus::ProtocolError,
                                "Not a frame");
                break;
            }
            handle(std::move(frame));
        }
    }
    shutDown();
}

void ChannelSession::handle(channel::ClientFrame frame) {
    switch (frame.frame_case()) {
    case channel::ClientFrame::kCall:
        call(frame.call());
        return;
    case channel::ClientFrame::kCancel: {
        // Released outside the lock: the topic may be delivering to it right now
        RpcSubscription ended;
        {
            std::lock_guard lock(m_callsMutex);
            const auto subscription = m_subscriptions.find(frame.cancel().correlation_id());
            if (subscription == m_subscriptions.end()) return;
            ended = std::move(subscription->second);
            m_subscriptions.erase(subscription);
        }
        return;
    }
    default:
        // A second hello, or a frame of a later version this server does not know
        queue(goodbyeFrame(RpcCode::InvalidArgument, "Unexpected frame"));
        closeAfterFlush(CloseStatus::ProtocolError, "Unexpected frame");
        return;
    }
}

void ChannelSession::call(const channel::Call &call) {
    const auto id = call.correlation_id();
    RpcCall rpc{call.procedure(), call.payload(), m_authorization};

    std::unique_lock lock(m_callsMutex);
    if (m_inFlight.contains(id) || m_subscriptions.contains(id)) {
        lock.unlock();
        queue(replyFrame(id, {RpcCode::InvalidArgument, {}, "Correlation ID " + std::to_string(id) + " is in use"}));
        return;
    }

    if (m_transport.m_router.isStream(rpc.procedure)) {
        // Only this thread adds calls, so the ID stays free meanwhile. The
        // messages are only queued: the first one may come before subscribe() returns
        lock.unlock();
        auto subscription = m_transport.m_router.subscribe(rpc, [this, id](std::string payload) {
            channel::ServerFrame frame;
            frame.mutable_message()->set_correlation_id(id);
            frame.mutable_message()->set_payload(std::move(payload));
            queue(frame.SerializeAsString());
        });
        if (!subscription.active()) {
            queue(replyFrame(id, subscription.result()));
            return;
        }
        lock.lock();
        m_subscriptions.emplace(id, std::move(subscription));
        return;
    }

    // Too many calls in flight: stop reading, so TCP slows the client down
    while (m_inFlight.size() >= m_transport.m_options.maxInFlight && m_socket.is_open())
        m_callsChanged.wait_for(lock, kPollInterval);
    m_inFlight.insert(id);
    lock.unlock();
    if (!m_transport.m_workers.enqueue([this, id, rpc = std::move(rpc)] { finish(id, m_transport.m_router.call(rpc)); }))
        finish(id, {RpcCode::Internal, {}, "The server is stopping"});
}

void ChannelSession::finish(std::uint64_t id, const RpcResult &result) {
    queue(replyFrame(id, result));
    {
        std::lock_guard lock(m_callsMutex);
        m_inFlight.erase(id);
    }
    m_callsChanged.notify_all();
}

void ChannelSession::queue(std::string frame) {
    {
        std::lock_guard lock(m_queueMutex);
        if (m_closeRequested || m_overflow) return;
        if (m_queuedBytes + frame.size() > m_transport.m_options.maxQueuedBytes) {
            // The client does not read: close it rather than hold the producer
            m_overflow = true;
        } else {
            m_queuedBytes += frame.size();
            m_queue.push_back(std::move(frame));
        }
    }
    m_queueChanged.notify_all();
}

void ChannelSession::write() {
    std::unique_lock lock(m_queueMutex);
    while (true) {
        m_queueChanged.wait(lock, [this] { return !m_queue.empty() || m_closeRequested || m_overflow; });
        if (m_overflow) {
            m_queue.clear();
            lock.unlock();
            m_socket.close(CloseStatus::PolicyViolation, "Too slow");
            return;
        }
        if (!m_queue.empty()) {
            auto frame = std::move(m_queue.front());
            m_queue.pop_front();
            m_queuedBytes -= frame.size();
            lock.unlock();
            const bool sent = m_socket.send(frame.data(), frame.size());
            lock.lock();
            if (!sent) {
                m_closeRequested = true;
                lock.unlock();
                m_socket.close(CloseStatus::Abnormal, {});
                return;
            }
            continue;
        }
        // Close requested and everything sent
        const auto status = m_closeStatus;
        const auto reason = m_closeReason;
        lock.unlock();
        m_socket.close(status, reason);
        return;
    }
}

void ChannelSession::shutDown() {
    // No message after this: the subscriptions end before the session does
    std::unordered_map<std::uint64_t, RpcSubscription> subscriptions;
    std::unique_lock lock(m_callsMutex);
    subscriptions.swap(m_subscriptions);
    lock.unlock();
    subscriptions.clear();
    // The unary calls still running use this session
    lock.lock();
    m_callsChanged.wait(lock, [this] { return m_inFlight.empty(); });
}

WebSocketRpcTransport::WebSocketRpcTransport(const RpcRouter &router, Authorizer authorizer, ChannelOptions options)
    : m_router(router), m_authorizer(std::move(authorizer)), m_options(options), m_workers(options.workers) {}

WebSocketRpcTransport::~WebSocketRpcTransport() {
    closeAll();
    m_workers.shutdown();
}

void WebSocketRpcTransport::addRoute(httplib::Server &server) {
    server.WebSocket("/api/channel", [this](const httplib::Request &, httplib::ws::WebSocket &socket) {
        serve(socket);
    });
}

void WebSocketRpcTransport::serve(httplib::ws::WebSocket &socket) {
    {
        // A place is reserved before the session exists, so the limit holds
        // for channels opening at once
        std::lock_guard lock(m_mutex);
        if (m_closing) {
            socket.close(CloseStatus::GoingAway, "Stopping");
            return;
        }
        if (m_sessions.size() + m_starting >= m_options.maxChannels) {
            socket.close(kTryAgainLater, "Too many channels");
            return;
        }
        ++m_starting;
    }
    ChannelSession session(*this, socket);
    {
        std::lock_guard lock(m_mutex);
        --m_starting;
        m_sessions.insert(&session);
        if (m_closing)
            session.closeAfterFlush(CloseStatus::GoingAway, "Stopping");
    }
    session.run();
    std::lock_guard lock(m_mutex);
    m_sessions.erase(&session);
}

void WebSocketRpcTransport::closeAll() {
    std::lock_guard lock(m_mutex);
    m_closing = true;
    for (auto *session : m_sessions)
        session->closeAfterFlush(CloseStatus::GoingAway, "Stopping");
}

std::size_t WebSocketRpcTransport::openChannels() const {
    std::lock_guard lock(m_mutex);
    return m_sessions.size();
}
