#pragma once
#include <functional>
#include <string>

// Receives the full Authorization header ("Bearer ...") and decides whether to accept it.
using Authorizer = std::function<bool(const std::string &authorizationHeader)>;

// Fixed shared token. Used by standalone mode (random token per launch) and,
// until user authentication exists, by the server during development. An empty
// token accepts everything (development only).
inline Authorizer tokenAuthorizer(const std::string &token) {
    if (token.empty())
        return [](const std::string &) { return true; };
    return [expected = "Bearer " + token](const std::string &header) { return header == expected; };
}
