#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcCall.h>
#include <google/protobuf/message_lite.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

// Dispatches calls to the procedures of the contract. It knows procedures, not
// transports: HTTP, in-process calls and the persistent channel all go through
// call(), and through subscribe() for stream procedures (server events).
// Procedures are registered at startup; afterwards call() and subscribe() are
// thread-safe and may run concurrently from any transport.
class RpcRouter {
public:
    explicit RpcRouter(Authorizer authorizer) : m_authorizer(std::move(authorizer)) {}

    // Registers "<package>.<Service>/<Method>" with the method's request and
    // response types, "<package>.<Method>Request" and "<package>.<Method>Response"
    // as the contract names them (checked here, at startup). Handlers get
    // decoded requests, return responses and report validation errors with
    // std::invalid_argument.
    template <class Request, class Response>
    void add(const std::string &procedure, std::function<Response(const Request &)> handler) {
        checkContract(procedure, Request::default_instance(), Response::default_instance());
        m_procedures[procedure] = [handler = std::move(handler)](const std::string &payload) {
            Request request;
            // Fields the contract does not define (of a later version) are skipped
            if (!request.ParseFromString(payload))
                return RpcResult{RpcCode::InvalidArgument, {},
                                 "Malformed " + std::string(request.GetTypeName())};
            return RpcResult{RpcCode::Ok, handler(request).SerializeAsString(), {}};
        };
    }

    // Registers a server-streaming procedure, with the same contract check.
    // The handler gets the decoded request and a function that sends one
    // response message, and returns the handle of its subscription (for
    // example from EventTopic::subscribe()); releasing the handle must stop
    // the messages. It may send messages before it returns.
    template <class Request, class Response>
    using StreamHandler = std::function<std::shared_ptr<void>(const Request &,
                                                              std::function<void(const Response &)>)>;

    template <class Request, class Response>
    void addStream(const std::string &procedure, StreamHandler<Request, Response> handler) {
        checkContract(procedure, Request::default_instance(), Response::default_instance());
        m_streams[procedure] = [handler = std::move(handler)](const std::string &payload, MessageSink sink) {
            Request request;
            if (!request.ParseFromString(payload))
                return RpcSubscription(RpcResult{RpcCode::InvalidArgument, {},
                                                 "Malformed " + std::string(request.GetTypeName())});
            return RpcSubscription(handler(request, [sink = std::move(sink)](const Response &response) {
                sink(response.SerializeAsString());
            }));
        };
    }

    // A unary call. A stream procedure answers unimplemented: it needs subscribe()
    RpcResult call(const RpcCall &call) const;

    // Subscribes to a stream procedure: onMessage gets each encoded response
    // message, in the thread that produces it, until the subscription is
    // released. A unary procedure answers unimplemented.
    using MessageSink = std::function<void(std::string)>;
    RpcSubscription subscribe(const RpcCall &call, MessageSink onMessage) const;

private:
    using Procedure = std::function<RpcResult(const std::string &)>;
    using Stream = std::function<RpcSubscription(const std::string &, MessageSink)>;

    static void checkContract(const std::string &procedure, const google::protobuf::MessageLite &request,
                              const google::protobuf::MessageLite &response);

    Authorizer m_authorizer;
    std::unordered_map<std::string, Procedure> m_procedures;
    std::unordered_map<std::string, Stream> m_streams;
};
