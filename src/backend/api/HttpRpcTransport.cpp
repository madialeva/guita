#include <backend/api/HttpRpcTransport.h>
#include <cstdio>
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

// A JSON string: quotes, backslashes and control characters escaped; UTF-8
// passes through, as JSON allows
std::string jsonString(const std::string &text) {
    std::string json = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':  json += "\\\""; break;
        case '\\': json += "\\\\"; break;
        case '\n': json += "\\n"; break;
        case '\r': json += "\\r"; break;
        case '\t': json += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned char>(c));
                json += escaped;
            } else {
                json += c;
            }
        }
    }
    return json + '"';
}

void sendError(httplib::Response &response, int status, RpcCode code, const std::string &message) {
    response.status = status;
    response.set_content(connectErrorBody(code, message), "application/json");
}

bool isProto(const std::string &contentType) {
    auto type = contentType.substr(0, contentType.find(';'));
    while (!type.empty() && type.back() == ' ')
        type.pop_back();
    return type == "application/proto";
}
}

std::string connectErrorBody(RpcCode code, const std::string &message) {
    std::string body = "{\"code\":" + jsonString(std::string(connectCodeName(code)));
    if (!message.empty())
        body += ",\"message\":" + jsonString(message);
    return body + '}';
}

// Handlers run concurrently on the server's thread pool: the router is thread-safe
void addRpcRoutes(httplib::Server &server, const RpcRouter &router) {
    server.Post(kProcedurePath, [&router](const httplib::Request &request, httplib::Response &response) {
        if (!isProto(request.get_header_value("Content-Type"))) {
            sendError(response, 415, RpcCode::Unimplemented, "Expected Content-Type application/proto");
            return;
        }
        const auto result = router.call({request.matches[1].str(), request.body,
                                         request.get_header_value("Authorization")});
        if (result.code != RpcCode::Ok) {
            sendError(response, httpStatus(result.code), result.code, result.message);
            return;
        }
        response.status = 200;
        response.set_content(result.payload, "application/proto");
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
