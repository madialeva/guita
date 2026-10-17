#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcCall.h>
#include <google/protobuf/message_lite.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

// Dispatches calls to the procedures of the contract. It knows procedures, not
// transports: HTTP, in-process calls and any later channel all go through
// call(). Procedures are registered at startup; afterwards call() is
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

    RpcResult call(const RpcCall &call) const;

private:
    using Procedure = std::function<RpcResult(const std::string &)>;

    static void checkContract(const std::string &procedure, const google::protobuf::MessageLite &request,
                              const google::protobuf::MessageLite &response);

    Authorizer m_authorizer;
    std::unordered_map<std::string, Procedure> m_procedures;
};
