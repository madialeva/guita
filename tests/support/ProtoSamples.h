#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// Binary Protobuf reference bytes of fixed messages of the contract, worked
// out by hand from the encoding (key = field number << 3 | wire type; varints;
// length-delimited strings and messages; proto3 fields without presence are
// not written when zero), not produced by protoc. protoc writes the fields of
// a message in field-number order, so the same code must write exactly these
// bytes on every side and platform: test_contract checks them natively and
// bench_encoding in WebAssembly.
namespace protoSamples {

// The values the samples encode
inline constexpr std::string_view kCustomer = "Acme S.L.";
inline constexpr std::string_view kTaxId = "B12345678";
inline constexpr std::int64_t kTotalCents = 121000;
inline constexpr std::int64_t kMaxCents = 999'999'999'999;
inline constexpr std::string_view kRequestId = "123e4567-e89b-12d3-a456-426614174000";
inline constexpr std::string_view kAuthorization = "Bearer s3cret";
inline constexpr std::string_view kCreateProcedure = "guita.v1.InvoiceService/CreateInvoice";

// CreateInvoiceRequest {customer, total_cents}, without tax ID
inline constexpr std::string_view kCreateInvoiceRequest =
    "0a0941636d6520532e4c2e18a8b107";
// CreateInvoiceRequest {customer, customer_tax_id, total_cents}
inline constexpr std::string_view kCreateInvoiceRequestWithTaxId =
    "0a0941636d6520532e4c2e120942313233343536373818a8b107";
// CreateInvoiceRequest with an empty tax ID: present, unlike an absent one
inline constexpr std::string_view kCreateInvoiceRequestEmptyTaxId =
    "0a0941636d6520532e4c2e120018a8b107";
// CreateInvoiceRequest {customer, total_cents -1}: a negative int64 takes ten bytes
inline constexpr std::string_view kCreateInvoiceRequestNegative =
    "0a0941636d6520532e4c2e18ffffffffffffffffff01";
// CreateInvoiceResponse {invoice {id 42, customer, customer_tax_id, total_cents}}
inline constexpr std::string_view kCreateInvoiceResponse =
    "0a1c082a120941636d6520532e4c2e1a0942313233343536373820a8b107";
// ListInvoicesResponse with {id 1, no tax ID, 0 cents} and {id INT64_MAX, tax ID, kMaxCents}
inline constexpr std::string_view kListInvoicesResponseLimits =
    "0a0d0801120941636d6520532e4c2e0a2708ffffffffffffffff7f120941636d6520532e4c2e"
    "1a0942313233343536373820ff9f94a58d1d";

// CreateInvoiceRequest {customer, total_cents, request_id}: field 4, after the others
inline constexpr std::string_view kCreateInvoiceRequestWithRequestId =
    "0a0941636d6520532e4c2e18a8b1072224"
    "31323365343536372d653839622d313264332d613435362d343236363134313734303030";
// WatchInvoicesResponse {started {}}: an empty message is still written in a oneof
inline constexpr std::string_view kWatchInvoicesStarted = "0a00";
// WatchInvoicesResponse {created {id 42, customer, customer_tax_id, total_cents}}
inline constexpr std::string_view kWatchInvoicesCreated =
    "121c082a120941636d6520532e4c2e1a0942313233343536373820a8b107";

// Frames of the persistent channel (guita.channel.v1)
// ClientFrame {hello {authorization}}
inline constexpr std::string_view kClientHello = "0a0f0a0d42656172657220733363726574";
// ClientFrame {call {correlation_id 7, procedure, payload kCreateInvoiceRequest}}
inline constexpr std::string_view kClientCall =
    "123a0807122567756974612e76312e496e766f696365536572766963652f437265617465496e766f696365"
    "1a0f0a0941636d6520532e4c2e18a8b107";
// ClientFrame {cancel {correlation_id 300}}: a two-byte varint
inline constexpr std::string_view kClientCancel = "1a0308ac02";
// ServerFrame {welcome {}}
inline constexpr std::string_view kServerWelcome = "0a00";
// ServerFrame {reply {correlation_id 7, payload kCreateInvoiceResponse}}
inline constexpr std::string_view kServerReply =
    "12220807121e0a1c082a120941636d6520532e4c2e1a0942313233343536373820a8b107";
// ServerFrame {reply {correlation_id 8, error {invalid_argument, "Customer is required"}}}
inline constexpr std::string_view kServerReplyError =
    "122c08081a280a10696e76616c69645f617267756d656e741214437573746f6d6572206973207265717569726564";
// ServerFrame {message {correlation_id 300, payload kWatchInvoicesStarted}}
inline constexpr std::string_view kServerStreamMessage = "1a0708ac0212020a00";
// ServerFrame {goodbye {error {unauthenticated, "Unauthorized"}}}
inline constexpr std::string_view kServerGoodbye =
    "22210a1f0a0f756e61757468656e74696361746564120c556e617574686f72697a6564";

inline std::string fromHex(std::string_view hex) {
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(char(digit(hex[i]) << 4 | digit(hex[i + 1])));
    return bytes;
}

inline std::string toHex(std::string_view bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    for (const unsigned char c : bytes) {
        hex.push_back(kDigits[c >> 4]);
        hex.push_back(kDigits[c & 0xf]);
    }
    return hex;
}

}
