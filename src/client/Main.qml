pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guita
import "Amount.js" as Amount

ApplicationWindow {
    id: window

    readonly property string modeName: {
        switch (ApiConnection.mode) {
        case ApiConnection.Web:        return qsTr("Web")
        case ApiConnection.Standalone: return qsTr("Standalone")
        default:                       return qsTr("Connected")
        }
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
        }
        // The list follows the events: reloaded when watching starts (also
        // after a reconnection, which may have missed some) and on every
        // invoice created, here or by another client
        onWatching: invoices.list()
        onInvoiceCreated: invoices.list()
        onListed: (rows) => list.model = rows
        onFailed: (message) => status.text = qsTr("Error: %1").arg(message)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            // In process there is no URL: the backend runs inside the application
            text: ApiConnection.mode === ApiConnection.Standalone
                  ? qsTr("%1 mode · in process").arg(window.modeName)
                  : qsTr("%1 mode · %2").arg(window.modeName).arg(ApiConnection.url)
            opacity: 0.6
            font.pixelSize: 12
        }

        Label {
            visible: !ApiConnection.available
            text: qsTr("No connection to the backend: reconnecting…")
            color: "firebrick"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
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
                    const cents = Amount.toCents(total.text)
                    if (cents < 0) {
                        status.text = qsTr("The total must be an amount such as 1234.50")
                        return
                    }
                    invoices.create(customer.text, customerTaxId.text, cents)
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
                    required property invoiceRow modelData
                    width: ListView.view.width
                    contentItem: RowLayout {
                        Label {
                            text: "#" + row.modelData.id + "  " + row.modelData.customer
                                  + (row.modelData.hasCustomerTaxId ? "  (" + row.modelData.customerTaxId + ")" : "")
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label { text: Amount.formatCents(row.modelData.totalCents) }
                    }
                }
            }
        }
    }

    Component.onCompleted: invoices.watch()
}
