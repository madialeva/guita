#pragma once
#include <string>
#include <string_view>

// A call to a procedure of the contract, independent of the transport that
// carried it (HTTP, in process, ...). Payloads are binary Protobuf. Only
// standard types: the client sees this header through Backend.h.

// Result of a call: Ok or one of the Connect error codes the backend uses
enum class RpcCode { Ok, InvalidArgument, Unauthenticated, Unimplemented, Internal };

struct RpcCall {
    std::string procedure;        // "guita.v1.InvoiceService/CreateInvoice"
    std::string payload;          // the encoded request message
    std::string authorization;    // as in an Authorization header; empty in process
};

struct RpcResult {
    RpcCode code = RpcCode::Ok;
    std::string payload;          // the encoded response message when code is Ok
    std::string message;          // the error message otherwise
};

// Connect name of a code, as in the "code" field of Connect errors
inline std::string_view connectCodeName(RpcCode code) {
    switch (code) {
    case RpcCode::Ok:              return "ok";
    case RpcCode::InvalidArgument: return "invalid_argument";
    case RpcCode::Unauthenticated: return "unauthenticated";
    case RpcCode::Unimplemented:   return "unimplemented";
    case RpcCode::Internal:        return "internal";
    }
    return "unknown";
}
