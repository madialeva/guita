pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guita

ApplicationWindow {
    id: window

    readonly property string modeName: {
        switch (ApiConnection.mode) {
        case ApiConnection.Web:        return qsTr("Web")
        case ApiConnection.Standalone: return qsTr("Standalone")
        default:                       return qsTr("Connected")
        }
    }

    // "1234.5" or "1234,50" -> 123450; -1 when it is not an amount. Exact:
    // parsed as text and kept in a double (QML int is 32-bit; doubles hold
    // whole numbers exactly up to 2^53), never through decimal arithmetic
    function toCents(text: string): double {
        const amount = text.trim().replace(",", ".")
        if (!/^\d{1,10}(\.\d{1,2})?$/.test(amount))
            return -1
        const [units, decimals = ""] = amount.split(".")
        return Number(units) * 100 + Number((decimals + "00").slice(0, 2))
    }

    function formatCents(cents: double): string {
        return (cents / 100).toLocaleString(Qt.locale(), "f", 2)
    }

    visible: true
    width: 560; height: 480
    title: qsTr("Guita · %1").arg(modeName)

    InvoiceRepository {
        id: invoices
        connection: ApiConnection

        onCreated: (invoice) => {
            status.text = qsTr("Invoice %1 saved").arg(invoice.id)
            customer.text = ""; customerTaxId.text = ""; total.text = ""
            invoices.list()
        }
        onListed: (rows) => list.model = rows
        onFailed: (message) => status.text = qsTr("Error: %1").arg(message)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            text: qsTr("%1 mode · %2").arg(window.modeName).arg(ApiConnection.baseUrl)
            opacity: 0.6
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            TextField { id: customer;      placeholderText: qsTr("Customer"); Layout.fillWidth: true }
            TextField { id: customerTaxId; placeholderText: qsTr("Tax ID");   Layout.preferredWidth: 110 }
            TextField {
                id: total
                placeholderText: qsTr("Total")
                horizontalAlignment: TextInput.AlignRight
                Layout.preferredWidth: 100
            }
            Button {
                text: qsTr("Save")
                onClicked: {
                    const cents = window.toCents(total.text)
                    if (cents < 0) {
                        status.text = qsTr("The total must be an amount such as 1234.50")
                        return
                    }
                    invoices.create({ customer: customer.text, customerTaxId: customerTaxId.text,
                                      totalCents: cents })
                }
            }
        }

        Label { id: status; Layout.fillWidth: true; wrapMode: Text.Wrap }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: []
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    contentItem: RowLayout {
                        Label {
                            text: "#" + row.modelData.id + "  " + row.modelData.customer
                                  + (row.modelData.customerTaxId ? "  (" + row.modelData.customerTaxId + ")" : "")
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label { text: window.formatCents(row.modelData.totalCents) }
                    }
                }
            }
        }
    }

    Component.onCompleted: invoices.list()
}
