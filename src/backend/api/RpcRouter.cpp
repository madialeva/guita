#include <backend/api/RpcRouter.h>
#include <iostream>

// The messages carry no descriptors (the contract is generated for the lite
// runtime), so the check uses the names of their types: the package of the
// service and the method's name, as buf lint's STANDARD rules require
void RpcRouter::checkContract(const std::string &procedure, const google::protobuf::MessageLite &request,
                              const google::protobuf::MessageLite &response) {
    const auto slash = procedure.find('/');
    const auto dot = procedure.rfind('.', slash);
    const bool wellFormed = slash != std::string::npos && dot != std::string::npos && slash + 1 < procedure.size();
    const auto prefix = wellFormed ? procedure.substr(0, dot + 1) + procedure.substr(slash + 1) : std::string();
    if (!wellFormed || request.GetTypeName() != prefix + "Request" || response.GetTypeName() != prefix + "Response")
        throw std::logic_error("Procedure " + procedure + " does not match the contract");
}

RpcResult RpcRouter::call(const RpcCall &call) const {
    if (!m_authorizer(call.authorization))
        return {RpcCode::Unauthenticated, {}, "Unauthorized"};

    const auto procedure = m_procedures.find(call.procedure);
    if (procedure == m_procedures.end()) {
        if (m_streams.contains(call.procedure))
            return {RpcCode::Unimplemented, {}, "Procedure " + call.procedure +
                    " is a stream: it is served over the persistent channel and in process"};
        return {RpcCode::Unimplemented, {}, "Unknown procedure " + call.procedure};
    }

    try {
        return procedure->second(call.payload);
    } catch (const std::invalid_argument &e) {
        return {RpcCode::InvalidArgument, {}, e.what()};
    } catch (const std::exception &e) {
        std::cerr << "error: " << call.procedure << ": " << e.what() << '\n';
        return {RpcCode::Internal, {}, "Internal error"};
    }
}

RpcSubscription RpcRouter::subscribe(const RpcCall &call, MessageSink onMessage) const {
    if (!m_authorizer(call.authorization))
        return RpcSubscription(RpcResult{RpcCode::Unauthenticated, {}, "Unauthorized"});

    const auto stream = m_streams.find(call.procedure);
    if (stream == m_streams.end()) {
        if (m_procedures.contains(call.procedure))
            return RpcSubscription(RpcResult{RpcCode::Unimplemented, {},
                                             "Procedure " + call.procedure + " is not a stream"});
        return RpcSubscription(RpcResult{RpcCode::Unimplemented, {}, "Unknown procedure " + call.procedure});
    }

    try {
        return stream->second(call.payload, std::move(onMessage));
    } catch (const std::invalid_argument &e) {
        return RpcSubscription(RpcResult{RpcCode::InvalidArgument, {}, e.what()});
    } catch (const std::exception &e) {
        std::cerr << "error: " << call.procedure << ": " << e.what() << '\n';
        return RpcSubscription(RpcResult{RpcCode::Internal, {}, "Internal error"});
    }
}
