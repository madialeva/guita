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
