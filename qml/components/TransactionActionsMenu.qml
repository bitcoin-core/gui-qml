// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../controls"

// The actions on one transaction, shared by its activity row and its
// details page.
ContextMenu {
    id: root

    property string txid: ""
    // Returns the raw transaction hex for txid, or "" when unavailable.
    property var rawTransaction: null
    property string itemObjectNamePrefix: "transaction"
    property var clipboard: Clipboard
    readonly property var explorerLinks: txid.length > 0 ? optionsModel.thirdPartyTransactionLinks(txid) : []

    // Explorer links send the transaction ID to a third party, so the host
    // confirms them.
    signal explorerLinkRequested(string url)

    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyTransactionId"
        text: qsTr("Copy transaction ID")
        iconSource: "image://images/copy"
        onTriggered: root.clipboard.setText(root.txid)
    }
    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyRawTransaction"
        visible: typeof root.rawTransaction === "function"
        text: qsTr("Copy raw transaction hex")
        iconSource: "image://images/copy"
        onTriggered: {
            const hex = root.rawTransaction(root.txid)
            if (hex.length === 0) return
            root.clipboard.setText(hex)
        }
    }
    ContextMenuDivider {
        visible: root.explorerLinks.length > 0
    }
    Repeater {
        model: root.explorerLinks
        delegate: ContextMenuButton {
            required property var modelData
            required property int index
            objectName: "transactionExplorerLink_" + index
            text: qsTr("Show in %1").arg(modelData.host)
            iconSource: "image://images/export"
            onTriggered: root.explorerLinkRequested(modelData.url)
        }
    }
}
