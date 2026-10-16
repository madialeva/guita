#pragma once
#include <backend/api/Authorizer.h>
#include <glaze/glaze.hpp>
#include <httplib.h>
#include <functional>
#include <stdexcept>
#include <string>

// Helpers shared by every REST route. JSON bodies map to plain structs through
// glaze's compile-time reflection: member names are the JSON field names.
namespace http {

using Handler = std::function<void(const httplib::Request &, httplib::Response &)>;

// A request the client must fix; carries its HTTP status
struct InvalidRequest : std::runtime_error {
    InvalidRequest(int status, const std::string &message)
        : std::runtime_error(message), status(status) {}
    int status;
};

// Unknown fields are ignored, so that newer clients keep working with older servers
inline constexpr glz::opts kReadOptions{.error_on_unknown_keys = false};

struct Error {
    std::string error;
};

template <class T>
void sendJson(httplib::Response &response, int status, const T &value) {
    std::string body;
    if (const auto ec = glz::write_json(value, body))
        throw std::runtime_error("JSON serialization failed: " + glz::format_error(ec));
    response.status = status;
    response.set_content(std::move(body), "application/json");
}

inline void sendError(httplib::Response &response, int status, const std::string &message) {
    sendJson(response, status, Error{message});
}

// Throws InvalidRequest: 415 unless the body is JSON
void requireJson(const httplib::Request &request);

// Parses the JSON body into T; throws InvalidRequest (415, 400)
template <class T>
T jsonBody(const httplib::Request &request) {
    requireJson(request);
    T value{};
    if (const auto ec = glz::read<kReadOptions>(value, request.body))
        throw InvalidRequest(400, "Invalid JSON: " + glz::format_error(ec, request.body));
    return value;
}

// Wraps a route handler with Bearer authorization and the common error mapping:
// 400 validation and malformed requests, 401 authorization, 415 non-JSON body,
// 500 anything unexpected (logged)
Handler guarded(Authorizer authorizer, Handler handler);

// Answers 405 for the methods a path does not implement
void rejectOtherMethods(httplib::Server &server, const std::string &path, const std::string &allow);

}
