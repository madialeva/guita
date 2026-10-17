#include <backend/api/HttpRpcTransport.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <optional>
#include <string>

namespace {
constexpr const char *kProcedurePath = R"(/api/([A-Za-z_][A-Za-z0-9_.]*/[A-Za-z_][A-Za-z0-9_]*))";

// Connect's mapping of error codes to HTTP statuses (an unknown procedure is a 404)
int httpStatus(RpcCode code) {
    switch (code) {
    case RpcCode::Ok:              return 200;
    case RpcCode::InvalidArgument: return 400;
    case RpcCode::Unauthenticated: return 401;
    case RpcCode::Unimplemented:   return 404;
    case RpcCode::Internal:        return 500;
    }
    return 500;
}

// Connect error body: {"code": "...", "message": "..."}
void sendError(httplib::Response &response, int status, RpcCode code, const std::string &message) {
    google::protobuf::Struct error;
    auto &fields = *error.mutable_fields();
    fields["code"].set_string_value(std::string(connectCodeName(code)));
    if (!message.empty())
        fields["message"].set_string_value(message);
    std::string body;
    (void)google::protobuf::util::MessageToJsonString(error, &body);
    response.status = status;
    response.set_content(body, "application/json");
}

std::optional<RpcCodec> codecOf(const std::string &contentType) {
    auto type = contentType.substr(0, contentType.find(';'));
    while (!type.empty() && type.back() == ' ')
        type.pop_back();
    if (type == "application/proto") return RpcCodec::Binary;
    if (type == "application/json")  return RpcCodec::Json;
    return std::nullopt;
}
}

// Handlers run concurrently on the server's thread pool: the router is thread-safe
void addRpcRoutes(httplib::Server &server, const RpcRouter &router) {
    server.Post(kProcedurePath, [&router](const httplib::Request &request, httplib::Response &response) {
        const auto codec = codecOf(request.get_header_value("Content-Type"));
        if (!codec) {
            sendError(response, 415, RpcCode::Unimplemented,
                      "Expected Content-Type application/proto or application/json");
            return;
        }
        const auto result = router.call({request.matches[1].str(), *codec, request.body,
                                         request.get_header_value("Authorization")});
        if (result.code != RpcCode::Ok) {
            sendError(response, httpStatus(result.code), result.code, result.message);
            return;
        }
        response.status = 200;
        response.set_content(result.payload,
                             *codec == RpcCodec::Binary ? "application/proto" : "application/json");
    });

    const auto onlyPost = [](const httplib::Request &, httplib::Response &response) {
        response.set_header("Allow", "POST");
        sendError(response, 405, RpcCode::Unimplemented, "Procedures are called with POST");
    };
    server.Get(kProcedurePath, onlyPost);
    server.Put(kProcedurePath, onlyPost);
    server.Patch(kProcedurePath, onlyPost);
    server.Delete(kProcedurePath, onlyPost);
}
