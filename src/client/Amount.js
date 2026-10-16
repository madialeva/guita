.pragma library

// Amounts typed in the UI and amounts in cents, the integer minor units of the API.

// "1234.5" or "1234,50" -> 123450; -1 when the text is not an amount (no sign,
// no thousands separator, up to 10 digits and 2 decimals). Exact: parsed as
// text and returned as a whole number, never through decimal arithmetic.
// Callers keep it in a double: QML int is 32-bit, while doubles hold whole
// numbers exactly up to 2^53.
function toCents(text) {
    const amount = String(text).trim().replace(",", ".")
    if (!/^\d{1,10}(\.\d{1,2})?$/.test(amount))
        return -1
    const [units, decimals = ""] = amount.split(".")
    return Number(units) * 100 + Number((decimals + "00").slice(0, 2))
}

// 123450 -> "1234,50" or "1,234.50", following the locale (the system's by default)
function formatCents(cents, locale) {
    return (cents / 100).toLocaleString(locale === undefined ? Qt.locale() : locale, "f", 2)
}
