#include <backend/api/RpcRouter.h>
#include <google/protobuf/util/json_util.h>
#include <iostream>

namespace rpc {

bool decode(google::protobuf::Message &message, RpcCodec codec, const std::string &payload) {
    if (codec == RpcCodec::Binary)
        return message.ParseFromString(payload);
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;     // newer clients keep working with older servers
    return google::protobuf::util::JsonStringToMessage(payload, &message, options).ok();
}

std::string encode(const google::protobuf::Message &message, RpcCodec codec) {
    if (codec == RpcCodec::Binary)
        return message.SerializeAsString();
    google::protobuf::util::JsonPrintOptions options;
    options.always_print_fields_with_no_presence = true;   // readable: zeros are printed too
    std::string json;
    if (const auto status = google::protobuf::util::MessageToJsonString(message, &json, options); !status.ok())
        throw std::runtime_error("JSON encoding failed: " + std::string(status.message()));
    return json;
}

}

void RpcRouter::checkContract(const std::string &procedure,
                              const google::protobuf::Descriptor *request,
                              const google::protobuf::Descriptor *response) {
    const auto slash = procedure.find('/');
    const auto *method = slash == std::string::npos
        ? nullptr
        : google::protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              procedure.substr(0, slash) + '.' + procedure.substr(slash + 1));
    if (!method || method->input_type() != request || method->output_type() != response)
        throw std::logic_error("Procedure " + procedure + " does not match the contract");
}

RpcResult RpcRouter::call(const RpcCall &call) const {
    if (!m_authorizer(call.authorization))
        return {RpcCode::Unauthenticated, {}, "Unauthorized"};

    const auto procedure = m_procedures.find(call.procedure);
    if (procedure == m_procedures.end())
        return {RpcCode::Unimplemented, {}, "Unknown procedure " + call.procedure};

    try {
        return procedure->second(call.codec, call.payload);
    } catch (const std::invalid_argument &e) {
        return {RpcCode::InvalidArgument, {}, e.what()};
    } catch (const std::exception &e) {
        std::cerr << "error: " << call.procedure << ": " << e.what() << '\n';
        return {RpcCode::Internal, {}, "Internal error"};
    }
}
