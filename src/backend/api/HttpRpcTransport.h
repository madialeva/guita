#pragma once
#include <backend/api/RpcRouter.h>
#include <httplib.h>
#include <string>

// The HTTP transport of the RPC router: the unary subset of the Connect
// protocol, POST /api/<package>.<Service>/<Method> with application/proto,
// answered in binary. HTTP statuses exist only here.
void addRpcRoutes(httplib::Server &server, const RpcRouter &router);

// A Connect error body, {"code": "...", "message": "..."}: JSON for any codec,
// as Connect defines it. Without a message, only the code.
std::string connectErrorBody(RpcCode code, const std::string &message);
