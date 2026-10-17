#pragma once
#include <backend/api/Authorizer.h>
#include <backend/api/RpcCall.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace rpc {
// Payload <-> message in the given codec; decode() is false for a malformed payload
bool decode(google::protobuf::Message &message, RpcCodec codec, const std::string &payload);
std::string encode(const google::protobuf::Message &message, RpcCodec codec);
}

// Dispatches calls to the procedures of the contract. It knows procedures, not
// transports: HTTP, in-process calls and any later channel all go through
// call(). Procedures are registered at startup; afterwards call() is
// thread-safe and may run concurrently from any transport.
class RpcRouter {
public:
    explicit RpcRouter(Authorizer authorizer) : m_authorizer(std::move(authorizer)) {}

    // Registers "<package>.<Service>/<Method>". The method must exist in the
    // contract with exactly these request and response types (checked here, at
    // startup). Handlers get decoded requests, return responses and report
    // validation errors with std::invalid_argument.
    template <class Request, class Response>
    void add(const std::string &procedure, std::function<Response(const Request &)> handler) {
        checkContract(procedure, Request::descriptor(), Response::descriptor());
        m_procedures[procedure] = [handler = std::move(handler)](RpcCodec codec, const std::string &payload) {
            Request request;
            if (!rpc::decode(request, codec, payload))
                return RpcResult{RpcCode::InvalidArgument, {},
                                 "Malformed " + std::string(Request::descriptor()->full_name())};
            return RpcResult{RpcCode::Ok, rpc::encode(handler(request), codec), {}};
        };
    }

    RpcResult call(const RpcCall &call) const;

private:
    using Procedure = std::function<RpcResult(RpcCodec, const std::string &)>;

    static void checkContract(const std::string &procedure,
                              const google::protobuf::Descriptor *request,
                              const google::protobuf::Descriptor *response);

    Authorizer m_authorizer;
    std::unordered_map<std::string, Procedure> m_procedures;
};
