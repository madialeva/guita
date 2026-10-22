#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

// An open subscription to a stream procedure: its messages reach the callback
// given to subscribe() until the subscription is destroyed or reset(); once
// that returns, no more messages arrive. Without a subscription, result()
// says why. Move-only.
class RpcSubscription {
public:
    RpcSubscription() = default;
    explicit RpcSubscription(RpcResult error) : m_result(std::move(error)) {}
    explicit RpcSubscription(std::shared_ptr<void> handle) : m_handle(std::move(handle)) {}
    RpcSubscription(RpcSubscription &&) noexcept = default;
    RpcSubscription &operator=(RpcSubscription &&) noexcept = default;
    RpcSubscription(const RpcSubscription &) = delete;
    RpcSubscription &operator=(const RpcSubscription &) = delete;

    const RpcResult &result() const { return m_result; }
    bool active() const { return m_handle != nullptr; }
    void reset() { m_handle.reset(); }

private:
    RpcResult m_result;
    std::shared_ptr<void> m_handle;   // ends the subscription when released
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
