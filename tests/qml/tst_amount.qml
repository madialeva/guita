import QtQuick
import QtTest
import "../../src/client/Amount.js" as Amount

// Parsing of the amounts typed in the UI and their display, from Amount.js.
TestCase {
    name: "Amount"

    function test_toCents_data() {
        return [
            { tag: "comma, one decimal", text: "1234,5", cents: 123450 },
            { tag: "dot, two decimals", text: "1234.50", cents: 123450 },
            { tag: "zero", text: "0", cents: 0 },
            { tag: "five cents", text: "0,05", cents: 5 },
            { tag: "surrounding spaces", text: " 12 ", cents: 1200 },
            { tag: "maximum", text: "9999999999.99", cents: 999999999999 },
            { tag: "thousands separator", text: "1.234,50", cents: -1 },
            { tag: "three decimals", text: "1.234", cents: -1 },
            { tag: "negative", text: "-1", cents: -1 },
            { tag: "empty", text: "", cents: -1 },
            { tag: "letters", text: "12a", cents: -1 },
            { tag: "too many digits", text: "12345678901", cents: -1 },
        ]
    }

    function test_toCents(data) {
        compare(Amount.toCents(data.text), data.cents)
    }

    function test_formatCents_data() {
        return [
            { tag: "Spanish", locale: "es_ES", cents: 123450, text: "1234,50" },
            { tag: "Spanish, grouped", locale: "es_ES", cents: 999999999999, text: "9.999.999.999,99" },
            { tag: "English", locale: "en_US", cents: 123450, text: "1,234.50" },
            { tag: "cents only", locale: "en_US", cents: 5, text: "0.05" },
            { tag: "zero", locale: "en_US", cents: 0, text: "0.00" },
        ]
    }

    function test_formatCents(data) {
        compare(Amount.formatCents(data.cents, Qt.locale(data.locale)), data.text)
    }
}
