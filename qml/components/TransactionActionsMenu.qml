// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

// The actions on one transaction, shared by its activity row and its
// details page.
ContextMenu {
    id: root

    property string txid: ""
    // Returns the raw transaction hex for txid, or "" when unavailable.
    property var rawTransaction: null
    property var wallet: null
    property string itemObjectNamePrefix: "transaction"
    property var clipboard: Clipboard
    readonly property var explorerLinks: txid.length > 0 ? optionsModel.thirdPartyTransactionLinks(txid) : []
    property string rebroadcastStatus: ""
    property bool rebroadcastStatusIsError: false
    onAboutToShow: rebroadcastStatus = ""

    // Explorer links send the transaction ID to a third party, so the host
    // confirms them.
    signal explorerLinkRequested(string url)

    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyTransactionId"
        text: qsTr("Copy transaction ID")
        iconSource: "qrc:/icons/copy.svg"
        onTriggered: root.clipboard.setText(root.txid)
    }
    ContextMenuButton {
        objectName: root.itemObjectNamePrefix + "CopyRawTransaction"
        visible: typeof root.rawTransaction === "function"
        text: qsTr("Copy raw transaction hex")
        iconSource: "qrc:/icons/copy.svg"
        onTriggered: {
            const hex = root.rawTransaction(root.txid)
            if (hex.length === 0) return
            root.clipboard.setText(hex)
        }
    }
    Connections {
        target: root.wallet
        function onTransactionRebroadcastFinished(txid, error) {
            if (!root.opened || txid !== root.txid) {
                return
            }
            if (error.length > 0) {
                root.rebroadcastStatus = error
                root.rebroadcastStatusIsError = true
            } else {
                root.rebroadcastStatus = qsTr("Rebroadcast scheduled.")
                root.rebroadcastStatusIsError = false
            }
        }
    }
    ContextMenuButton {
        id: idRebroadcastTransaction
        objectName: root.itemObjectNamePrefix + "RebroadcastTransaction"
        autoClose: false
        text: qsTr("Rebroadcast transaction")
        iconSource: "qrc:/icons/activity-split.svg"
        onTriggered: {
            if (!root.wallet) {
                root.rebroadcastStatus = qsTr("No wallet.")
                root.rebroadcastStatusIsError = true
                return
            }
            root.rebroadcastStatus = ""
            root.rebroadcastStatusIsError = false
            const err = root.wallet.rebroadcastTransaction(root.txid)
            if (err.length > 0) {
                root.rebroadcastStatus = err
                root.rebroadcastStatusIsError = true
            }
        }
    }
    CoreText {
        objectName: root.itemObjectNamePrefix + "RebroadcastTransactionStatus"
        visible: root.rebroadcastStatus.length > 0
        text: root.rebroadcastStatus
        color: root.rebroadcastStatusIsError ? Theme.color.warning : Theme.color.success
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WrapAnywhere
        Layout.leftMargin: idRebroadcastTransaction.leftPadding
        Layout.maximumWidth: 240
    }

    ContextMenuDivider {
        visible: root.explorerLinks.length > 0
    }
    // A positioner keeps Repeater delegates out of the Qt 6.4 layout engine.
    // The preferred width stands in for its implicit width, which a Column
    // derives from the (parent-bound) widths of its delegates.
    Column {
        Layout.fillWidth: true
        Layout.preferredWidth: {
            let widest = 0
            for (let i = 0; i < explorerLinkRepeater.count; ++i) {
                const item = explorerLinkRepeater.itemAt(i)
                if (item) widest = Math.max(widest, item.implicitWidth)
            }
            return widest
        }
        visible: root.explorerLinks.length > 0

        Repeater {
            id: explorerLinkRepeater
            model: root.explorerLinks
            delegate: ContextMenuButton {
                required property var modelData
                required property int index
                objectName: "transactionExplorerLink_" + index
                width: parent ? parent.width : 0
                text: qsTr("Show in %1").arg(modelData.host)
                iconSource: "image://images/export"
                onTriggered: root.explorerLinkRequested(modelData.url)
            }
        }
    }
}
