// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

FormSection {
    id: root
    property var entryModel: null
    property bool input: false
    property int displayUnit: ApplicationModels.optionsModel.displayUnit
    property string walletName: qsTr("Your wallet")
    signal entryRequested(int row)
    signal paymentRequestRequested(string requestId)
    sectionSpacing: 12
    ListView {
        id: rows
        objectName: root.objectName + "Rows"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(360, Math.max(80, contentHeight))
        model: root.visible ? root.entryModel : null
        cacheBuffer: 0
        reuseItems: true
        spacing: Theme.space.ml
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: ColumnLayout {
            id: row
            objectName: "transactionEntryPreview"
            required property int index
            required property var entry
            width: rows.width
            spacing: Theme.space.xs
            TransactionFlowNode {
                Layout.fillWidth: true
                input: root.input
                entry: row.entry
                walletName: root.walletName
                displayUnit: root.displayUnit
                onPaymentRequestRequested: (requestId) => root.paymentRequestRequested(requestId)
            }
            LinkButton {
                objectName: root.objectName + "Entry_" + row.index
                text: row.entry.textTruncated || row.entry.paymentRequestCount > 4
                    ? qsTr("View complete entry") : qsTr("View entry fields")
                onClicked: root.entryRequested(row.index)
            }
        }
        CoreText {
            visible: rows.count === 0
            text: qsTr("No entries")
            textStyle: Theme.text.caption
            color: Theme.color.neutral7
        }
    }
}
