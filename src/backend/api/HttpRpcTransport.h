#pragma once
#include <backend/api/RpcRouter.h>
#include <httplib.h>

// The HTTP transport of the RPC router: the unary subset of the Connect
// protocol, POST /api/<package>.<Service>/<Method> with application/proto or
// application/json, answered in the same codec. HTTP statuses exist only here.
void addRpcRoutes(httplib::Server &server, const RpcRouter &router);
